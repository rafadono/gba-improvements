#define SDL_MAIN_HANDLED
#include "test_harness.h"
#include "gbaimp/widescreen_gbarecomp.h"

#include <vector>

// Stand-ins for the runtime's globals (the library never links the real ones).
extern "C" {
int (*g_ws_tilemap_provider)(int, int, int, uint16_t*) = nullptr;
int (*g_ws_bg_x_provider)(int, int, int, int*) = nullptr;
unsigned g_ws_bg_x_provider_layers = 0xFu;
int (*g_ws_obj_attr_x_provider)(int, uint16_t, uint16_t, uint16_t, int*) = nullptr;
int g_ws_authored_margin_layers = 0;
int g_ws_obj_native_clip = 0;
int g_ws_pillarbox = 0;
int g_ws_pillarbox_left = 0;
int g_ws_pillarbox_right = 0;
unsigned g_ws_active = 0;
unsigned g_ws_extra_left = 0;
unsigned g_ws_extra_right = 0;
void (*g_runtime_fn_entry_hook)(uint32_t) = nullptr;
}

using namespace gbaimp;

namespace {

// ---- a made-up game: "mode" is a word of fake guest memory; kModeWide is the wide one --------
uint32_t g_mode_word = 0;             // what poll_context reads (0 = not yet set)
constexpr uint32_t kModeWide = 0x100, kModeField = 0x200;
constexpr uint32_t kEnterWidePc = 0x1000, kEnterFieldPc = 0x2000;
int g_entry_calls = 0;
int g_prev_hook_calls = 0;
uint32_t g_prev_hook_last = 0;

int poll_ctx(void*) {
    if (g_mode_word == 0) return -1;
    return g_mode_word == kModeWide ? 1 : 0;
}
void on_entry(uint32_t pc, void* user) {
    ++g_entry_calls;
    auto* ws = static_cast<Widescreen*>(user);
    if (pc == kEnterWidePc) ws->set_wide_context(true);
    else if (pc == kEnterFieldPc) ws->set_wide_context(false);
}
void prev_hook(uint32_t pc) { ++g_prev_hook_calls; g_prev_hook_last = pc; }

// Game sprite provider: player gauge (priority 1, x in [-16,85]) goes to the left screen edge.
int game_obj_provider(int, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x) {
    Widescreen& ws = Widescreen::instance();
    if (!out_x || !ws.view_active()) return 0;
    if (ws.wide_context_cached()) {
        const int sx = widescreen_obj_x(attr1);
        const int sy = ((attr0 & 0xFF) >= 160) ? int(attr0 & 0xFF) - 256 : int(attr0 & 0xFF);
        const int prio = (attr2 >> 10) & 3;
        if (prio == 1 && sx >= -16 && sx <= 85 && sy >= -16 && sy <= 24) { *out_x = sx - ws.extra_left(); return 1; }
        if (prio == 1 && sx >= 160 && sx <= 250 && sy >= -16 && sy <= 24) { *out_x = sx + ws.extra_right(); return 1; }
    }
    return default_obj_attr_x_provider(0, attr0, attr1, attr2, out_x);
}

struct Snapshot {
    void* tilemap; void* bg_x; unsigned layers; void* obj;
    int authored, clip, pillar, pillar_l, pillar_r;
    bool operator==(const Snapshot& o) const {
        return tilemap == o.tilemap && bg_x == o.bg_x && layers == o.layers && obj == o.obj &&
               authored == o.authored && clip == o.clip && pillar == o.pillar &&
               pillar_l == o.pillar_l && pillar_r == o.pillar_r;
    }
};
Snapshot snap() {
    return {reinterpret_cast<void*>(g_ws_tilemap_provider), reinterpret_cast<void*>(g_ws_bg_x_provider),
            g_ws_bg_x_provider_layers, reinterpret_cast<void*>(g_ws_obj_attr_x_provider),
            g_ws_authored_margin_layers, g_ws_obj_native_clip, g_ws_pillarbox,
            g_ws_pillarbox_left, g_ws_pillarbox_right};
}

// What the runtime does on a view change, then the per-frame hook.
void host_set_view(Widescreen& ws, bool wide, unsigned l = 22, unsigned r = 22) {
    g_ws_extra_left = wide ? l : 0u;
    g_ws_extra_right = wide ? r : 0u;
    g_ws_active = wide ? 1u : 0u;
    ws.update();
}

int hp_x(bool* moved) {
    int out = 0;
    const int r = g_ws_obj_attr_x_provider
        ? g_ws_obj_attr_x_provider(3, 2 | 0x4000, 20 | 0x4000, 0x0400, &out) : 0;
    *moved = (r == 1 && out != 20);
    return r == 1 ? out : 20;
}
int enemy_hp_x(bool* moved) {
    int out = 0;
    const int r = g_ws_obj_attr_x_provider
        ? g_ws_obj_attr_x_provider(4, 2 | 0x4000, 200 | 0x4000, 0x0400, &out) : 0;
    *moved = (r == 1 && out != 200);
    return r == 1 ? out : 200;
}

WidescreenProfile make_profile(Widescreen* ws) {
    WidescreenProfile p;
    p.user = ws;
    p.tilemap_provider = &default_tilemap_provider;
    p.bg_x_provider = &default_bg_x_provider;
    p.obj_attr_x_provider = &game_obj_provider;
    p.centered_bg_layers = 0x3u;                   // BG0 and BG1 stay in the centre
    p.poll_context = &poll_ctx;
    p.on_fn_entry = &on_entry;
    return p;
}

void test_return_to_original() {
    Widescreen& ws = Widescreen::instance();
    HudAnchoring& hud = HudAnchoring::instance();
    hud.set_mode(HudAnchorMode::OriginalCentered);
    hud.set_horizontal_offset(32);

    g_prev_hook_calls = 0;
    g_runtime_fn_entry_hook = &prev_hook;
    ws.configure(gbarecomp_widescreen_bindings(), make_profile(&ws));
    const Snapshot boot = snap();   // what a faithful run has
    CHECK(boot == (Snapshot{nullptr, nullptr, 0xFu, nullptr, 0, 0, 0, 0, 0}));

    ws.install();
    CHECK(boot == snap());          // 3:2: nothing installed
    CHECK(g_runtime_fn_entry_hook != &prev_hook);   // our link is in the chain
    ws.install();                   // idempotent: prev is not captured twice
    g_runtime_fn_entry_hook(0x4242);
    CHECK_EQ(g_prev_hook_calls, 1); CHECK_EQ(g_prev_hook_last, 0x4242u);

    bool moved = false;
    int el = -1, er = -1;
    host_set_view(ws, false);
    CHECK(boot == snap());
    CHECK(!ws.view_active() && !ws.presentation_active());
    ws.effective_extra_pixels(&el, &er); CHECK_EQ(el, 0); CHECK_EQ(er, 0);
    CHECK_EQ(hp_x(&moved), 20); CHECK(!moved);

    // 3:2 -> 16:9 outside the wide context: pillarboxed, providers installed, HUD untouched.
    host_set_view(ws, true);
    CHECK(ws.view_active() && !ws.presentation_active());
    ws.effective_extra_pixels(&el, &er); CHECK_EQ(el, 0); CHECK_EQ(er, 0);
    CHECK_EQ(g_ws_pillarbox, 1);
    CHECK(g_ws_tilemap_provider != nullptr && g_ws_bg_x_provider != nullptr && g_ws_obj_attr_x_provider != nullptr);
    CHECK_EQ(g_ws_authored_margin_layers, 1);
    CHECK_EQ(hp_x(&moved), 20); CHECK(!moved);
    CHECK(hud.settings().mode == HudAnchorMode::OriginalCentered);

    // Enter the wide context through the function-entry hook.
    g_runtime_fn_entry_hook(kEnterWidePc);
    CHECK(ws.wide_context_cached());
    CHECK_EQ(g_ws_pillarbox, 0);
    CHECK(ws.presentation_active());
    ws.effective_extra_pixels(&el, &er); CHECK_EQ(el, 22); CHECK_EQ(er, 22);
    CHECK(hud.settings().mode == HudAnchorMode::WidescreenAnchored);
    CHECK_EQ(hud.settings().horizontal_offset, 22);
    CHECK_EQ(hp_x(&moved), 20 - 22); CHECK(moved);
    CHECK_EQ(enemy_hp_x(&moved), 200 + 22); CHECK(moved);
    CHECK_EQ(g_ws_tilemap_provider(2, 0, 0, nullptr), kWsTilemapKeepWrapped);
    {   // bg X provider: BG0/BG1 centred, BG2/BG3 across the full width
        int hw = 0;
        CHECK_EQ(g_ws_bg_x_provider(0, 5, 0, &hw), -1);
        CHECK_EQ(g_ws_bg_x_provider(1, 22 + 240, 0, &hw), -1);
        CHECK_EQ(g_ws_bg_x_provider(0, 100, 0, &hw), 0);
        CHECK_EQ(g_ws_bg_x_provider(2, 5, 0, &hw), 0);
        CHECK_EQ(g_ws_bg_x_provider(0, 5, 0, nullptr), 0);
    }

    // 3:2 WHILE the context is wide: everything back to a faithful start.
    host_set_view(ws, false);
    CHECK(ws.wide_context_cached());
    CHECK(boot == snap());
    CHECK(!ws.view_active() && !ws.presentation_active());
    ws.effective_extra_pixels(&el, &er); CHECK_EQ(el, 0); CHECK_EQ(er, 0);
    CHECK_EQ(hp_x(&moved), 20); CHECK(!moved);
    CHECK_EQ(default_tilemap_provider(2, 0, 0, nullptr), 0);
    {
        int hw = 0, o = 0;
        CHECK_EQ(default_bg_x_provider(0, 5, 0, &hw), 0);
        CHECK_EQ(default_obj_attr_x_provider(0, 0, 300, 0, &o), 0);
    }
    CHECK(hud.settings().mode == HudAnchorMode::OriginalCentered);
    CHECK_EQ(hud.settings().horizontal_offset, 32);
    {
        uint16_t a0 = 10, a1 = 20;
        hud.process_oam_entry(a0, a1);
        CHECK_EQ(a0, uint16_t(10)); CHECK_EQ(a1, uint16_t(20));
    }
    {   // 3:2 viewport
        SDL_Rect src{}, dst{}; int bw = 0, bh = 0;
        ws.compute_effective_viewport(240, 160, 1920, 1080, &src, &dst, &bw, &bh);
        CHECK_EQ(src.x, 0); CHECK_EQ(src.w, 240); CHECK_EQ(src.h, 160);
        CHECK_EQ(bw, 240); CHECK_EQ(dst.w, 1620); CHECK_EQ(dst.h, 1080);
    }

    // Back to 16:9 mid-context, leave the context in 16:9, then round trips through 3:2.
    host_set_view(ws, true);
    CHECK_EQ(g_ws_pillarbox, 0);
    CHECK_EQ(hp_x(&moved), -2); CHECK(moved);
    g_runtime_fn_entry_hook(kEnterFieldPc);
    CHECK_EQ(g_ws_pillarbox, 1);
    CHECK_EQ(hp_x(&moved), 20); CHECK(!moved);
    g_runtime_fn_entry_hook(kEnterWidePc);
    host_set_view(ws, false);
    g_runtime_fn_entry_hook(kEnterFieldPc);
    CHECK(boot == snap());
    host_set_view(ws, true);
    CHECK_EQ(g_ws_pillarbox, 1); CHECK(!ws.wide_context_cached());
    host_set_view(ws, false);
    CHECK(boot == snap());

    // Asymmetric extras: each HUD side uses its own real extra width; offset is the larger one.
    g_runtime_fn_entry_hook(kEnterWidePc);
    host_set_view(ws, true, 60, 59);
    CHECK_EQ(hp_x(&moved), 20 - 60); CHECK(moved);
    CHECK_EQ(enemy_hp_x(&moved), 200 + 59); CHECK(moved);
    CHECK_EQ(hud.settings().horizontal_offset, 60);
    ws.effective_extra_pixels(&el, &er); CHECK_EQ(el, 60); CHECK_EQ(er, 59);
    ws.effective_extra_pixels(nullptr, nullptr);   // null outputs are fine
    g_runtime_fn_entry_hook(kEnterFieldPc);
    host_set_view(ws, false);
    CHECK(boot == snap());
    CHECK(hud.settings().mode == HudAnchorMode::OriginalCentered);
    CHECK_EQ(hud.settings().horizontal_offset, 32);

    // Extras but runtime flag off, or flag on with no extras: not an expanded view.
    g_ws_active = 0; g_ws_extra_left = 22; g_ws_extra_right = 22; ws.update();
    CHECK(!ws.view_active() && boot == snap());
    g_ws_active = 1; g_ws_extra_left = 0; g_ws_extra_right = 0; ws.update();
    CHECK(!ws.view_active() && boot == snap());
    g_ws_active = 0;

    // Removing the hook restores the previous one.
    CHECK(ws.remove_fn_entry_hook());
    CHECK(g_runtime_fn_entry_hook == &prev_hook);
    CHECK(!ws.remove_fn_entry_hook());
    g_runtime_fn_entry_hook = nullptr;
    ws.install();                       // slot was replaced behind our back: re-chained
    CHECK(g_runtime_fn_entry_hook != nullptr);
    CHECK(ws.remove_fn_entry_hook());
    g_runtime_fn_entry_hook = nullptr;
}

void test_poll_and_frames() {
    Widescreen& ws = Widescreen::instance();
    ws.configure(gbarecomp_widescreen_bindings(), make_profile(&ws));
    g_ws_extra_left = g_ws_extra_right = 22; g_ws_active = 1;
    g_mode_word = 0;                          // unknown: keeps the current value
    CHECK(!ws.wide_context());
    ws.set_wide_context(true);
    CHECK(ws.wide_context());
    g_mode_word = kModeField;                 // pull: narrow
    CHECK(!ws.wide_context());
    CHECK_EQ(g_ws_pillarbox, 1);
    g_mode_word = kModeWide;
    ws.update();
    CHECK(ws.wide_context_cached()); CHECK_EQ(g_ws_pillarbox, 0);

    // Frame counter and context stamps.
    CHECK_EQ(ws.frame_counter(), uint64_t(0));
    ws.notify_present(); ws.notify_present(); ws.notify_present();
    CHECK_EQ(ws.frame_counter(), uint64_t(3));
    g_mode_word = kModeField; ws.update();
    CHECK_EQ(ws.last_narrow_frame(), uint64_t(3));
    g_mode_word = kModeWide; ws.update();
    CHECK_EQ(ws.last_wide_frame(), uint64_t(3));

    // The viewport polls the context itself.
    SDL_Rect src{}, dst{}; int bw = 0, bh = 0;
    g_mode_word = kModeField;
    widescreen_compute_viewport(284, 160, 1920, 1080, &src, &dst, &bw, &bh);
    CHECK(!ws.wide_context_cached());
    CHECK_EQ(src.x, 22); CHECK_EQ(src.w, 240); CHECK_EQ(src.h, 160); CHECK_EQ(bw, 240);
    CHECK_EQ(dst.w, 1620); CHECK_EQ(dst.h, 1080);
    g_mode_word = kModeWide;
    widescreen_compute_viewport(284, 160, 1920, 1080, &src, &dst, &bw, &bh);
    CHECK(ws.wide_context_cached());
    CHECK_EQ(src.x, 0); CHECK_EQ(src.w, 284); CHECK_EQ(bw, 284); CHECK_EQ(bh, 160);
    CHECK_EQ(dst.h, 1080); CHECK_EQ(dst.w, 1917);
    CHECK_EQ(dst.x, (1920 - 1917) / 2);

    // Invalid arguments leave outputs untouched.
    SDL_Rect keep{7, 7, 7, 7};
    ws.compute_effective_viewport(0, 160, 1920, 1080, &keep, &dst, &bw, &bh);
    CHECK_EQ(keep.x, 7);
    ws.compute_effective_viewport(284, 160, 1920, 1080, nullptr, &dst, &bw, &bh);

    g_ws_active = 0; g_ws_extra_left = g_ws_extra_right = 0;
    ws.set_wide_context(false); g_mode_word = 0;
}

void test_pure_viewport() {
    SDL_Rect src{}, dst{}; int bw = 0, bh = 0;
    // 3x prescaled wide texture (852x480), narrow context: crop 22*3 columns, 240x160 base.
    widescreen_viewport(false, 852, 480, 1280, 720, &src, &dst, &bw, &bh);
    CHECK_EQ(src.x, 66); CHECK_EQ(src.w, 720); CHECK_EQ(src.h, 480);
    CHECK_EQ(bw, 240); CHECK_EQ(bh, 160);
    CHECK_EQ(dst.w, 1080); CHECK_EQ(dst.h, 720); CHECK_EQ(dst.x, 100);
    // Wide context on a native texture: still the 3:2 branch.
    widescreen_viewport(true, 240, 160, 480, 320, &src, &dst, &bw, &bh);
    CHECK_EQ(src.w, 240); CHECK_EQ(bw, 240); CHECK_EQ(dst.w, 480); CHECK_EQ(dst.h, 320);
    CHECK_EQ(widescreen_obj_x(0x1FF), -1);
    CHECK_EQ(widescreen_obj_x(0x0FF), 255);
    CHECK_EQ(widescreen_obj_x(uint16_t(0xFE00 | 0x100)), -256);
}

void test_independent_instance_and_options() {
    // An independent instance with its own bindings and HudAnchoring; bindings may be sparse.
    unsigned active = 1, l = 30, r = 10;
    int pillar = -1;
    HudAnchoring my_hud;
    WidescreenBindings b;
    b.active = &active; b.extra_left = &l; b.extra_right = &r; b.pillarbox = &pillar;
    WidescreenProfile p;
    p.hud = &my_hud;
    p.hud_default_offset = 8;
    p.initial_wide_context = true;
    p.pillarbox_outside_context = false;
    Widescreen ws;
    ws.configure(b, p);
    ws.apply_policy();
    CHECK(ws.presentation_active());
    CHECK_EQ(pillar, 0);
    CHECK(my_hud.settings().mode == HudAnchorMode::WidescreenAnchored);
    CHECK_EQ(my_hud.settings().horizontal_offset, 30);
    ws.set_wide_context(false);
    CHECK_EQ(pillar, 0);                           // pillarbox_outside_context = false
    CHECK(my_hud.settings().mode == HudAnchorMode::OriginalCentered);
    CHECK_EQ(my_hud.settings().horizontal_offset, 8);
    CHECK(HudAnchoring::instance().settings().mode == HudAnchorMode::OriginalCentered);
    active = 0; ws.apply_policy();
    CHECK_EQ(pillar, 0);
    CHECK(!ws.install_fn_entry_hook());            // not the shared instance / no slot
    CHECK(!ws.remove_fn_entry_hook());

    // manage_hud = false never touches the HudAnchoring.
    HudAnchoring untouched;
    untouched.set_horizontal_offset(77);
    p.hud = &untouched; p.manage_hud = false; active = 1; ws.configure(b, p);
    ws.set_wide_context(true);
    CHECK_EQ(untouched.settings().horizontal_offset, 77);
    CHECK(untouched.settings().mode == HudAnchorMode::OriginalCentered);

    // Empty bindings and profile: everything is a harmless no-op.
    Widescreen empty;
    empty.configure(WidescreenBindings{}, WidescreenProfile{});
    empty.install(); empty.update();
    CHECK(!empty.view_active() && !empty.presentation_active());
    empty.set_wide_context(true);
    CHECK_EQ(empty.extra_left(), 0);
}

// ---- declarative margin policy per scene --------------------------------------------------------
enum class Scene { Menu, Map, Battle, Cutscene };
Scene g_scene = Scene::Menu;
int g_policy_calls = 0;
WsMarginPolicySet scene_policy(bool wide, void*) {
    ++g_policy_calls;
    if (!wide) return WsMarginPolicySet::all(WsMarginPolicy::Blank);               // menus: nothing in the margins
    if (g_scene == Scene::Battle) {
        WsMarginPolicySet s;                                                       // backdrop (BG2) extends
        s.set_bg(0xB, WsMarginPolicy::Blank);                                      // BG0, BG1, BG3 stay inside
        return s;
    }
    if (g_scene == Scene::Cutscene) {
        WsMarginPolicySet s;
        s.bg[1] = WsMarginPolicy::Runtime;
        s.obj = WsMarginPolicy::Blank;                                             // sprites clipped to 240
        return s;
    }
    return WsMarginPolicySet{};                                                    // map: everything extends
}
int scene_ctx(void*) { return g_scene == Scene::Menu ? 0 : 1; }

// Answers of the installed background-X provider for one layer at a margin column and at the centre.
int bgx(int bg, int x) {
    int hw = 0;
    return g_ws_bg_x_provider ? g_ws_bg_x_provider(bg, x, 40, &hw) : 99;
}

void test_margin_policy() {
    Widescreen& ws = Widescreen::instance();
    HudAnchoring::instance().set_mode(HudAnchorMode::OriginalCentered);
    HudAnchoring::instance().set_horizontal_offset(32);
    g_runtime_fn_entry_hook = nullptr;
    WidescreenProfile p;
    p.tilemap_provider = &default_tilemap_provider;
    p.bg_x_provider = &default_bg_x_provider;
    p.poll_context = &scene_ctx;
    p.margin_policy = &scene_policy;
    ws.configure(gbarecomp_widescreen_bindings(), p);
    const Snapshot native = snap();

    // Pure value type.
    WsMarginPolicySet s;
    s.set_bg(0x5, WsMarginPolicy::Blank).obj = WsMarginPolicy::Runtime;
    const WsMarginPolicySet r = WsMarginPolicySet::unpack(s.pack());
    CHECK(r.bg[0] == WsMarginPolicy::Blank && r.bg[1] == WsMarginPolicy::Extend);
    CHECK(r.bg[2] == WsMarginPolicy::Blank && r.bg[3] == WsMarginPolicy::Extend);
    CHECK(r.obj == WsMarginPolicy::Runtime);
    CHECK(WsMarginPolicySet::all(WsMarginPolicy::Blank).pack() != WsMarginPolicySet{}.pack());

    // Menu: nothing in the margins on any layer; the centre is never touched.
    g_scene = Scene::Menu;
    host_set_view(ws, true);
    CHECK(!ws.wide_context_cached());
    for (int bg = 0; bg < 4; ++bg) {
        CHECK_EQ(bgx(bg, 3), -1);     // left margin (22 px)
        CHECK_EQ(bgx(bg, 270), -1);   // right margin
        CHECK_EQ(bgx(bg, 22), 0);     // first native column
        CHECK_EQ(bgx(bg, 100), 0);
        CHECK_EQ(bgx(bg, 261), 0);    // last native column
    }
    CHECK_EQ(g_ws_obj_native_clip, 1);     // all(Blank) includes OBJ

    // Map: every layer extends with the wrapped ring.
    g_scene = Scene::Map;
    ws.update();
    CHECK(ws.wide_context_cached());
    for (int bg = 0; bg < 4; ++bg) { CHECK_EQ(bgx(bg, 3), 0); CHECK_EQ(bgx(bg, 270), 0); }

    // Battle: BG0, BG1 and BG3 blank in the margins, BG2 extends, centre untouched.
    g_scene = Scene::Battle;
    ws.update();
    CHECK_EQ(bgx(0, 3), -1); CHECK_EQ(bgx(1, 270), -1); CHECK_EQ(bgx(3, 3), -1);
    CHECK_EQ(bgx(2, 3), 0);  CHECK_EQ(bgx(2, 270), 0);
    for (int bg = 0; bg < 4; ++bg) CHECK_EQ(bgx(bg, 120), 0);
    CHECK(ws.margin_policy().bg[2] == WsMarginPolicy::Extend);
    CHECK(ws.margin_policy_for_bg(7) == WsMarginPolicy::Runtime);
    int hw = 0;
    CHECK_EQ(default_bg_x_provider(9, 3, 0, &hw), 0);        // not a real layer: left to the runtime

    // OBJ Blank installs the native clip; Runtime never blanks.
    g_scene = Scene::Cutscene;
    ws.update();
    CHECK_EQ(bgx(1, 3), 0);          // Runtime = decline
    CHECK_EQ(bgx(0, 3), 0);
    CHECK_EQ(g_ws_obj_native_clip, 1);
    g_scene = Scene::Map;
    ws.update();
    CHECK_EQ(g_ws_obj_native_clip, 0);

    // The answer is cached: per-pixel provider calls do not call the profile.
    const int calls = g_policy_calls;
    for (int i = 0; i < 1000; ++i) bgx(i & 3, i % 284);
    CHECK_EQ(g_policy_calls, calls);

    // Back to 3:2 in the middle of a battle: the exact native state (invariant).
    g_scene = Scene::Cutscene;
    ws.update();
    CHECK_EQ(g_ws_obj_native_clip, 1);
    host_set_view(ws, false);
    CHECK(native == snap());
    CHECK_EQ(g_ws_obj_native_clip, 0);
    CHECK(HudAnchoring::instance().settings().mode == HudAnchorMode::OriginalCentered);
    // and the provider is inert if somebody still calls it
    CHECK_EQ(default_bg_x_provider(0, 3, 0, &hw), 0);

    // Without margin_policy the old rule holds: narrow blanks all, wide blanks centered_bg_layers.
    p.margin_policy = nullptr;
    p.centered_bg_layers = 0x1u;
    ws.configure(gbarecomp_widescreen_bindings(), p);
    g_scene = Scene::Menu;
    host_set_view(ws, true);
    CHECK_EQ(bgx(2, 3), -1);
    g_scene = Scene::Map;
    ws.update();
    CHECK_EQ(bgx(0, 3), -1); CHECK_EQ(bgx(1, 3), 0); CHECK_EQ(bgx(2, 270), 0);
    host_set_view(ws, false);
    CHECK(native == snap());
}

} // namespace

int main() {
    SDL_SetMainReady();
    test_return_to_original();
    test_poll_and_frames();
    test_pure_viewport();
    test_independent_instance_and_options();
    test_margin_policy();
    return testh::finish("widescreen");
}
