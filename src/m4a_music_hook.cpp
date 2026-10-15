#include "gbaimp/m4a_music_hook.h"

#include "gbaimp/hd_audio_player.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace gbaimp {

// ---------------------------------------------------------------------------------------------
// Locator
// ---------------------------------------------------------------------------------------------
namespace {

struct Pat {
    uint16_t value;
    uint16_t mask;
};

constexpr uint16_t kExact = 0xFFFFu;

// Thumb `bl` is a pair: high half 0xF000..0xF7FF, low half 0xF800..0xFFFF.
constexpr Pat kBlHi{0xF000u, 0xF800u};
constexpr Pat kBlLo{0xF800u, 0xF800u};

struct Image {
    const uint8_t* d;
    size_t n;
    uint32_t base;

    bool has(size_t off, size_t len) const { return off <= n && len <= n - off; }
    uint16_t u16(size_t off) const { return static_cast<uint16_t>(d[off] | (d[off + 1] << 8)); }
    uint32_t u32(size_t off) const {
        return static_cast<uint32_t>(d[off]) | (static_cast<uint32_t>(d[off + 1]) << 8) |
               (static_cast<uint32_t>(d[off + 2]) << 16) | (static_cast<uint32_t>(d[off + 3]) << 24);
    }
    // Guest address (any of the three cartridge mirrors) -> offset in the image.
    bool offset_of(uint32_t addr, size_t* out) const {
        if (addr < 0x08000000u || addr >= 0x0E000000u) return false;
        const uint32_t off = (addr & 0x01FFFFFFu) - (base & 0x01FFFFFFu);
        if ((addr & 0x01FFFFFFu) < (base & 0x01FFFFFFu) || off >= n) return false;
        *out = off;
        return true;
    }
};

bool match(const Image& im, size_t off, const Pat* p, size_t count) {
    if (!im.has(off, count * 2)) return false;
    for (size_t i = 0; i < count; ++i) {
        if ((im.u16(off + i * 2) & p[i].mask) != p[i].value) return false;
    }
    return true;
}

// Head shared by the four song-number functions:
//   push {lr}; lsls r0,r0,#16; ldr rA,=gMPlayTable; ldr r1,=gSongTable; lsrs r0,r0,#13; adds r0,r0,r1
// then a "ptr = &gMPlayTable[gSongTable[n].player]" sequence in one of two register allocations.
const Pat kHead[] = {{0xB500, kExact}, {0x0400, kExact}, {0x4800, 0xF800}, {0x4900, 0xFF00},
                     {0x0B40, kExact}, {0x1840, kExact}};
const Pat kBodyA[] = {{0x8882, kExact}, {0x0051, kExact}, {0x1889, kExact}, {0x0089, kExact}, {0x18C9, kExact}};
const Pat kBodyB[] = {{0x8883, kExact}, {0x0059, kExact}, {0x18C9, kExact}, {0x0089, kExact}, {0x1889, kExact}};
constexpr size_t kHeadLen = sizeof(kHead) / sizeof(kHead[0]);
constexpr size_t kBodyLen = 5;

// Tails (after head + body, 11 halfwords from the function start).
// Start:       ldr r2,[r1]; ldr r1,[r0]; mov r0,r2; bl MPlayStart; pop {r0}; bx r0
const Pat kTailStart[] = {{0x680A, kExact}, {0x6801, kExact}, {0x1C10, kExact}, kBlHi, kBlLo,
                          {0xBC01, kExact}, {0x4700, kExact}};
// StartOrChange / StartOrContinue: compare the playing header, then bl MPlayStart and branch out.
// The branch distance after the call tells the two apart (0xE00E vs 0xE012).
const Pat kTailChange[] = {{0x6809, kExact}, {0x680B, kExact}, {0x6802, kExact}, {0x4293, kExact},
                           {0xD009, kExact}, {0x1C08, kExact}, {0x1C11, kExact}, kBlHi, kBlLo,
                           {0xE00E, kExact}};
const Pat kTailContinue[] = {{0x6809, kExact}, {0x680B, kExact}, {0x6802, kExact}, {0x4293, kExact},
                             {0xD009, kExact}, {0x1C08, kExact}, {0x1C11, kExact}, kBlHi, kBlLo,
                             {0xE012, kExact}};
// Stop / Continue share one shape (if (info->song == header) callee(info)); the callee tells them apart.
const Pat kTailStopShape[] = {{0x680A, kExact}, {0x6811, kExact}, {0x6800, kExact}, {0x4281, kExact},
                              {0xD102, kExact}, {0x1C10, kExact}, kBlHi, kBlLo, {0xBC01, kExact},
                              {0x4700, kExact}};
// AllStop: push {r4,r5,lr}; ldr r0,=pool; lsls r0,r0,#16; lsrs r0,r0,#16; cmp r0,#0; beq; ldr r5,=...
const Pat kAllStop[] = {{0xB530, kExact}, {0x4800, 0xF800}, {0x0400, kExact}, {0x0C00, kExact},
                        {0x2800, kExact}, {0xD008, kExact}, {0x4800, 0xF800}, {0x1C04, kExact},
                        {0x6828, kExact}};

// Does a song-number function with the given tail start at `off`? On success `*body_end`
// is the offset right after head + body.
bool match_song_fn(const Image& im, size_t off, const Pat* tail, size_t tail_len) {
    if (!match(im, off, kHead, kHeadLen)) return false;
    const size_t bo = off + kHeadLen * 2;
    if (!match(im, bo, kBodyA, kBodyLen) && !match(im, bo, kBodyB, kBodyLen)) return false;
    return match(im, bo + kBodyLen * 2, tail, tail_len);
}

// Decodes a bl pair at `off` into the guest target address; 0 when it is not one.
uint32_t decode_bl(const Image& im, size_t off) {
    if (!im.has(off, 4)) return 0;
    const uint16_t hi = im.u16(off), lo = im.u16(off + 2);
    if ((hi & 0xF800u) != 0xF000u || (lo & 0xF800u) != 0xF800u) return 0;
    int32_t imm = static_cast<int32_t>(hi & 0x7FFu);
    if (imm & 0x400) imm -= 0x800;                       // sign extend the 11-bit high part
    const int64_t rel = (static_cast<int64_t>(imm) << 12) + (static_cast<int64_t>(lo & 0x7FFu) << 1);
    return static_cast<uint32_t>(static_cast<int64_t>(im.base) + static_cast<int64_t>(off) + 4 + rel);
}

// Offset of the bl inside a matched function (first bl at or after `from`, within 0x40 bytes).
size_t find_bl(const Image& im, size_t from) {
    for (size_t o = from; o < from + 0x40 && im.has(o, 4); o += 2) {
        if (decode_bl(im, o)) return o;
    }
    return static_cast<size_t>(-1);
}

// Literal loaded by `ldr rD,[pc,#imm]` at halfword offset `off`.
bool pool_literal(const Image& im, size_t off, uint32_t* out) {
    const uint16_t ins = im.u16(off);
    if ((ins & 0xF800u) != 0x4800u) return false;
    const size_t pc = (im.base & 3u) + off + 4;          // alignment is of the guest address
    const size_t addr = (pc & ~static_cast<size_t>(3)) + static_cast<size_t>(ins & 0xFFu) * 4;
    const size_t lit = addr - (im.base & 3u);
    if (!im.has(lit, 4)) return false;
    *out = im.u32(lit);
    return true;
}

// First offset in [from, to) where `ok(off)` is true (halfword steps).
template <typename F>
size_t scan(const Image& im, size_t from, size_t to, F ok) {
    if (to > im.n) to = im.n;
    for (size_t o = from; o + 2 <= to; o += 2) {
        if (ok(o)) return o;
    }
    return static_cast<size_t>(-1);
}

constexpr size_t kNone = static_cast<size_t>(-1);
constexpr size_t kGroupSpan = 0x400;

bool plausible_song_header(const Image& im, uint32_t hdr) {
    size_t o;
    return (hdr & 3u) == 0 && im.offset_of(hdr, &o);
}

// Entries with a plausible header pointer (and, when the player-table size is known, a player
// index inside it): the table is not terminated, so the first bad entry ends it.
uint32_t count_song_entries(const Image& im, size_t table_off, uint32_t players) {
    uint32_t n = 0;
    while (n < 0x10000u && im.has(table_off + n * 8u, 8)) {
        if (!plausible_song_header(im, im.u32(table_off + n * 8u))) break;
        if (players && (im.u32(table_off + n * 8u + 4u) & 0xFFFFu) >= players) break;
        ++n;
    }
    return n;
}

} // namespace

