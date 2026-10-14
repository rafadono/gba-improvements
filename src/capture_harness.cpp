// Headless capture + scripted-input harness. See capture_harness.h and docs/CAPTURE_HARNESS.md.
#include "gbaimp/capture_harness.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace gbaimp {
namespace capture {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

constexpr std::uint64_t kNone = ~0ull;

struct State {
    CaptureConfig cfg;
    std::string log_tag = "capture";
    bool any = false;

    // capture
    std::string dir;
    std::vector<std::uint64_t> frames;  // sorted targets
    std::size_t next_target = 0;
    bool want_output = true, want_native = false;
    bool exit_after_capture = false;
    std::uint64_t exit_frame = 0;
    bool have_exit_frame = false;

    // presentation
    int win_w = 0, win_h = 0;
    int win_applied_presents = 0;

    // input
    std::vector<InputRule> rules;
    std::string input_var;  // name of the input script variable, for the trace header

    // host key presses ("<frame>:<key name>;..."): SDL key events pushed at an emulated frame
    std::vector<std::pair<std::uint64_t, int>> push_keys;  // (frame, SDL_Keycode)
    std::size_t push_key_idx = 0;

    // variants
    std::vector<Variant> variants;
    std::size_t var_idx = 0;
    int var_phase = 0, var_settle = 0;

    // settings / savestate
    std::vector<std::pair<std::string, int>> sets;
    std::string savestate_load, savestate_save, fast;

