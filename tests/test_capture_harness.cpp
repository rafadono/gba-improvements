#define SDL_MAIN_HANDLED  // the test provides plain main(); no SDL_main redirection
#include "test_harness.h"
#include "gbaimp/capture_harness.h"

#include <SDL.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace gbaimp::capture;

namespace {

// --- fake environment ------------------------------------------------------------------
std::map<std::string, std::string>& fake_env() {
    static std::map<std::string, std::string> m;
    return m;
}
const char* fake_getenv(const char* name) {
    auto it = fake_env().find(name);
    return it == fake_env().end() ? nullptr : it->second.c_str();
}
void fake_setenv(const char* name, const char* value) { fake_env()[name] = value; }

int g_set_calls = 0;
std::vector<std::pair<std::string, int>> g_set_log;
int on_setting(const char* k, int v) {
    ++g_set_calls;
    g_set_log.emplace_back(k, v);
    return 1;
}

void cfg_with(const char* prefix) {
    CaptureConfig c;
    c.env_prefix = prefix;
    c.log_tag = "t";
    c.apply_setting = on_setting;
    c.getenv_fn = fake_getenv;
    c.setenv_fn = fake_setenv;
    configure(c);
}

// --- minimal PNG decoder (stored deflate only, filter 0) ---------------------------------
std::uint32_t crc32(const std::uint8_t* p, size_t n) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : (crc >> 1);
    }
    return ~crc;
}
std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

struct Decoded {
    bool ok = false;
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgb;
};

Decoded decode_png(const std::vector<std::uint8_t>& d) {
    Decoded r;
    static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (d.size() < 8 || std::memcmp(d.data(), sig, 8) != 0) return r;
    size_t pos = 8;
    std::vector<std::uint8_t> idat;
    bool iend = false;
    while (pos + 12 <= d.size()) {
        const std::uint32_t n = be32(&d[pos]);
        if (pos + 12 + n > d.size()) return r;
        if (crc32(&d[pos + 4], 4 + n) != be32(&d[pos + 8 + n])) return r;  // CRC covers type + data
        const std::string tag(reinterpret_cast<const char*>(&d[pos + 4]), 4);
        if (tag == "IHDR") {
            if (n != 13 || d[pos + 16] != 8 || d[pos + 17] != 2) return r;
            r.w = static_cast<int>(be32(&d[pos + 8]));
            r.h = static_cast<int>(be32(&d[pos + 12]));
        } else if (tag == "IDAT") {
            idat.insert(idat.end(), d.begin() + pos + 8, d.begin() + pos + 8 + n);
        } else if (tag == "IEND") {
            iend = true;
        }
        pos += 12 + n;
    }
    if (!iend || idat.size() < 6 || idat[0] != 0x78) return r;
    std::vector<std::uint8_t> raw;
    size_t z = 2;
    for (;;) {
        if (z + 5 > idat.size()) return r;
        const bool last = idat[z] & 1;
        if ((idat[z] >> 1) != 0) return r;  // stored blocks only
        const unsigned len = idat[z + 1] | (idat[z + 2] << 8);
        const unsigned nlen = idat[z + 3] | (idat[z + 4] << 8);
        if ((len ^ 0xFFFFu) != nlen || z + 5 + len > idat.size()) return r;
        raw.insert(raw.end(), idat.begin() + z + 5, idat.begin() + z + 5 + len);
        z += 5 + len;
        if (last) break;
    }
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    if (z + 4 != idat.size() || be32(&idat[z]) != ((b << 16) | a)) return r;
    const size_t stride = static_cast<size_t>(r.w) * 3 + 1;
    if (raw.size() != stride * static_cast<size_t>(r.h)) return r;
    for (int y = 0; y < r.h; ++y) {
        if (raw[y * stride] != 0) return r;
        r.rgb.insert(r.rgb.end(), raw.begin() + y * stride + 1, raw.begin() + (y + 1) * stride);
    }
    r.ok = true;
    return r;
}

std::vector<std::uint8_t> read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

