#include "gbaimp/widescreen.h"

#include <algorithm>

namespace gbaimp {

uint32_t WsMarginPolicySet::pack() const {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= (static_cast<uint32_t>(bg[i]) & 3u) << (2 * i);
    v |= (static_cast<uint32_t>(obj) & 3u) << 8;
    return v;
}

WsMarginPolicySet WsMarginPolicySet::unpack(uint32_t v) {
    auto at = [v](int shift) {
        const uint32_t x = (v >> shift) & 3u;
        return x > 2u ? WsMarginPolicy::Runtime : static_cast<WsMarginPolicy>(x);
    };
    WsMarginPolicySet s;
    for (int i = 0; i < 4; ++i) s.bg[i] = at(2 * i);
    s.obj = at(8);
    return s;
}

Widescreen& Widescreen::instance() {
    static Widescreen s_instance;
    return s_instance;
}

HudAnchoring& Widescreen::hud() const {
    return profile_.hud ? *profile_.hud : HudAnchoring::instance();
}

void Widescreen::configure(const WidescreenBindings& bindings, const WidescreenProfile& profile) {
    bindings_ = bindings;
    profile_ = profile;
    context_.store(profile.initial_wide_context, std::memory_order_relaxed);
    refresh_margin_policy();
    frame_counter_.store(0, std::memory_order_relaxed);
    last_wide_frame_.store(0, std::memory_order_relaxed);
    last_narrow_frame_.store(0, std::memory_order_relaxed);
}

// --- view queries ---------------------------------------------------------------------------

int Widescreen::extra_left() const {
    return (view_active() && bindings_.extra_left) ? static_cast<int>(*bindings_.extra_left) : 0;
}

int Widescreen::extra_right() const {
    return (view_active() && bindings_.extra_right) ? static_cast<int>(*bindings_.extra_right) : 0;
}

bool Widescreen::view_active() const {
    if (!bindings_.active || !*bindings_.active) return false;
    const unsigned l = bindings_.extra_left ? *bindings_.extra_left : 0u;
    const unsigned r = bindings_.extra_right ? *bindings_.extra_right : 0u;
    return (l + r) > 0;
}

bool Widescreen::presentation_active() const {
    return view_active() && context_.load(std::memory_order_relaxed);
}

void Widescreen::effective_extra_pixels(int* left, int* right) const {
    const bool wide = presentation_active();
    if (left) *left = wide ? extra_left() : 0;
    if (right) *right = wide ? extra_right() : 0;
}

// --- policy ---------------------------------------------------------------------------------

void Widescreen::refresh_margin_policy() {
    const bool wide = context_.load(std::memory_order_relaxed);
    WsMarginPolicySet set;
    if (profile_.margin_policy) {
        set = profile_.margin_policy(wide, profile_.user);
    } else if (!wide) {
        set = WsMarginPolicySet::all(WsMarginPolicy::Blank);
        set.obj = WsMarginPolicy::Extend;
    } else {
        set.set_bg(profile_.centered_bg_layers, WsMarginPolicy::Blank);
    }
    margin_policy_.store(set.pack(), std::memory_order_relaxed);
}

WsMarginPolicySet Widescreen::margin_policy() const {
    return WsMarginPolicySet::unpack(margin_policy_.load(std::memory_order_relaxed));
}

WsMarginPolicy Widescreen::margin_policy_for_bg(int bg) const {
    if (bg < 0 || bg > 3) return WsMarginPolicy::Runtime;
    return static_cast<WsMarginPolicy>((margin_policy_.load(std::memory_order_relaxed) >> (2 * bg)) & 3u);
}

void Widescreen::apply_policy() {
    const bool expanded = view_active();
    const bool presenting_wide = expanded && context_.load(std::memory_order_relaxed);
    const WidescreenBindings& b = bindings_;
    refresh_margin_policy();

    if (expanded) {
        if (b.tilemap_provider) *b.tilemap_provider = profile_.tilemap_provider;
        if (b.bg_x_provider) *b.bg_x_provider = profile_.bg_x_provider;
        if (b.bg_x_provider_layers) *b.bg_x_provider_layers = profile_.bg_x_provider_layers;
        if (b.obj_attr_x_provider) *b.obj_attr_x_provider = profile_.obj_attr_x_provider;
        if (b.authored_margin_layers) *b.authored_margin_layers = profile_.authored_margin_layers;
        if (b.obj_native_clip) {
            const bool blank_obj = margin_policy().obj == WsMarginPolicy::Blank;
            *b.obj_native_clip = blank_obj ? 1 : profile_.obj_native_clip;
        }
        if (b.pillarbox) {
            *b.pillarbox = (!presenting_wide && profile_.pillarbox_outside_context) ? 1 : 0;
        }
    } else {
        // Faithful native run: exactly what the runtime leaves for a game that never widens.
        if (b.tilemap_provider) *b.tilemap_provider = nullptr;
        if (b.bg_x_provider) *b.bg_x_provider = nullptr;
        if (b.bg_x_provider_layers) *b.bg_x_provider_layers = 0xFu;
        if (b.obj_attr_x_provider) *b.obj_attr_x_provider = nullptr;
        if (b.authored_margin_layers) *b.authored_margin_layers = 0;
        if (b.obj_native_clip) *b.obj_native_clip = 0;
        if (b.pillarbox) *b.pillarbox = 0;
        if (b.pillarbox_left) *b.pillarbox_left = 0;
        if (b.pillarbox_right) *b.pillarbox_right = 0;
    }

    if (profile_.manage_hud) {
        HudAnchoring& anchoring = hud();
        const HudAnchorMode mode = presenting_wide ? HudAnchorMode::WidescreenAnchored
                                                   : HudAnchorMode::OriginalCentered;
        if (anchoring.settings().mode != mode) anchoring.set_mode(mode);
        const int offset = presenting_wide ? std::max(extra_left(), extra_right())
                                           : profile_.hud_default_offset;
        if (anchoring.settings().horizontal_offset != std::clamp(offset, 0, 96)) {
            anchoring.set_horizontal_offset(offset);
        }
    }
}

void Widescreen::set_wide_context(bool wide) {
    context_.store(wide, std::memory_order_relaxed);
    const uint64_t now = frame_counter_.load(std::memory_order_relaxed);
    if (wide) last_wide_frame_.store(now, std::memory_order_relaxed);
    else last_narrow_frame_.store(now, std::memory_order_relaxed);
    apply_policy();
}

bool Widescreen::wide_context() {
    if (profile_.poll_context) {
        const int polled = profile_.poll_context(profile_.user);
        if (polled >= 0) {
            const bool wide = polled != 0;
            if (wide != context_.load(std::memory_order_relaxed)) set_wide_context(wide);
        }
        // The callback may also depend on flags read during the poll (a scene inside a context).
        refresh_margin_policy();
    }
    return context_.load(std::memory_order_relaxed);
}

void Widescreen::install() {
    apply_policy();
    install_fn_entry_hook();
}

void Widescreen::update() {
    wide_context();
    apply_policy();
    install_fn_entry_hook();
}

// --- function-entry hook --------------------------------------------------------------------

void widescreen_fn_entry_trampoline(uint32_t pc) {
    Widescreen& ws = Widescreen::instance();
    if (ws.prev_fn_entry_hook_) ws.prev_fn_entry_hook_(pc);
    if (ws.profile_.on_fn_entry) ws.profile_.on_fn_entry(pc, ws.profile_.user);
}

bool Widescreen::install_fn_entry_hook() {
    if (this != &instance() || !bindings_.fn_entry_hook || !profile_.on_fn_entry) return false;
    FnEntryHook& slot = *bindings_.fn_entry_hook;
    if (slot == &widescreen_fn_entry_trampoline) return true;
    prev_fn_entry_hook_ = slot;
    slot = &widescreen_fn_entry_trampoline;
    return true;
}

bool Widescreen::remove_fn_entry_hook() {
    if (this != &instance() || !bindings_.fn_entry_hook) return false;
    FnEntryHook& slot = *bindings_.fn_entry_hook;
    if (slot != &widescreen_fn_entry_trampoline) return false;
    slot = prev_fn_entry_hook_;
    prev_fn_entry_hook_ = nullptr;
    return true;
}

// --- viewport -------------------------------------------------------------------------------

namespace {
void fit_uniform(int base_w, int base_h, int win_w, int win_h, SDL_Rect* dst) {
    const float s = std::min(static_cast<float>(win_w) / static_cast<float>(base_w),
                             static_cast<float>(win_h) / static_cast<float>(base_h));
    const int dw = static_cast<int>(static_cast<float>(base_w) * s);
    const int dh = static_cast<int>(static_cast<float>(base_h) * s);
    dst->x = (win_w - dw) / 2;
    dst->y = (win_h - dh) / 2;
    dst->w = dw;
    dst->h = dh;
}
} // namespace

void widescreen_viewport(bool wide_context, int tex_w, int tex_h, int win_w, int win_h,
                         SDL_Rect* out_src, SDL_Rect* out_dst, int* out_base_w, int* out_base_h) {
    if (!out_src || !out_dst || tex_w <= 0 || tex_h <= 0 || win_w <= 0 || win_h <= 0) return;

    const bool is_expanded = (tex_w * 2 > tex_h * 3);
    const int factor = (tex_h / 160 > 0) ? (tex_h / 160) : 1;
    const int base_w_val = tex_w / factor;

    if (is_expanded && wide_context) {
        out_src->x = 0;
        out_src->y = 0;
        out_src->w = tex_w;
        out_src->h = tex_h;
        if (out_base_w) *out_base_w = base_w_val;
        if (out_base_h) *out_base_h = 160;
        fit_uniform(base_w_val, 160, win_w, win_h, out_dst);
    } else if (is_expanded) {
        // Expanded texture, narrow context: crop the central authentic 240x160 area.
        const int margin_left = (base_w_val - 240) / 2;
        out_src->x = margin_left * factor;
        out_src->y = 0;
        out_src->w = 240 * factor;
        out_src->h = 160 * factor;
        if (out_base_w) *out_base_w = 240;
        if (out_base_h) *out_base_h = 160;
        fit_uniform(240, 160, win_w, win_h, out_dst);
    } else {
        out_src->x = 0;
        out_src->y = 0;
        out_src->w = tex_w;
        out_src->h = tex_h;
        if (out_base_w) *out_base_w = 240;
        if (out_base_h) *out_base_h = 160;
        fit_uniform(240, 160, win_w, win_h, out_dst);
    }
}

void Widescreen::compute_effective_viewport(int tex_w, int tex_h, int win_w, int win_h,
                                            SDL_Rect* out_src, SDL_Rect* out_dst,
                                            int* out_base_w, int* out_base_h) {
    if (!out_src || !out_dst || tex_w <= 0 || tex_h <= 0 || win_w <= 0 || win_h <= 0) return;
    widescreen_viewport(wide_context(), tex_w, tex_h, win_w, win_h, out_src, out_dst, out_base_w, out_base_h);
}

void widescreen_compute_viewport(int tex_w, int tex_h, int win_w, int win_h,
                                 SDL_Rect* out_src, SDL_Rect* out_dst,
                                 int* out_base_w, int* out_base_h) {
    Widescreen::instance().compute_effective_viewport(tex_w, tex_h, win_w, win_h,
                                                      out_src, out_dst, out_base_w, out_base_h);
}

// --- providers ------------------------------------------------------------------------------

int widescreen_obj_x(uint16_t attr1) {
    const int raw_x = static_cast<int>(attr1 & 0x1FFu);
    return (raw_x >= 256) ? (raw_x - 512) : raw_x;
}

int default_tilemap_provider(int, int, int, uint16_t*) {
    return Widescreen::instance().presentation_active() ? kWsTilemapKeepWrapped : 0;
}

int default_bg_x_provider(int bg, int output_x, int, int* out_hw_x) {
    const Widescreen& ws = Widescreen::instance();
    if (!out_hw_x || !ws.view_active()) return 0;
    const int left = ws.extra_left();
    const int right = ws.extra_right();
    if (left == 0 && right == 0) return 0;

    const bool in_margin = (output_x < left) || (output_x >= left + 240);
    if (!in_margin) return 0;
    return ws.margin_policy_for_bg(bg) == WsMarginPolicy::Blank ? -1 : 0;
}

int default_obj_attr_x_provider(int, uint16_t, uint16_t attr1, uint16_t, int* out_x) {
    if (!out_x || !Widescreen::instance().view_active()) return 0;
    if ((attr1 & 0x1FFu) >= 256) {
        *out_x = widescreen_obj_x(attr1);
        return 1;
    }
    return 0;
}

} // namespace gbaimp
