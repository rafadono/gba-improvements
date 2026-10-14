#define SDL_MAIN_HANDLED  // the test provides plain main(); no SDL_main redirection
#include "test_harness.h"
#include "gbaimp/m4a_music_hook.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

using namespace gbaimp;

namespace {

// ---- synthetic ROM: the five functions in the two register allocations seen in the wild ----------
struct Rom {
    std::vector<uint8_t> d;
    explicit Rom(size_t n) : d(n, 0xFF) {}
    void h(size_t off, uint16_t v) { d[off] = uint8_t(v); d[off + 1] = uint8_t(v >> 8); }
    void w(size_t off, uint32_t v) { h(off, uint16_t(v)); h(off + 2, uint16_t(v >> 16)); }
    // bl at `off` (guest base 0x08000000) calling `target`
    void bl(size_t off, uint32_t target) {
        const int32_t rel = int32_t(target) - int32_t(0x08000000u + off + 4);
        h(off, uint16_t(0xF000u | ((rel >> 12) & 0x7FF)));
        h(off + 2, uint16_t(0xF800u | ((rel >> 1) & 0x7FF)));
    }
};

constexpr uint32_t kBase = 0x08000000u;
constexpr uint32_t kSongTable = 0x0A000400u;   // reached through the wait-state-1 mirror of the ROM
constexpr uint32_t kMPlayTable = 0x08000300u;
constexpr uint32_t kMPlayStart = kBase + 0x7000u;
constexpr uint32_t kMPlayStop = kBase + 0x7100u;
constexpr uint32_t kMPlayContinue = kBase + 0x7200u;

struct Layout { size_t start, change, cont, stop, cont2, all, pool; };

// ldr rd,[pc,#..] at `off` loading the word at `lit` (both 4-aligned relative to the ROM base)
uint16_t ldr_pc(unsigned rd, size_t off, size_t lit) {
    const size_t pc = (off + 4) & ~size_t(3);
    return uint16_t(0x4800u | (rd << 8) | ((lit - pc) / 4));
}

// variant 0: ldr r3 / body 8882..; variant 1: ldr r2 / body 8883..
void body(Rom& r, size_t off, int variant, size_t pool_lit) {
    r.h(off + 0, 0xB500);
    r.h(off + 2, 0x0400);
    r.h(off + 4, ldr_pc(variant == 0 ? 3 : 2, off + 4, pool_lit));        // gMPlayTable
    r.h(off + 6, ldr_pc(1, off + 6, pool_lit + 4));                        // gSongTable
    r.h(off + 8, 0x0B40);
    r.h(off + 10, 0x1840);
    const uint16_t a[5] = {0x8882, 0x0051, 0x1889, 0x0089, 0x18C9};
    const uint16_t b[5] = {0x8883, 0x0059, 0x18C9, 0x0089, 0x1889};
    for (int i = 0; i < 5; ++i) r.h(off + 12 + i * 2, variant == 0 ? a[i] : b[i]);
}

Layout build(Rom& r, size_t at, int variant, bool swap_stop_callee = false) {
    Layout L{};
    L.pool = at + 0x200;
    r.w(L.pool, kMPlayTable);
    r.w(L.pool + 4, kSongTable);
    // song table: 5 entries {header, player, me}, then a non-pointer terminator
    for (uint32_t i = 0; i < 5; ++i) {
        r.w(0x400 + i * 8, 0x08002000u + i * 0x10);
        r.w(0x400 + i * 8 + 4, (i & 1) ? 2u : 7u);   // players 7,2,7,2,7
    }
    r.w(0x400 + 5 * 8, 0xFFFFFFFFu);

    size_t p = at;
    L.start = p;
    body(r, p, variant, L.pool);
    r.h(p + 22, 0x680A); r.h(p + 24, 0x6801); r.h(p + 26, 0x1C10);
    r.bl(p + 28, kMPlayStart); r.h(p + 32, 0xBC01); r.h(p + 34, 0x4700);
    p += 0x2C;

    L.change = p;
    body(r, p, variant, L.pool);
    const uint16_t t1[] = {0x6809, 0x680B, 0x6802, 0x4293, 0xD009, 0x1C08, 0x1C11};
    for (int i = 0; i < 7; ++i) r.h(p + 22 + i * 2, t1[i]);
    r.bl(p + 36, kMPlayStart); r.h(p + 40, 0xE00E);
    p += 0x4C;

    L.cont = p;
    body(r, p, variant, L.pool);
    for (int i = 0; i < 7; ++i) r.h(p + 22 + i * 2, t1[i]);
    r.bl(p + 36, kMPlayStart); r.h(p + 40, 0xE012);
    p += 0x54;

    auto stop_shape = [&](size_t q, uint32_t callee) {
        body(r, q, variant, L.pool);
        const uint16_t t[] = {0x680A, 0x6811, 0x6800, 0x4281, 0xD102, 0x1C10};
        for (int i = 0; i < 6; ++i) r.h(q + 22 + i * 2, t[i]);
        r.bl(q + 34, callee); r.h(q + 38, 0xBC01); r.h(q + 40, 0x4700);
    };
    L.stop = p;
    stop_shape(p, swap_stop_callee ? kMPlayContinue : kMPlayStop);
    p += 0x34;
    L.cont2 = p;                                   // "continue": same shape, different callee
    stop_shape(p, swap_stop_callee ? kMPlayStop : kMPlayContinue);
    p += 0x34;

    L.all = p;
    const uint16_t a[] = {0xB530, 0x4808, 0x0400, 0x0C00, 0x2800, 0xD008, 0x4D06, 0x1C04, 0x6828};
    for (int i = 0; i < 9; ++i) r.h(p + i * 2, a[i]);
    r.bl(p + 18, kMPlayStop);
    return L;
}

// ---- fake HD back end ----------------------------------------------------------------------------
struct FakeSink : HdMusicSink {
    bool on = true;
    std::map<uint32_t, bool> tracks;     // id -> file loads
    uint32_t cur = 0;
    int triggers = 0, stops = 0, loads = 0;
    std::string last_ini;
    bool enabled() const override { return on; }
    bool has_track(uint32_t id) const override { return tracks.count(id) != 0; }
    uint32_t current_song_id() const override { return cur; }
    void trigger_song(uint32_t id) override { ++triggers; cur = tracks.at(id) ? id : 0; }
    void stop_music() override { ++stops; cur = 0; }
    void load_track_mapping(const std::string& p) override { ++loads; last_ini = p; }
};

uint32_t fake_bus(uint32_t addr) {
    // guest memory view of the synthetic song table
    if (addr >= kSongTable && addr < kSongTable + 5 * 8 && ((addr - kSongTable) % 8) == 4)
        return (((addr - kSongTable) / 8) & 1) ? 2u : 7u;
    return 0xDEADBEEFu;
}

} // namespace