    // runtime
    std::atomic<std::uint64_t> frame{0};
    std::atomic<std::uint64_t> pending_output_label{kNone};
    std::atomic<std::uint64_t> pending_native_label{kNone};
    std::atomic<bool> quit_requested{false};
    std::atomic<int> outstanding{0};  // captures armed but not yet written
    std::vector<std::uint8_t> buf;
};

State g_state;
std::mutex g_cfg_mutex;
std::atomic<bool> g_configured{false};

const char* getenv_of(const State& s, const std::string& name) {
    const char* v = s.cfg.getenv_fn ? s.cfg.getenv_fn(name.c_str()) : std::getenv(name.c_str());
    return (v && *v) ? v : nullptr;
}

void setenv_of(const State& s, const char* name, const char* value) {
    if (s.cfg.setenv_fn) {
        s.cfg.setenv_fn(name, value);
        return;
    }
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

std::string var_name(const State& s, const char* suffix) {
    std::string p = s.cfg.env_prefix ? s.cfg.env_prefix : "";
    if (!p.empty() && p.back() != '_') p.push_back('_');
    return p + suffix;
}

const char* env(const State& s, const char* suffix) { return getenv_of(s, var_name(s, suffix)); }

void make_dir(const std::string& d) {
    if (d.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(d), ec);
}

void init_from_env(State& s) {
    s.log_tag = s.cfg.log_tag ? s.cfg.log_tag : "capture";
    const char* tag = s.log_tag.c_str();
    if (const char* d = env(s, "CAPTURE_DIR")) s.dir = d;
    if (const char* f = env(s, "CAPTURE_FRAMES")) s.frames = parse_frame_list(f);
    if (const char* m = env(s, "CAPTURE_MODE")) {
        const std::string mode = upper(m);
        s.want_output = (mode == "OUTPUT" || mode == "BOTH");
        s.want_native = (mode == "NATIVE" || mode == "BOTH");
        if (!s.want_output && !s.want_native) s.want_output = true;
    }
    if (const char* x = env(s, "EXIT_AFTER_CAPTURE")) s.exit_after_capture = std::strcmp(x, "0") != 0;
    if (const char* x = env(s, "EXIT_FRAME")) {
        s.exit_frame = std::strtoull(x, nullptr, 10);
        s.have_exit_frame = true;
    }
    if (const char* w = env(s, "WINDOW_SIZE")) {
        if (!parse_size(w, &s.win_w, &s.win_h)) {
            s.win_w = s.win_h = 0;
            std::fprintf(stderr, "[%s] bad %s \"%s\" (expected WxH)\n", tag,
                         var_name(s, "WINDOW_SIZE").c_str(), w);
        }
    }
    s.input_var = var_name(s, "INPUT_SCRIPT");
    if (const char* sc = env(s, "INPUT_SCRIPT")) {
        std::string text = sc;
        std::ifstream f(sc, std::ios::binary);
        if (f.is_open()) {
            std::stringstream ss;
            ss << f.rdbuf();
            text = ss.str();
        }
        s.rules = parse_input_script(text);
    }
    if (const char* k = env(s, "SET")) s.sets = parse_settings(k);
    if (const char* pk = env(s, "PUSH_KEYS")) {
        std::string text = pk;
        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t end = text.find(';', pos);
            if (end == std::string::npos) end = text.size();
            const std::string item = text.substr(pos, end - pos);
            pos = end + 1;
            const std::size_t colon = item.find(':');
            if (colon == std::string::npos) continue;
            const std::uint64_t fr = std::strtoull(item.substr(0, colon).c_str(), nullptr, 10);
            const SDL_Keycode kc = SDL_GetKeyFromName(trim(item.substr(colon + 1)).c_str());
            if (kc == SDLK_UNKNOWN) {
                std::fprintf(stderr, "[%s] bad key name in %s: %s\n", tag,
                             var_name(s, "PUSH_KEYS").c_str(), item.c_str());
                continue;
            }
            s.push_keys.emplace_back(fr, static_cast<int>(kc));
        }
        std::sort(s.push_keys.begin(), s.push_keys.end());
    }
    if (const char* v = env(s, "CAPTURE_VARIANTS")) s.variants = parse_variants(v);
    if (const char* p = env(s, "SAVESTATE_LOAD")) s.savestate_load = p;
    if (const char* p = env(s, "SAVESTATE_SAVE")) s.savestate_save = p;
    if (const char* p = env(s, "FAST")) s.fast = p;
    if (!s.frames.empty() && s.dir.empty()) s.dir = "captures";
    s.any = !s.push_keys.empty() || !s.dir.empty() || !s.frames.empty() || !s.rules.empty() ||
            s.win_w > 0 || !s.sets.empty() || !s.savestate_load.empty() || !s.savestate_save.empty() ||
            s.have_exit_frame || !s.fast.empty();
    if (s.any) {
        std::fprintf(stderr,
                     "[%s] harness ON: dir=%s frames=%zu mode=%s%s window=%dx%d input_rules=%zu sets=%zu\n",
                     tag, s.dir.c_str(), s.frames.size(), s.want_output ? "output" : "",
                     s.want_native ? (s.want_output ? "+native" : "native") : "", s.win_w, s.win_h,
                     s.rules.size(), s.sets.size());
    }
}

State& ensure() {
    if (!g_configured.load(std::memory_order_acquire)) configure(CaptureConfig{});
    return g_state;
}

void push_quit() {
    SDL_Event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.type = SDL_QUIT;
    SDL_PushEvent(&ev);
}

void request_quit_if_done(State& s) {
    if (s.exit_after_capture && !s.frames.empty() && s.next_target >= s.frames.size() &&
        s.outstanding.load() == 0 && !s.quit_requested.exchange(true)) {
        std::fprintf(stderr, "[%s] all captures written, requesting quit\n", s.log_tag.c_str());
        push_quit();
    }
}

bool grab_output(State& s, SDL_Renderer* renderer, const std::string& path) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    if (w <= 0 || h <= 0) return false;
    s.buf.resize(static_cast<size_t>(w) * h * 3);
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24, s.buf.data(), w * 3) != 0) {
        std::fprintf(stderr, "[%s] SDL_RenderReadPixels failed: %s\n", s.log_tag.c_str(), SDL_GetError());
        return false;
    }
    const bool ok = write_png_rgb(path, s.buf.data(), w, h, w * 3);
    std::fprintf(stderr, "[%s] %s %s\n", s.log_tag.c_str(), ok ? "wrote" : "FAILED", path.c_str());
    return ok;
}

}  // namespace

// ---------------------------------------------------------------------------------------
// Pure helpers

bool parse_button(const std::string& tok, unsigned* mask) {
    const std::string t = upper(trim(tok));
    if (t == "A") *mask |= kKeyA;
    else if (t == "B") *mask |= kKeyB;
    else if (t == "L") *mask |= kKeyL;
    else if (t == "R") *mask |= kKeyR;
    else if (t == "START") *mask |= kKeyStart;
    else if (t == "SELECT") *mask |= kKeySelect;
    else if (t == "UP") *mask |= kKeyUp;
    else if (t == "DOWN") *mask |= kKeyDown;
    else if (t == "LEFT") *mask |= kKeyLeft;
    else if (t == "RIGHT") *mask |= kKeyRight;
    else if (t.empty() || t == "NONE") {}
    else return false;
    return true;
}

