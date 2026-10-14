#pragma once
// HD music trigger for games that use the MusicPlayer2000 ("m4a") sound engine.
//
// Two independent parts:
//
//  1. locate_m4a(): finds the public m4a song-control functions inside a ROM image by their
//     Thumb code shape (no fixed addresses), together with the song table they index.
//     A host that already knows the addresses can fill M4aAddresses by hand instead.
//
//  2. M4aMusicHook: the policy. It is driven by the runtime's function-entry hook (the host
//     keeps owning that hook and forwards "pc, R0" to on_function_entry()):
//       * a started song that has a mapping (config/music_tracks.ini)       -> trigger_song(id)
//       * a started song WITHOUT a mapping on the SAME music player the HD
//         track was started for (the game replaced that music)               -> stop_music()
//       * m4aSongNumStop of the HD song, and m4aMPlayAllStop                 -> stop_music()
//       * anything else (sound effects on other players, unmapped ids)       -> left alone
//     The original m4a music keeps playing underneath: only the host's mixed output is visible.
//
// Host adoption (see docs/MODULES.md): at start-up
//     auto& hook = gbaimp::M4aMusicHook::instance();
//     hook.set_log_tag("mygame");  hook.set_logging_from_env("MYGAME_");   // optional
//     hook.locate(rom_ptr, rom_size);        // or hook.set_addresses(...)
//     hook.set_bus_reader(&my_bus_read_u32); // reads guest memory (for the song table)
//     hook.load_track_mapping("config/music_tracks.ini");
// and from the function-entry hook:  hook.on_function_entry(pc, cpu.R[0]);
// The library needs no gbarecomp header.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <string>

namespace gbaimp {

// ---------------------------------------------------------------------------------------------
// Locator
// ---------------------------------------------------------------------------------------------

// Guest addresses (0x08xxxxxx for the ROM, as the CPU sees it). 0 = not found / unknown.
struct M4aAddresses {
    uint32_t song_num_start = 0;             // m4aSongNumStart(u16 n)            R0 = song id
    uint32_t song_num_start_or_change = 0;   // m4aSongNumStartOrChange(u16 n)    R0 = song id
    uint32_t song_num_start_or_continue = 0; // m4aSongNumStartOrContinue(u16 n)  R0 = song id
    uint32_t song_num_stop = 0;              // m4aSongNumStop(u16 n)             R0 = song id
    uint32_t mplay_all_stop = 0;             // m4aMPlayAllStop()
    // Extra facts recovered by the locator (informational for the hook; 0 when set by hand).
    uint32_t mplay_start = 0;                // MPlayStart, called by the three start functions
    uint32_t mplay_stop = 0;                 // MPlayStop, called by song_num_stop and mplay_all_stop
    uint32_t song_table = 0;                 // gSongTable: 8-byte entries {u32 header, u16 player, u16 me}
    uint32_t mplay_table = 0;                // gMPlayTable: 12-byte entries
    uint32_t song_table_entries = 0;         // entries with a plausible header pointer (0 = unknown)

    // All five function addresses are known.
    bool complete() const {
        return song_num_start && song_num_start_or_change && song_num_start_or_continue &&
               song_num_stop && mplay_all_stop;
    }
};

// Counts the 4-byte-aligned "Smsh" literals (the m4a sound-engine ident, 0x68736D53) in the ROM.
// A ROM that has none does not use the engine.
size_t count_m4a_idents(const uint8_t* rom, size_t size);

// Looks for the five m4a functions in a ROM image mapped at `rom_base` (0x08000000 for a cartridge).
// The group is accepted only when all of these hold: the five functions have the expected Thumb
// shape and appear in this order within 0x400 bytes, the three start functions call the same
// routine, and m4aSongNumStop calls the same routine as m4aMPlayAllStop (MPlayStop). Two compiler
// register allocations are recognised. On failure the result is default-constructed (complete()
// is false). Cost: one linear pass over the image.
M4aAddresses locate_m4a(const uint8_t* rom, size_t size, uint32_t rom_base = 0x08000000u);

// ---------------------------------------------------------------------------------------------
// Policy
// ---------------------------------------------------------------------------------------------

// What the hook needs from the HD player. HdAudioPlayerSink adapts gbaimp::HdAudioPlayer; tests
// and other back ends can implement it.
class HdMusicSink {
public:
    virtual ~HdMusicSink() = default;
    virtual bool enabled() const = 0;
    virtual bool has_track(uint32_t song_id) const = 0;
    virtual uint32_t current_song_id() const = 0;   // 0 = nothing playing
    virtual void trigger_song(uint32_t song_id) = 0;
    virtual void stop_music() = 0;
    virtual void load_track_mapping(const std::string& ini_path) = 0;
};

struct M4aHookStats {
    uint64_t starts = 0;            // m4aSongNum*Start* calls seen
    uint64_t mapped_triggers = 0;   // of those, ids that have a mapping (trigger_song was called)
    uint64_t hd_stops = 0;          // times the HD track was stopped by the hook
};

class M4aMusicHook {
public:
    M4aMusicHook();   // sink = the shared HdAudioPlayer::instance()
    ~M4aMusicHook();
    M4aMusicHook(const M4aMusicHook&) = delete;
    M4aMusicHook& operator=(const M4aMusicHook&) = delete;
    static M4aMusicHook& instance();