// --- tests -------------------------------------------------------------------------------
void test_png() {
    // 3x2 with a padded pitch (12 bytes per row, 9 used).
    std::uint8_t px[24];
    for (int i = 0; i < 24; ++i) px[i] = static_cast<std::uint8_t>(i * 11 + 3);
    auto png = encode_png_rgb(px, 3, 2, 12);
    Decoded d = decode_png(png);
    CHECK(d.ok);
    CHECK_EQ(d.w, 3);
    CHECK_EQ(d.h, 2);
    bool same = d.ok;
    for (int y = 0; y < 2 && same; ++y)
        for (int x = 0; x < 9; ++x) same = same && d.rgb[y * 9 + x] == px[y * 12 + x];
    CHECK(same);

    // Large image: more than one 65535-byte stored block.
    const int W = 240, H = 160;
    std::vector<std::uint8_t> big(W * H * 3);
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<std::uint8_t>((i * 7) ^ (i >> 8));
    Decoded b = decode_png(encode_png_rgb(big.data(), W, H, W * 3));
    CHECK(b.ok);
    CHECK(b.rgb == big);

    // Invalid arguments.
    CHECK(encode_png_rgb(nullptr, 3, 2, 9).empty());
    CHECK(encode_png_rgb(px, 0, 2, 9).empty());
    CHECK(encode_png_rgb(px, 3, 2, 8).empty());  // pitch smaller than a row
    testh::TempDir tmp("png");
    CHECK(write_png_rgb(tmp.file("a.png"), px, 3, 2, 12));
    CHECK(decode_png(read_file(tmp.file("a.png"))).ok);
    CHECK(!write_png_rgb(tmp.file("no/such/dir/a.png"), px, 3, 2, 12));

    // A corrupted byte is caught by the CRC.
    png[png.size() / 2] ^= 0x5A;
    CHECK(!decode_png(png).ok);
}

void test_parsers() {
    CHECK_EQ(frame_label(300), std::string("f000300"));
    auto fr = parse_frame_list("300,600-602, 2000-2100/50,300");
    CHECK((fr == std::vector<std::uint64_t>{300, 600, 601, 602, 2000, 2050, 2100}));
    CHECK(parse_frame_list("").empty());

    auto sets = parse_settings("screen_mask=0; mask_intensity = 60,bad;=3;k=");
    CHECK_EQ(sets.size(), 2u);
    CHECK(sets[0].first == "screen_mask" && sets[0].second == 0);
    CHECK(sets[1].first == "mask_intensity" && sets[1].second == 60);

    int w = 0, h = 0;
    CHECK(parse_size("852x480", &w, &h) && w == 852 && h == 480);
    CHECK(!parse_size("852", &w, &h));
    CHECK(!parse_size("0x480", &w, &h));

    auto vars = parse_variants("852x480 screen_mask=0|960x640| |1536x864 screen_mask=1 mask_intensity=60");
    CHECK_EQ(vars.size(), 3u);
    CHECK(vars[0].w == 852 && vars[0].h == 480 && vars[0].sets.size() == 1);
    CHECK(vars[1].w == 960 && vars[1].sets.empty());
    CHECK(vars[2].sets.size() == 2 && vars[2].sets[1].second == 60);
    CHECK_EQ(sanitize_variant("852x480 screen_mask=0"), std::string("852x480_screen_mask_0"));
    CHECK_EQ(sanitize_variant("a,b*c"), std::string("a_bc"));
}

void test_input_rules() {
    unsigned m = 0;
    CHECK(parse_button("start", &m) && m == kKeyStart);
    CHECK(parse_button(" Up ", &m) && m == (kKeyStart | kKeyUp));
    CHECK(parse_button("none", &m) && m == (kKeyStart | kKeyUp));
    CHECK(!parse_button("turbo", &m));

    auto rules = parse_input_script(
        "120-125: A            # comment\n"
        "200: START;\n"
        "1500-1700 every 100 hold 3: A+B\n"
        "300-310 every 10: UP\n"
        "no colon here\n"
        "400: BOGUS\n");
    CHECK_EQ(rules.size(), 4u);
    CHECK(rules[0].first == 120 && rules[0].last == 125 && rules[0].mask == kKeyA);
    CHECK(rules[1].first == 200 && rules[1].last == 201);
    CHECK(rules[2].period == 100 && rules[2].hold == 3 && rules[2].mask == (kKeyA | kKeyB));
    CHECK(rules[3].hold == 2);  // "every" without "hold" defaults to 2

    CHECK_EQ(scripted_mask(rules, 119), 0u);
    CHECK_EQ(scripted_mask(rules, 120), kKeyA);
    CHECK_EQ(scripted_mask(rules, 125), kKeyA);
    CHECK_EQ(scripted_mask(rules, 126), 0u);
    CHECK_EQ(scripted_mask(rules, 200), kKeyStart);
    CHECK_EQ(scripted_mask(rules, 201), kKeyStart);   // single frame held for two frames
    CHECK_EQ(scripted_mask(rules, 202), 0u);
    CHECK_EQ(scripted_mask(rules, 1500), kKeyA | kKeyB);
    CHECK_EQ(scripted_mask(rules, 1502), kKeyA | kKeyB);
    CHECK_EQ(scripted_mask(rules, 1503), 0u);
    CHECK_EQ(scripted_mask(rules, 1600), kKeyA | kKeyB);
    CHECK_EQ(scripted_mask(rules, 301), kKeyUp);
    CHECK_EQ(scripted_mask(rules, 302), 0u);

    // Trace: active low, only transitions, starts at frame 0 all released (0x3ff).
    const std::string tr = build_input_trace(parse_input_script("5-6: A"), "X_INPUT_SCRIPT");
    CHECK(tr.find("# gbarecomp-keyinput-v1\n") == 0);
    CHECK(tr.find("generated from X_INPUT_SCRIPT") != std::string::npos);
    CHECK(tr.find("\n0,0x3ff\n") != std::string::npos);
    CHECK(tr.find("\n5,0x3fe\n") != std::string::npos);
    CHECK(tr.find("\n7,0x3ff\n") != std::string::npos);
    CHECK(tr.find("\n6,") == std::string::npos);
}