namespace {
void parse_script_entry(const std::string& raw, std::vector<InputRule>* out) {
    std::string e = raw;
    const size_t hash = e.find('#');
    if (hash != std::string::npos) e.resize(hash);
    e = trim(e);
    if (e.empty()) return;
    const size_t colon = e.find(':');
    if (colon == std::string::npos) {
        std::fprintf(stderr, "[capture] input script: missing ':' in \"%s\"\n", e.c_str());
        return;
    }
    InputRule r;
    std::istringstream left(e.substr(0, colon));
    std::string range;
    left >> range;
    const size_t dash = range.find('-');
    r.first = std::strtoull(range.c_str(), nullptr, 10);
    if (dash != std::string::npos) r.last = std::strtoull(range.c_str() + dash + 1, nullptr, 10);
    else r.last = r.first + 1;
    std::string kw;
    while (left >> kw) {
        std::string k = upper(kw);
        std::string val;
        if (!(left >> val)) break;
        if (k == "EVERY") r.period = std::strtoull(val.c_str(), nullptr, 10);
        else if (k == "HOLD") r.hold = std::strtoull(val.c_str(), nullptr, 10);
    }
    if (r.period && !r.hold) r.hold = 2;
    std::istringstream bs(e.substr(colon + 1));
    std::string b;
    while (std::getline(bs, b, '+')) {
        if (!parse_button(b, &r.mask)) {
            std::fprintf(stderr, "[capture] input script: unknown button \"%s\"\n", trim(b).c_str());
            return;
        }
    }
    out->push_back(r);
}
}  // namespace