size_t count_m4a_idents(const uint8_t* rom, size_t size) {
    size_t count = 0;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        if (rom[i] == 'S' && rom[i + 1] == 'm' && rom[i + 2] == 's' && rom[i + 3] == 'h') ++count;
    }
    return count;
}

M4aAddresses locate_m4a(const uint8_t* rom, size_t size, uint32_t rom_base) {
    M4aAddresses none;
    if (!rom || size < 0x100) return none;
    const Image im{rom, size, rom_base};

    for (size_t s = 0; s + 2 <= size; s += 2) {
        // Cheap first-halfword reject: push {lr} then lsls r0,r0,#16.
        if (rom[s] != 0x00 || rom[s + 1] != 0xB5) continue;
        if (!match_song_fn(im, s, kTailStart, sizeof(kTailStart) / sizeof(Pat))) continue;
        const size_t end = s + kGroupSpan;

        const size_t o_change = scan(im, s + 0x20, end, [&](size_t o) {
            return match_song_fn(im, o, kTailChange, sizeof(kTailChange) / sizeof(Pat));
        });
        if (o_change == kNone) continue;
        const size_t o_cont = scan(im, o_change + 0x20, end, [&](size_t o) {
            return match_song_fn(im, o, kTailContinue, sizeof(kTailContinue) / sizeof(Pat));
        });
        if (o_cont == kNone) continue;
        const size_t o_all = scan(im, o_cont + 0x20, end, [&](size_t o) {
            return match(im, o, kAllStop, sizeof(kAllStop) / sizeof(Pat));
        });
        if (o_all == kNone) continue;

        // The three start functions must call the same routine (MPlayStart).
        const size_t bl_s = find_bl(im, s + 0x10);
        const size_t bl_c = find_bl(im, o_change + 0x10);
        const size_t bl_k = find_bl(im, o_cont + 0x10);
        if (bl_s == kNone || bl_c == kNone || bl_k == kNone) continue;
        const uint32_t mplay_start = decode_bl(im, bl_s);
        if (decode_bl(im, bl_c) != mplay_start || decode_bl(im, bl_k) != mplay_start) continue;

        // m4aSongNumStop: the stop-shaped function that calls the routine AllStop loops over.
        const size_t bl_a = find_bl(im, o_all + 0x10);
        if (bl_a == kNone) continue;
        const uint32_t mplay_stop = decode_bl(im, bl_a);
        const size_t o_stop = scan(im, o_cont + 0x20, o_all, [&](size_t o) {
            if (!match_song_fn(im, o, kTailStopShape, sizeof(kTailStopShape) / sizeof(Pat))) return false;
            const size_t b = find_bl(im, o + 0x10);
            return b != kNone && decode_bl(im, b) == mplay_stop;
        });
        if (o_stop == kNone) continue;

        M4aAddresses a;
        a.song_num_start = rom_base + static_cast<uint32_t>(s);
        a.song_num_start_or_change = rom_base + static_cast<uint32_t>(o_change);
        a.song_num_start_or_continue = rom_base + static_cast<uint32_t>(o_cont);
        a.song_num_stop = rom_base + static_cast<uint32_t>(o_stop);
        a.mplay_all_stop = rom_base + static_cast<uint32_t>(o_all);
        a.mplay_start = mplay_start;
        a.mplay_stop = mplay_stop;
        // The two pool literals of m4aSongNumStart: gMPlayTable first, gSongTable second.
        uint32_t mp = 0, st = 0;
        if (pool_literal(im, s + 4, &mp) && pool_literal(im, s + 6, &st)) {
            a.mplay_table = mp;
            size_t toff;
            if (im.offset_of(st, &toff)) {
                a.song_table = st;
                // gMPlayTable (12-byte entries) normally sits right before gSongTable.
                uint32_t players = 0;
                if (st > mp && (st - mp) % 12u == 0 && (st - mp) / 12u <= 255u) players = (st - mp) / 12u;
                a.song_table_entries = count_song_entries(im, toff, players);
            }
        }
        return a;
    }
    return none;
}