void test_env_prefix() {
    // Harness off: nothing set under the chosen prefix (a variable under another prefix is ignored).
    fake_env().clear();
    fake_env()["OTHER_CAPTURE_FRAMES"] = "10";
    cfg_with("ZZ");
    CHECK(!active());
    CHECK(extra_runtime_args().empty());
    CHECK(settings_overrides().empty());
    CHECK(!wants_native_frame());
    on_emulated_frame(1000);  // inert

    // Arbitrary prefix, with and without a trailing underscore.
    for (const char* prefix : {"ABC", "ABC_"}) {
        fake_env().clear();
        fake_env()["ABC_SET"] = "screen_mask=0;perf_mode=1";
        fake_env()["ABC_SAVESTATE_LOAD"] = "state.bin";
        fake_env()["ABC_SAVESTATE_SAVE"] = "500:3";
        fake_env()["ABC_FAST"] = "1";
        cfg_with(prefix);
        CHECK(active());
        CHECK_EQ(settings_overrides().size(), 2u);
        auto args = extra_runtime_args();
        CHECK(args.size() == 2 && args[0] == "--load-state" && args[1] == "state.bin");
        CHECK_EQ(std::string(fake_env()["GBARECOMP_ASSIST_SCRIPT"]), std::string("1:fast_on;500:save3;501:fast_off"));
    }

    // Savestate save without fast: minimum pump 2, slot defaults to 1; existing script is kept.
    fake_env().clear();
    fake_env()["ABC_SAVESTATE_SAVE"] = "0";
    fake_env()["GBARECOMP_ASSIST_SCRIPT"] = "9:foo";
    cfg_with("ABC");
    extra_runtime_args();
    CHECK_EQ(std::string(fake_env()["GBARECOMP_ASSIST_SCRIPT"]), std::string("9:foo;2:save1"));

    // Input script -> trace file + GBARECOMP_INPUT_REPLAY, written inside the capture dir.
    testh::TempDir tmp("env");
    fake_env().clear();
    fake_env()["ABC_CAPTURE_DIR"] = tmp.file("cap");
    fake_env()["ABC_INPUT_SCRIPT"] = "10-12: A";
    cfg_with("ABC");
    extra_runtime_args();
    const std::string path = fake_env()["GBARECOMP_INPUT_REPLAY"];
    CHECK_EQ(path, tmp.file("cap") + "/input_trace.txt");
    const auto tr = read_file(path);
    CHECK(!tr.empty());
    CHECK(std::string(tr.begin(), tr.end()).find("ABC_INPUT_SCRIPT") != std::string::npos);
    // An existing GBARECOMP_INPUT_REPLAY is respected.
    fake_env()["GBARECOMP_INPUT_REPLAY"] = "mine.txt";
    extra_runtime_args();
    CHECK_EQ(std::string(fake_env()["GBARECOMP_INPUT_REPLAY"]), std::string("mine.txt"));

    // Script given as a file.
    {
        std::ofstream f(tmp.file("s.txt"));
        f << "7: START\n";
    }
    fake_env().clear();
    fake_env()["ABC_INPUT_SCRIPT"] = tmp.file("s.txt");
    cfg_with("ABC");
    CHECK(active());

    // Default trace path (no capture dir) uses the lower-case prefix.
    fake_env().clear();
    fake_env()["MyPre_INPUT_SCRIPT"] = "1: A";
    cfg_with("MyPre");
    tmp.enter();
    extra_runtime_args();
    CHECK_EQ(std::string(fake_env()["GBARECOMP_INPUT_REPLAY"]), std::string("mypre_input_trace.txt"));
    CHECK(!read_file(tmp.file("mypre_input_trace.txt")).empty());
}