std::vector<InputRule> parse_input_script(const std::string& text) {
    std::vector<InputRule> out;
    std::string cur;
    for (char c : text) {
        if (c == '\n' || c == '\r' || c == ';') {
            parse_script_entry(cur, &out);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parse_script_entry(cur, &out);
    return out;
}

unsigned scripted_mask(const std::vector<InputRule>& rules, std::uint64_t f) {
    unsigned m = 0;
    for (const InputRule& r : rules) {
        if (f < r.first || f > r.last) continue;
        if (r.period) {
            if (((f - r.first) % r.period) < r.hold) m |= r.mask;
        } else {
            m |= r.mask;  // single-frame entries were expanded to [N, N+1]
        }
    }
    return m;
}

std::string build_input_trace(const std::vector<InputRule>& rules, const char* origin_name) {
    std::uint64_t maxf = 0;
    for (const InputRule& r : rules) maxf = std::max(maxf, r.last);
    std::string out = "# gbarecomp-keyinput-v1\n# frame,keyinput_active_low (generated from ";
    out += origin_name ? origin_name : "input script";
    out += ")\n";
    unsigned prev = 0xFFFFu;
    char line[48];
    for (std::uint64_t f = 0; f <= maxf + 2; ++f) {
        const unsigned v = 0x03FFu & ~scripted_mask(rules, f);
        if (f == 0 || v != prev) {
            std::snprintf(line, sizeof(line), "%llu,0x%03x\n", static_cast<unsigned long long>(f), v);
            out += line;
            prev = v;
        }
    }
    return out;
}

std::vector<std::uint64_t> parse_frame_list(const std::string& text) {
    std::vector<std::uint64_t> out;
    std::istringstream ss(text);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        tok = trim(tok);
        if (tok.empty()) continue;
        std::uint64_t step = 1;
        const size_t slash = tok.find('/');
        if (slash != std::string::npos) {
            step = std::max<std::uint64_t>(1, std::strtoull(tok.c_str() + slash + 1, nullptr, 10));
            tok.resize(slash);
        }
        const size_t dash = tok.find('-');
        const std::uint64_t a = std::strtoull(tok.c_str(), nullptr, 10);
        const std::uint64_t b = dash == std::string::npos ? a : std::strtoull(tok.c_str() + dash + 1, nullptr, 10);
        for (std::uint64_t f = a; f <= b && out.size() < 100000; f += step) out.push_back(f);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::pair<std::string, int>> parse_settings(const std::string& text) {
    std::vector<std::pair<std::string, int>> out;
    std::string cur;
    auto flush = [&] {
        const size_t eq = cur.find('=');
        if (eq != std::string::npos) {
            const std::string k = trim(cur.substr(0, eq));
            const std::string v = trim(cur.substr(eq + 1));
            if (!k.empty() && !v.empty()) out.emplace_back(k, std::atoi(v.c_str()));
        }
        cur.clear();
    };
    for (char c : text) {
        if (c == ';' || c == ',' || c == '\n' || c == '\r') flush();
        else cur.push_back(c);
    }
    flush();
    return out;
}

bool parse_size(const std::string& text, int* w, int* h) {
    int a = 0, b = 0;
    if (std::sscanf(text.c_str(), "%dx%d", &a, &b) == 2 && a > 0 && b > 0) {
        if (w) *w = a;
        if (h) *h = b;
        return true;
    }
    return false;
}

std::vector<Variant> parse_variants(const std::string& text) {
    std::vector<Variant> out;
    std::istringstream ss(text);
    std::string entry;
    while (std::getline(ss, entry, '|')) {
        entry = trim(entry);
        if (entry.empty()) continue;
        Variant v;
        v.spec = entry;
        std::istringstream es(entry);
        std::string tok;
        while (es >> tok) {
            const size_t eq = tok.find('=');
            if (eq != std::string::npos) v.sets.emplace_back(tok.substr(0, eq), std::atoi(tok.c_str() + eq + 1));
            else parse_size(tok, &v.w, &v.h);
        }
        out.push_back(v);
    }
    return out;
}

std::string frame_label(std::uint64_t f) {
    char b[32];
    std::snprintf(b, sizeof(b), "f%06llu", static_cast<unsigned long long>(f));
    return b;
}

std::string sanitize_variant(const std::string& in) {
    std::string o;
    for (char c : in) {
        if (std::isalnum(static_cast<unsigned char>(c))) o.push_back(c);
        else if (c == 'x' || c == '_') o.push_back(c);
        else if (c == ' ' || c == ',' || c == '=') o.push_back('_');
    }
    return o;
}

// ---------------------------------------------------------------------------------------
// Configuration and hooks

void configure(const CaptureConfig& config) {
    std::lock_guard<std::mutex> lock(g_cfg_mutex);
    State& s = g_state;
    s.cfg = config;
    s.any = false;
    s.dir.clear();
    s.frames.clear();
    s.next_target = 0;
    s.want_output = true;
    s.want_native = false;
    s.exit_after_capture = false;
    s.exit_frame = 0;
    s.have_exit_frame = false;
    s.win_w = s.win_h = 0;
    s.win_applied_presents = 0;
    s.rules.clear();
    s.push_keys.clear();
    s.push_key_idx = 0;
    s.variants.clear();
    s.var_idx = 0;
    s.var_phase = s.var_settle = 0;
    s.sets.clear();
    s.savestate_load.clear();
    s.savestate_save.clear();
    s.fast.clear();
    s.frame.store(0);
    s.pending_output_label.store(kNone);
    s.pending_native_label.store(kNone);
    s.quit_requested.store(false);
    s.outstanding.store(0);
    init_from_env(s);
    g_configured.store(true, std::memory_order_release);
}

bool active() { return ensure().any; }

const std::vector<std::pair<std::string, int>>& settings_overrides() { return ensure().sets; }

void set_setting_callback(int (*cb)(const char* key, int value)) {
    ensure().cfg.apply_setting = cb;
}

std::vector<std::string> extra_runtime_args() {
    State& s = ensure();
    std::vector<std::string> a;
    if (!s.savestate_load.empty()) {
        a.push_back("--load-state");
        a.push_back(s.savestate_load);
    }
    // Savestate saving and fast-forward reuse the runtime's GBARECOMP_ASSIST_SCRIPT
    // ("<pump>:<action>;..."). One pump == one emulated frame of the main loop.
    std::string script;
    const bool fast = !s.fast.empty() && s.fast != "0";
    if (fast) script += "1:fast_on";
    if (!s.savestate_save.empty()) {
        const size_t c = s.savestate_save.find(':');
        const unsigned long long frame = std::strtoull(s.savestate_save.c_str(), nullptr, 10);
        const int slot = c == std::string::npos ? 1 : std::atoi(s.savestate_save.c_str() + c + 1);
        const unsigned long long at = std::max<unsigned long long>(frame, 2);
        if (!script.empty()) script += ";";
        script += std::to_string(at) + ":save" + std::to_string(slot < 1 ? 1 : slot);
        if (fast) script += ";" + std::to_string(at + 1) + ":fast_off";
    }
    // Scripted input goes through the runtime's deterministic input-trace replay
    // (GBARECOMP_INPUT_REPLAY, frame-indexed by the PPU frame counter). Writing KEYINPUT from the
    // per-frame hook is not enough: the host pump overwrites it before the guest reads it.
    if (!s.rules.empty() && !getenv_of(s, "GBARECOMP_INPUT_REPLAY")) {
        std::uint64_t maxf = 0;
        for (const InputRule& r : s.rules) maxf = std::max(maxf, r.last);
        std::string prefix = s.cfg.env_prefix ? lower(s.cfg.env_prefix) : "gbaimp";
        while (!prefix.empty() && prefix.back() == '_') prefix.pop_back();
        std::string path = prefix + "_input_trace.txt";
        if (!s.dir.empty()) {
            make_dir(s.dir);
            path = s.dir + "/input_trace.txt";
        }
        const std::string trace = build_input_trace(s.rules, s.input_var.c_str());
        std::ofstream tr(path, std::ios::trunc | std::ios::binary);
        tr << trace;
        tr.close();
        setenv_of(s, "GBARECOMP_INPUT_REPLAY", path.c_str());
        std::fprintf(stderr, "[%s] input trace written to %s (%zu rules, last frame %llu)\n",
                     s.log_tag.c_str(), path.c_str(), s.rules.size(),
                     static_cast<unsigned long long>(maxf));
    }
    if (!script.empty()) {
        const char* prev = getenv_of(s, "GBARECOMP_ASSIST_SCRIPT");
        if (prev) script = std::string(prev) + ";" + script;
        setenv_of(s, "GBARECOMP_ASSIST_SCRIPT", script.c_str());
        std::fprintf(stderr, "[%s] GBARECOMP_ASSIST_SCRIPT=%s\n", s.log_tag.c_str(), script.c_str());
    }
    return a;
}

void on_emulated_frame(std::uint64_t hw_frame_count) {
    State& s = ensure();
    if (!s.any) return;
    // Frame numbers are the PPU frame counter of the runtime (0 at boot; a loaded savestate
    // restores its own value).
    const std::uint64_t f = hw_frame_count;
    s.frame.store(f);
    while (s.next_target < s.frames.size() && s.frames[s.next_target] <= f) {
        const std::uint64_t t = s.frames[s.next_target++];
        if (s.want_output) {
            s.pending_output_label.store(t);
            s.outstanding.fetch_add(1);
        }
        if (s.want_native) {
            s.pending_native_label.store(t);
            s.outstanding.fetch_add(1);
        }
    }
    while (s.push_key_idx < s.push_keys.size() && s.push_keys[s.push_key_idx].first <= f) {
        const SDL_Keycode kc = static_cast<SDL_Keycode>(s.push_keys[s.push_key_idx++].second);
        for (int down = 1; down >= 0; --down) {
            SDL_Event kev;
            std::memset(&kev, 0, sizeof(kev));
            kev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
            kev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
            kev.key.keysym.sym = kc;
            kev.key.keysym.scancode = SDL_GetScancodeFromKey(kc);
            SDL_PushEvent(&kev);
        }
        std::fprintf(stderr, "[%s] pushed key %s at frame %llu\n", s.log_tag.c_str(), SDL_GetKeyName(kc),
                     static_cast<unsigned long long>(f));
    }
    if (s.have_exit_frame && f >= s.exit_frame && !s.quit_requested.exchange(true)) {
        std::fprintf(stderr, "[%s] %s reached, requesting quit\n", s.log_tag.c_str(),
                     var_name(s, "EXIT_FRAME").c_str());
        push_quit();
    }
}

bool wants_native_frame() {
    State& s = ensure();
    return s.any && s.pending_native_label.load() != kNone;
}

void on_native_frame(const void* pixels, int pitch, int width, int height) {
    State& s = ensure();
    if (!s.any) return;
    const std::uint64_t t = s.pending_native_label.exchange(kNone);
    if (t == kNone) return;
    make_dir(s.dir);
    const std::string path = s.dir + "/" + frame_label(t) + "_native_" + std::to_string(width) + "x" +
                             std::to_string(height) + ".png";
    const bool ok = write_png_rgb(path, static_cast<const std::uint8_t*>(pixels), width, height, pitch);
    std::fprintf(stderr, "[%s] %s %s\n", s.log_tag.c_str(), ok ? "wrote" : "FAILED", path.c_str());
    s.outstanding.fetch_sub(1);
    request_quit_if_done(s);
}

void on_before_present(SDL_Renderer* renderer) {
    State& s = ensure();
    if (!s.any || !renderer) return;

    if (s.win_w > 0 && s.win_applied_presents >= 0) {
        SDL_Window* win = SDL_RenderGetWindow(renderer);
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(renderer, &ow, &oh);
        if (win && (ow != s.win_w || oh != s.win_h)) {
            // Re-assert for a while: the host may resize/relayout the window during startup.
            if (s.win_applied_presents < 60) {
                SDL_SetWindowSize(win, s.win_w, s.win_h);
                ++s.win_applied_presents;
            }
        } else if (win) {
            if (s.win_applied_presents > 0) {
                std::fprintf(stderr, "[%s] window/output size is now %dx%d\n", s.log_tag.c_str(), ow, oh);
            }
            s.win_applied_presents = -1;  // done
        }
    }

    const std::uint64_t t = s.pending_output_label.load();
    // Do not grab while the window size is still being applied.
    if (t == kNone || (s.win_w > 0 && s.win_applied_presents >= 0 && s.win_applied_presents < 60)) return;

    if (s.variants.empty()) {
        s.pending_output_label.store(kNone);
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        make_dir(s.dir);
        grab_output(s, renderer, s.dir + "/" + frame_label(t) + "_output_" + std::to_string(w) + "x" + std::to_string(h) + ".png");
        s.outstanding.fetch_sub(1);
        request_quit_if_done(s);
        return;
    }

    // Variants: one output image per "WxH [key=value ...]" entry, taken at the same emulated
    // frame (the page is static) while the window size / settings are changed live.
    Variant& v = s.variants[s.var_idx];
    if (s.var_phase == 0) {
        if (v.w > 0) {
            if (SDL_Window* win = SDL_RenderGetWindow(renderer)) SDL_SetWindowSize(win, v.w, v.h);
        }
        if (s.cfg.apply_setting) {
            for (const auto& kv : v.sets) s.cfg.apply_setting(kv.first.c_str(), kv.second);
        }
        s.var_settle = 0;
        s.var_phase = 1;
        return;
    }
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(renderer, &ow, &oh);
    ++s.var_settle;
    const bool size_ok = v.w <= 0 || (ow == v.w && oh == v.h);
    if (s.var_settle < 4 || (!size_ok && s.var_settle < 120)) return;
    if (!size_ok) std::fprintf(stderr, "[%s] variant %s: output stayed %dx%d\n", s.log_tag.c_str(), v.spec.c_str(), ow, oh);
    make_dir(s.dir);
    grab_output(s, renderer, s.dir + "/" + frame_label(t) + "_output_" + sanitize_variant(v.spec) +
                                 (size_ok ? "" : "_actual" + std::to_string(ow) + "x" + std::to_string(oh)) + ".png");
    s.var_phase = 0;
    if (++s.var_idx >= s.variants.size()) {
        s.var_idx = 0;
        s.pending_output_label.store(kNone);
        s.outstanding.fetch_sub(1);
        request_quit_if_done(s);
    }
}

// ---------------------------------------------------------------------------------------
// PNG writer (RGB8, stored deflate blocks).
namespace {
std::uint32_t crc32_update(std::uint32_t crc, const std::uint8_t* p, size_t n) {
    static const std::vector<std::uint32_t> table = [] {
        std::vector<std::uint32_t> t(256);
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

void chunk(std::vector<std::uint8_t>& out, const char tag[4], const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> td(tag, tag + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    put32(out, crc32_update(0, td.data(), td.size()));
}
}  // namespace

std::vector<std::uint8_t> encode_png_rgb(const std::uint8_t* rgb, int width, int height, int pitch) {
    if (!rgb || width <= 0 || height <= 0 || pitch < width * 3) return {};
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);  // filter: none
        const std::uint8_t* row = rgb + static_cast<size_t>(y) * pitch;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 3);
    }
    std::vector<std::uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    for (;;) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 0xFF));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 0xFF));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
        if (last) break;
    }
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    put32(z, (b << 16) | a);

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, static_cast<std::uint32_t>(width));
    put32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // colour type RGB
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

bool write_png_rgb(const std::string& path, const std::uint8_t* rgb, int width, int height, int pitch) {
    const std::vector<std::uint8_t> out = encode_png_rgb(rgb, width, height, pitch);
    if (out.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

}  // namespace capture
}  // namespace gbaimp