// ---------------------------------------------------------------------------------------------
// Policy
// ---------------------------------------------------------------------------------------------

struct M4aMusicHook::DefaultSink final : HdMusicSink {
    HdAudioPlayer& hd() const { return HdAudioPlayer::instance(); }
    bool enabled() const override { return hd().settings().enabled; }
    bool has_track(uint32_t id) const override { return hd().has_track(id); }
    uint32_t current_song_id() const override { return hd().current_song_id(); }
    void trigger_song(uint32_t id) override { hd().trigger_song(id); }
    void stop_music() override { hd().stop_music(); }
    void load_track_mapping(const std::string& p) override { hd().load_track_mapping(p); }
};

M4aMusicHook::M4aMusicHook() : default_sink_(new DefaultSink) {}
M4aMusicHook::~M4aMusicHook() { delete default_sink_; }

M4aMusicHook& M4aMusicHook::instance() {
    static M4aMusicHook h;
    return h;
}

HdMusicSink& M4aMusicHook::sink() const { return sink_ ? *sink_ : *default_sink_; }
void M4aMusicHook::set_sink(HdMusicSink* s) { sink_ = s; }

void M4aMusicHook::set_addresses(const M4aAddresses& a) {
    addr_ = a;
    source_ = M4aSetupSource::Manual;
    const uint32_t f[5] = {a.song_num_start, a.song_num_start_or_change, a.song_num_start_or_continue,
                           a.song_num_stop, a.mplay_all_stop};
    lo_ = 0xFFFFFFFFu;
    hi_ = 0;
    for (uint32_t v : f) {
        if (!v) continue;
        if (v < lo_) lo_ = v;
        if (v > hi_) hi_ = v;
    }
    if (hi_ == 0) lo_ = 0;
}