int main() {
    // --- locator: both register allocations, mirrors, rejects -------------------------------------
    for (int variant = 0; variant < 2; ++variant) {
        Rom r(0x10000);
        const Layout L = build(r, 0x1000, variant);
        const M4aAddresses a = locate_m4a(r.d.data(), r.d.size());
        CHECK(a.complete());
        CHECK_EQ(a.song_num_start, kBase + L.start);
        CHECK_EQ(a.song_num_start_or_change, kBase + L.change);
        CHECK_EQ(a.song_num_start_or_continue, kBase + L.cont);
        CHECK_EQ(a.song_num_stop, kBase + L.stop);
        CHECK_EQ(a.mplay_all_stop, kBase + L.all);
        CHECK_EQ(a.mplay_start, kMPlayStart);
        CHECK_EQ(a.mplay_stop, kMPlayStop);
        CHECK_EQ(a.mplay_table, kMPlayTable);
        CHECK_EQ(a.song_table, kSongTable);
        CHECK_EQ(a.song_table_entries, 5u);
    }
    {   // Stop and Continue have the same shape; the callee decides, whatever their order
        Rom r(0x10000);
        const Layout L = build(r, 0x1000, 0, /*swap_stop_callee=*/true);
        const M4aAddresses a = locate_m4a(r.d.data(), r.d.size());
        CHECK(a.complete());
        CHECK_EQ(a.song_num_stop, kBase + L.cont2);
    }
    {   // a ROM mapped elsewhere (other base) reports addresses in that space
        Rom r(0x10000);
        build(r, 0x1000, 0);
        const M4aAddresses a = locate_m4a(r.d.data(), r.d.size(), kBase);
        CHECK(a.complete());
    }
    {   // no engine / damaged group
        Rom empty(0x10000);
        CHECK(!locate_m4a(empty.d.data(), empty.d.size()).complete());
        CHECK(!locate_m4a(nullptr, 0).complete());
        Rom r(0x10000);
        const Layout L = build(r, 0x1000, 0);
        r.h(L.all, 0x0000);   // AllStop shape broken
        CHECK(!locate_m4a(r.d.data(), r.d.size()).complete());
        Rom r2(0x10000);
        const Layout L2 = build(r2, 0x1000, 0);
        r2.bl(L2.change + 36, kBase + 0x7300u);   // OrChange calls a different routine
        CHECK(!locate_m4a(r2.d.data(), r2.d.size()).complete());
        Rom r3(0x10000);
        const Layout L3 = build(r3, 0x1000, 0);
        r3.bl(L3.stop + 34, kBase + 0x7300u);     // no stop-shaped function calls AllStop's callee
        CHECK(!locate_m4a(r3.d.data(), r3.d.size()).complete());
    }
    {   // ident counting
        Rom r(0x100);
        std::memcpy(&r.d[0x10], "Smsh", 4);
        std::memcpy(&r.d[0x20], "Smsh", 4);
        std::memcpy(&r.d[0x31], "Smsh", 4);   // unaligned: not a literal
        CHECK_EQ(count_m4a_idents(r.d.data(), r.d.size()), size_t(2));
    }

    // --- policy ----------------------------------------------------------------------------------
    Rom rom(0x10000);
    build(rom, 0x1000, 0);
    FakeSink sink;
    sink.tracks[1] = true;    // mapped, file plays
    sink.tracks[3] = true;
    sink.tracks[4] = false;   // mapped, file unavailable
    M4aMusicHook hook;
    hook.set_sink(&sink);
    CHECK(hook.locate(rom.d.data(), rom.d.size()));
    hook.set_rom_image(rom.d.data(), rom.d.size());
    const M4aAddresses a = hook.addresses();

    hook.load_track_mapping("x/music_tracks.ini");
    CHECK_EQ(sink.loads, 1);
    CHECK_EQ(sink.last_ini, std::string("x/music_tracks.ini"));

    hook.on_function_entry(0x08000010u, 1);          // not an m4a entry
    hook.on_function_entry(a.song_num_start + 2, 1); // inside a function, not its entry
    CHECK_EQ(hook.stats().starts, uint64_t(0));

    hook.on_function_entry(a.song_num_start, 1);     // song 1 -> player 2, mapped
    CHECK_EQ(sink.triggers, 1);
    CHECK_EQ(sink.cur, 1u);
    CHECK_EQ(hook.stats().mapped_triggers, uint64_t(1));

    hook.on_function_entry(a.song_num_start_or_change, 0x20000 | 2);   // upper half masked: song 2 -> player 7, unmapped
    CHECK_EQ(sink.stops, 0);                         // other player: HD track untouched
    CHECK_EQ(sink.cur, 1u);

    hook.on_function_entry(a.song_num_start_or_continue, 0);   // song 0 -> player 7, unmapped, still other player
    CHECK_EQ(sink.stops, 0);

    hook.on_function_entry(a.song_num_start, 3);     // song 3 -> player 2, mapped, replaces HD track
    CHECK_EQ(sink.cur, 3u);

    hook.on_song_start(99, 2);                       // unmapped song on the HD player -> stop
    CHECK_EQ(sink.stops, 1);
    CHECK_EQ(sink.cur, 0u);
    hook.on_song_start(99, 2);                       // nothing playing: no second stop
    CHECK_EQ(sink.stops, 1);

    hook.on_function_entry(a.song_num_start, 1);
    CHECK_EQ(sink.cur, 1u);
    hook.on_function_entry(a.song_num_stop, 5);      // stop of another song: ignored
    CHECK_EQ(sink.cur, 1u);
    hook.on_function_entry(a.song_num_stop, 1);      // stop of the HD song
    CHECK_EQ(sink.cur, 0u);
    CHECK_EQ(sink.stops, 2);

    hook.on_function_entry(a.song_num_start, 1);
    hook.on_function_entry(a.mplay_all_stop, 0);
    CHECK_EQ(sink.cur, 0u);
    CHECK_EQ(sink.stops, 3);
    hook.on_function_entry(a.mplay_all_stop, 0);     // already stopped
    CHECK_EQ(sink.stops, 3);

    // mapped song whose file cannot load: silent, not recorded as the HD player
    hook.on_function_entry(a.song_num_start, 4);
    CHECK_EQ(sink.cur, 0u);
    hook.on_song_start(99, 2);
    CHECK_EQ(sink.stops, 3);

    // disabled HD audio: starts are counted, nothing is triggered
    sink.on = false;
    const int trig = sink.triggers;
    hook.on_function_entry(a.song_num_start, 1);
    CHECK_EQ(sink.triggers, trig);
    sink.on = true;

    const M4aHookStats st = hook.stats();
    CHECK(st.starts >= 8);
    CHECK_EQ(st.hd_stops, uint64_t(3));
    hook.reset_stats_and_state();
    CHECK_EQ(hook.stats().starts, uint64_t(0));

    // --- song player lookup: custom reader > bus > ROM image; out of range ---------------------
    {
        M4aMusicHook h;
        FakeSink s;
        s.tracks[1] = true;
        h.set_sink(&s);
        M4aAddresses m = a;
        h.set_addresses(m);
        h.set_bus_reader(&fake_bus);
        h.on_function_entry(a.song_num_start, 1);    // bus: song 1 -> player 2
        h.on_song_start(50, 2);                      // sanity: same player as the track
        CHECK_EQ(s.cur, 0u);
        h.on_function_entry(a.song_num_start, 1);
        h.on_function_entry(a.song_num_start, 7);    // id >= entries: player 0xFFFFFFFF, other player
        CHECK_EQ(s.cur, 1u);
        h.set_song_player_reader([](uint32_t) { return 2u; });
        h.on_function_entry(a.song_num_start, 6);    // custom reader says player 2 -> replaces HD
        CHECK_EQ(s.cur, 0u);

        M4aMusicHook h2;                             // table address known only by hand, no reader
        FakeSink s2;
        s2.tracks[1] = true;
        h2.set_sink(&s2);
        M4aAddresses manual;
        manual.song_num_start = 0x08001000u;         // no table: player unknown, never matches a started HD player
        h2.set_addresses(manual);
        h2.on_function_entry(0x08001000u, 1);
        CHECK_EQ(s2.cur, 1u);
        h2.on_function_entry(0x08001000u, 9);
        CHECK_EQ(s2.cur, 0u);                        // 0xFFFFFFFF == recorded player: documented parity
    }

    // --- default sink adapts the shared HdAudioPlayer without touching files ------------------------
    {
        testh::TempDir tmp("m4a");
        tmp.enter();
        M4aMusicHook h;
        h.set_addresses(a);
        h.set_rom_image(rom.d.data(), rom.d.size());
        h.on_function_entry(a.song_num_start, 1);    // no mapping loaded: native only
        CHECK_EQ(h.stats().starts, uint64_t(1));
        CHECK_EQ(h.stats().mapped_triggers, uint64_t(0));
        h.on_function_entry(a.mplay_all_stop, 0);
        CHECK_EQ(h.stats().hd_stops, uint64_t(0));
    }

    return testh::finish("m4a_music_hook");
}