    // Back end. nullptr restores the shared HdAudioPlayer. The sink must outlive the hook.
    void set_sink(HdMusicSink* sink);

    // Function addresses: from locate() or set by the host (e.g. a per-game table).
    bool locate(const uint8_t* rom, size_t size, uint32_t rom_base = 0x08000000u);   // true when complete
    void set_addresses(const M4aAddresses& a);
    const M4aAddresses& addresses() const { return addr_; }

    // Where the player index of a song is read from (gSongTable[id].player). In order of use:
    //   1. set_song_player_reader (test seam / custom lookup, receives the song id)
    //   2. set_bus_reader: guest memory read at song_table + id * 8 + 4
    //   3. set_rom_image: the cartridge bytes (the host keeps the buffer alive)
    // Ids at or beyond song_table_entries (when known) and unreadable ids give 0xFFFFFFFF.
    void set_song_player_reader(std::function<uint32_t(uint32_t song_id)> reader);
    void set_bus_reader(uint32_t (*read_u32)(uint32_t addr));
    void set_rom_image(const uint8_t* rom, size_t size, uint32_t rom_base = 0x08000000u);

    // Delegates to the sink (HdAudioPlayer parses the [tracks] section: "<song id> = <file>").
    void load_track_mapping(const std::string& ini_path);

    // Diagnostics on stderr: "[tag] m4a start id=N player=P -> decision", first 40 events only.
    void set_log_tag(const char* tag);
    void set_logging(bool on);
    // Enables logging when the environment variable "<prefix>LOG_INTERPOSE" is set to
    // anything but "" or "0".
    void set_logging_from_env(const char* env_prefix);

    // Function-entry dispatch: a cheap reject for every pc that is not an m4a entry point.
    // `r0` is the guest R0 at function entry (the u16 song id argument).
    void on_function_entry(uint32_t pc, uint32_t r0);

    // The decisions, separated from the CPU so they can be unit tested.
    void on_song_start(uint32_t song_id, uint32_t player);
    void on_song_stop(uint32_t song_id);
    void on_all_stop();

    M4aHookStats stats() const;
    void reset_stats_and_state();

private:
    uint32_t song_player(uint32_t song_id) const;
    void log_event(const char* what, uint32_t id, uint32_t player, const char* decision);
    void stop_hd(const char* why, uint32_t id, uint32_t player);
    HdMusicSink& sink() const;

    M4aAddresses addr_;
    uint32_t lo_ = 0, hi_ = 0;   // address window for the fast reject
    HdMusicSink* sink_ = nullptr;
    struct DefaultSink;
    DefaultSink* default_sink_ = nullptr;

    std::function<uint32_t(uint32_t)> reader_;
    uint32_t (*bus_)(uint32_t) = nullptr;
    const uint8_t* rom_ = nullptr;
    size_t rom_size_ = 0;
    uint32_t rom_base_ = 0x08000000u;

    std::string tag_ = "gbaimp";
    bool log_ = false;
    std::atomic<int> log_lines_{0};
    std::atomic<uint64_t> starts_{0}, mapped_{0}, stops_{0};
    uint32_t hd_player_ = 0xFFFFFFFFu;   // music player the current HD track was started for
};

} // namespace gbaimp