bool M4aMusicHook::locate(const uint8_t* rom, size_t size, uint32_t rom_base) {
    const M4aAddresses a = locate_m4a(rom, size, rom_base);
    set_addresses(a);
    source_ = a.complete() ? M4aSetupSource::Locator : M4aSetupSource::None;
    return a.complete();
}

M4aSetupSource M4aMusicHook::setup_from_rom(const uint8_t* rom, size_t size, const M4aSetupOptions& o) {
    if (!rom || size == 0) return M4aSetupSource::None;
    if (o.log_tag) set_log_tag(o.log_tag);
    if (o.env_prefix) set_logging_from_env(o.env_prefix);
    if (!locate(rom, size, o.rom_base) && o.fallback) {
        set_addresses(*o.fallback);
        source_ = M4aSetupSource::Fallback;
    }
    if (o.bus_read_u32) set_bus_reader(o.bus_read_u32);
    else set_rom_image(rom, size, o.rom_base);   // no bus: read the song table from the image
    return source_;
}

void M4aMusicHook::set_song_player_reader(std::function<uint32_t(uint32_t)> r) { reader_ = std::move(r); }
void M4aMusicHook::set_bus_reader(uint32_t (*fn)(uint32_t)) { bus_ = fn; }
void M4aMusicHook::set_rom_image(const uint8_t* rom, size_t size, uint32_t base) {
    rom_ = rom;
    rom_size_ = size;
    rom_base_ = base;
}

void M4aMusicHook::load_track_mapping(const std::string& ini_path) { sink().load_track_mapping(ini_path); }

void M4aMusicHook::set_log_tag(const char* tag) { tag_ = (tag && *tag) ? tag : "gbaimp"; }
void M4aMusicHook::set_logging(bool on) { log_ = on; }
void M4aMusicHook::set_logging_from_env(const char* env_prefix) {
    const std::string name = std::string(env_prefix ? env_prefix : "") + "LOG_INTERPOSE";
    const char* v = std::getenv(name.c_str());
    log_ = v && *v && std::strcmp(v, "0") != 0;
}