void test_capture_arming() {
    testh::TempDir tmp("arm");
    fake_env().clear();
    fake_env()["QQ_CAPTURE_DIR"] = tmp.file("out");
    fake_env()["QQ_CAPTURE_FRAMES"] = "5,8";
    fake_env()["QQ_CAPTURE_MODE"] = "both";
    fake_env()["QQ_EXIT_AFTER_CAPTURE"] = "1";
    cfg_with("QQ");
    CHECK(active());
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);

    SDL_Window* win = SDL_CreateWindow("t", 0, 0, 64, 48, SDL_WINDOW_HIDDEN);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    CHECK(win && ren);
    if (!ren) { SDL_Quit(); return; }

    std::vector<std::uint8_t> native(240 * 160 * 3, 0x40);
    CHECK(!wants_native_frame());
    on_emulated_frame(4);                       // nothing armed yet
    CHECK(!wants_native_frame());
    on_before_present(ren);
    CHECK(!std::filesystem::exists(tmp.file("out")));

    on_emulated_frame(6);                       // frame 5 armed (6 >= 5), 8 not yet
    CHECK(wants_native_frame());
    on_native_frame(native.data(), 240 * 3, 240, 160);
    CHECK(!wants_native_frame());
    CHECK(std::filesystem::exists(tmp.file("out") + "/f000005_native_240x160.png"));
    SDL_SetRenderDrawColor(ren, 10, 200, 30, 255);
    SDL_RenderClear(ren);
    on_before_present(ren);
    const std::string outp = tmp.file("out") + "/f000005_output_64x48.png";
    Decoded o = decode_png(read_file(outp));
    CHECK(o.ok && o.w == 64 && o.h == 48);
    CHECK(o.ok && o.rgb[0] == 10 && o.rgb[1] == 200 && o.rgb[2] == 30);

    SDL_Event ev;
    SDL_PumpEvents();
    CHECK(SDL_PeepEvents(&ev, 1, SDL_PEEKEVENT, SDL_QUIT, SDL_QUIT) == 0);  // frame 8 still outstanding
    on_emulated_frame(9);
    on_native_frame(native.data(), 240 * 3, 240, 160);
    SDL_PumpEvents();
    CHECK(SDL_PeepEvents(&ev, 1, SDL_PEEKEVENT, SDL_QUIT, SDL_QUIT) == 0);  // output still pending
    on_before_present(ren);
    CHECK(std::filesystem::exists(tmp.file("out") + "/f000008_output_64x48.png"));
    SDL_PumpEvents();
    CHECK(SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) == 1);   // all written -> quit
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

void test_exit_frame_and_variants() {
    testh::TempDir tmp("var");
    fake_env().clear();
    fake_env()["VV_EXIT_FRAME"] = "20";
    fake_env()["VV_CAPTURE_DIR"] = tmp.file("v");
    fake_env()["VV_CAPTURE_FRAMES"] = "3";
    fake_env()["VV_CAPTURE_VARIANTS"] = "80x60 screen_mask=0|key=7";
    g_set_calls = 0;
    g_set_log.clear();
    cfg_with("VV");
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    SDL_Window* win = SDL_CreateWindow("t", 0, 0, 64, 48, SDL_WINDOW_HIDDEN);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    CHECK(win && ren);
    if (!ren) { SDL_Quit(); return; }

    on_emulated_frame(3);
    SDL_Event ev;
    SDL_PumpEvents();
    CHECK(SDL_PeepEvents(&ev, 1, SDL_PEEKEVENT, SDL_QUIT, SDL_QUIT) == 0);
    on_emulated_frame(20);   // exit frame reached
    SDL_PumpEvents();
    CHECK(SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) == 1);
    on_emulated_frame(21);   // only once
    SDL_PumpEvents();
    CHECK(SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) == 0);

    // Drive the variant state machine until both images exist.
    for (int i = 0; i < 400; ++i) on_before_present(ren);
    CHECK(g_set_calls == 2);
    CHECK(g_set_log.size() == 2 && g_set_log[0].first == "screen_mask" && g_set_log[1].first == "key" &&
          g_set_log[1].second == 7);
    namespace fs = std::filesystem;
    int pngs = 0;
    if (fs::exists(tmp.file("v"))) {
        for (auto& e : fs::directory_iterator(tmp.file("v"))) {
            if (e.path().extension() == ".png") ++pngs;
        }
    }
    CHECK_EQ(pngs, 2);
    CHECK(fs::exists(tmp.file("v") + "/f000003_output_key_7.png"));
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

}  // namespace

int main() {
    SDL_SetMainReady();
    test_png();
    test_parsers();
    test_input_rules();
    test_env_prefix();
    test_capture_arming();
    test_exit_frame_and_variants();
    return testh::finish("capture_harness");
}