uint32_t M4aMusicHook::song_player(uint32_t id) const {
    if (reader_) return reader_(id);
    if (!addr_.song_table) return 0xFFFFFFFFu;
    if (addr_.song_table_entries && id >= addr_.song_table_entries) return 0xFFFFFFFFu;
    // Entry = { u32 header; u16 ms; u16 me }: the player index is the low half of word 1.
    const uint32_t a = addr_.song_table + id * 8u + 4u;
    if (bus_) return bus_(a) & 0xFFFFu;
    if (rom_) {
        const Image im{rom_, rom_size_, rom_base_};
        size_t off;
        if (im.offset_of(a, &off) && im.has(off, 4)) return im.u32(off) & 0xFFFFu;
    }
    return 0xFFFFFFFFu;
}

void M4aMusicHook::log_event(const char* what, uint32_t id, uint32_t player, const char* decision) {
    if (!log_) return;
    if (log_lines_.fetch_add(1, std::memory_order_relaxed) >= 40) return;   // low-noise: first events only
    std::fprintf(stderr, "[%s] m4a %s id=%u player=%u -> %s\n", tag_.c_str(), what, id, player, decision);
}

void M4aMusicHook::stop_hd(const char* why, uint32_t id, uint32_t player) {
    sink().stop_music();
    hd_player_ = 0xFFFFFFFFu;
    stops_.fetch_add(1, std::memory_order_relaxed);
    log_event(why, id, player, "HD stop");
}

void M4aMusicHook::on_song_start(uint32_t song_id, uint32_t player) {
    starts_.fetch_add(1, std::memory_order_relaxed);
    HdMusicSink& hd = sink();
    if (!hd.enabled()) return;

    if (hd.has_track(song_id)) {
        mapped_.fetch_add(1, std::memory_order_relaxed);
        hd.trigger_song(song_id);
        const bool playing = hd.current_song_id() == song_id;
        if (playing) hd_player_ = player;
        log_event("start", song_id, player, playing ? "HD trigger (playing)" : "HD trigger (file unavailable, silent)");
        return;
    }
    if (hd.current_song_id() != 0 && player == hd_player_) {
        stop_hd("start", song_id, player);   // unmapped song replaced the music the HD track belongs to
        return;
    }
    log_event("start", song_id, player, "native only");
}

void M4aMusicHook::on_song_stop(uint32_t song_id) {
    HdMusicSink& hd = sink();
    if (hd.current_song_id() != 0 && hd.current_song_id() == song_id) stop_hd("stop", song_id, hd_player_);
}

void M4aMusicHook::on_all_stop() {
    if (sink().current_song_id() != 0) stop_hd("all-stop", 0, hd_player_);
}

void M4aMusicHook::on_function_entry(uint32_t pc, uint32_t r0) {
    pc &= ~1u;
    if (pc < lo_ || pc > hi_ || hi_ == 0) return;   // fast reject: one narrow window of the m4a library
    if (pc == addr_.song_num_start || pc == addr_.song_num_start_or_change ||
        pc == addr_.song_num_start_or_continue) {
        const uint32_t id = r0 & 0xFFFFu;   // u16 argument (the callee masks it too)
        on_song_start(id, song_player(id));
    } else if (pc == addr_.song_num_stop) {
        on_song_stop(r0 & 0xFFFFu);
    } else if (pc == addr_.mplay_all_stop) {
        on_all_stop();
    }
}

M4aHookStats M4aMusicHook::stats() const {
    M4aHookStats s;
    s.starts = starts_.load(std::memory_order_relaxed);
    s.mapped_triggers = mapped_.load(std::memory_order_relaxed);
    s.hd_stops = stops_.load(std::memory_order_relaxed);
    return s;
}

void M4aMusicHook::print_stats(const char* tag, std::FILE* out) const {
    const M4aHookStats m = stats();
    std::fprintf(out ? out : stderr, "[%s] m4a song hook: starts=%llu hd_mapped=%llu hd_stops=%llu\n",
                 (tag && *tag) ? tag : tag_.c_str(), static_cast<unsigned long long>(m.starts),
                 static_cast<unsigned long long>(m.mapped_triggers), static_cast<unsigned long long>(m.hd_stops));
}

void M4aMusicHook::reset_stats_and_state() {
    starts_ = 0;
    mapped_ = 0;
    stops_ = 0;
    log_lines_ = 0;
    hd_player_ = 0xFFFFFFFFu;
}

} // namespace gbaimp
