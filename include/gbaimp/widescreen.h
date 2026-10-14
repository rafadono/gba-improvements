#pragma once
// Generic widescreen core for a GBA static-recompilation runtime.
//
// The runtime (gbarecomp) can render a view wider than 240x160. What it shows in the extra
// columns is host policy. This module owns the part of that policy that is the same for every
// game and leaves the game-specific decisions to a WidescreenProfile.
//
// What it owns (derived state only, nothing cached from the runtime):
//   - view_active()          the host presents an expanded view (runtime flag set and extras > 0).
//   - presentation_active()  expanded view AND the game's "wide context" is on (the picture is
//                            really shown widened; this is also exactly when the HUD is anchored).
//   - effective_extra_pixels()  real extra width per side while the presentation is active, else 0/0.
//   - apply_policy()         installs / withdraws the tilemap, background-X and sprite-X providers
//                            and sets the pillarbox / margin policy values from (expanded?, context?),
//                            then derives the HudAnchoring mode and offset. Idempotent and stateless:
//                            a view switch, a context change or a save-state load cannot leave
//                            anything stale behind.
//   - frame counter and compute_effective_viewport() (src / dst / base rectangles).
//
// Invariant: whenever the view is not expanded, every policy value equals what the runtime
// sets for a faithful native run (providers null, bg layer mask 0xF, authored margin layers 0,
// native clip 0, pillarbox 0/0/0) and the HudAnchoring mode / offset are back to
// OriginalCentered / profile.hud_default_offset.
//
// What the host supplies:
//   - WidescreenBindings: pointers to the runtime's g_ws_* values and provider slots. The library
//     never links those symbols (they belong to the runtime); the host fills the struct, either
//     by hand or with gbaimp/widescreen_gbarecomp.h. A test fills it with plain variables.
//   - WidescreenProfile: the game-specific part (which providers, how the "wide context" is decided,
//     HUD defaults). See docs/MODULES.md for a step by step guide.
//
// "Wide context" is whatever the game decides it is: a battle arena, a scene authored for a wider
// screen, or simply always. It is a bool owned by this module that the host drives with
// set_wide_context() (push, e.g. from the function-entry hook) and/or profile.poll_context (pull).
//
// Threading: the context flag and the frame counter are atomic (relaxed); everything else is meant
// for the thread that calls the runtime's per-frame hook, like the runtime's own g_ws_* values.
#include "gbaimp/hud_anchoring.h"
#include "gbaimp/sdl_interpose.h"

#include <atomic>
#include <cstdint>

namespace gbaimp {

// Signatures of the runtime's provider slots (see docs/GBARECOMP_CONTRACT.md, section 2.2).
using WsTilemapProvider = int (*)(int bg, int hw_x, int screen_y, uint16_t* out_entry);
using WsBgXProvider = int (*)(int bg, int output_x, int screen_y, int* out_hw_x);
using WsObjAttrXProvider = int (*)(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x);
using FnEntryHook = void (*)(uint32_t entry_pc);

// Return value of a tilemap provider that keeps the hardware-wrapped entry on purpose.
constexpr int kWsTilemapKeepWrapped = 2;

// Pointers to the runtime's values. Every pointer may be null: a null input reads as 0, a null
// output is simply not written (useful for a runtime that lacks that global).
struct WidescreenBindings {
    // Read-only view state written by the runtime.
    const unsigned* active = nullptr;        // g_ws_active
    const unsigned* extra_left = nullptr;    // g_ws_extra_left
    const unsigned* extra_right = nullptr;   // g_ws_extra_right

    // Policy outputs written by this module.
    WsTilemapProvider* tilemap_provider = nullptr;       // g_ws_tilemap_provider
    WsBgXProvider* bg_x_provider = nullptr;              // g_ws_bg_x_provider
    unsigned* bg_x_provider_layers = nullptr;            // g_ws_bg_x_provider_layers
    WsObjAttrXProvider* obj_attr_x_provider = nullptr;   // g_ws_obj_attr_x_provider
    int* authored_margin_layers = nullptr;               // g_ws_authored_margin_layers
    int* obj_native_clip = nullptr;                      // g_ws_obj_native_clip
    int* pillarbox = nullptr;                            // g_ws_pillarbox
    int* pillarbox_left = nullptr;                       // g_ws_pillarbox_left
    int* pillarbox_right = nullptr;                      // g_ws_pillarbox_right

    // Function-entry hook slot (g_runtime_fn_entry_hook), only needed when the profile
    // has an on_fn_entry callback.
    FnEntryHook* fn_entry_hook = nullptr;
};

// Everything that differs from game to game. All members are optional; the defaults give a
// profile that never widens anything (providers null) and reports a permanently "narrow" context.
struct WidescreenProfile {
    // Opaque pointer handed back to poll_context / on_fn_entry.
    void* user = nullptr;

    // Providers installed while the view is expanded. A null entry leaves that slot empty
    // (the runtime then behaves faithfully for it). The default_* functions below are
    // ready-made choices; a sprite provider that tells HUD from world sprites is game specific.
    WsTilemapProvider tilemap_provider = nullptr;
    WsBgXProvider bg_x_provider = nullptr;
    WsObjAttrXProvider obj_attr_x_provider = nullptr;

    // Values written while the view is expanded.
    unsigned bg_x_provider_layers = 0xFu;
    int authored_margin_layers = 1;
    int obj_native_clip = 0;
    // Black pillars (g_ws_pillarbox = 1) while the view is expanded but the context is not wide.
    bool pillarbox_outside_context = true;

    // Context at start-up and after configure().
    bool initial_wide_context = false;
    // Pull: returns -1 when unknown (keep the current value), 0 narrow, 1 wide. Called by update()
    // and by compute_effective_viewport().
    int (*poll_context)(void* user) = nullptr;
    // Push: called for every guest function entry once install_fn_entry_hook() ran. The host
    // decides there and calls set_wide_context(). Runs on the guest thread, so keep it cheap.
    void (*on_fn_entry)(uint32_t pc, void* user) = nullptr;

    // HudAnchoring bookkeeping. manage_hud = false leaves it alone. The offset used while the
    // presentation is not active is hud_default_offset (the HudAnchorSettings default).
    bool manage_hud = true;
    int hud_default_offset = 32;
    HudAnchoring* hud = nullptr;   // nullptr = HudAnchoring::instance()

    // Used by default_bg_x_provider(): BG layers (bit n = BG n) that never bleed into the margins
    // (dialogue windows, UI layers). Layers not in the mask render across the full wide view.
    unsigned centered_bg_layers = 0;
};

class Widescreen {
public:
    Widescreen() = default;
    // Shared default. The function-entry hook and the default_* providers always use this one.
    static Widescreen& instance();

    // Sets bindings and profile, resets the context to profile.initial_wide_context and the frame
    // counter to 0. Does not touch the runtime values: call install() or apply_policy() next.
    void configure(const WidescreenBindings& bindings, const WidescreenProfile& profile);
    const WidescreenProfile& profile() const { return profile_; }
    const WidescreenBindings& bindings() const { return bindings_; }

    // apply_policy() + install_fn_entry_hook(). Call once the view geometry is known (the runtime's
    // extended-view init callback) and it is safe to call again at any time.
    void install();
    // Per-frame refresh (the runtime's per-frame extended-view hook and every SDL poll):
    // poll_context, apply_policy, install_fn_entry_hook. Cheap and idempotent.
    void update();

    // Recomputes the policy values and the HudAnchoring state from (expanded?, wide context?).
    void apply_policy();

    // --- wide context ---------------------------------------------------------------------
    // Polls the profile (if any), applies the policy on a change, returns the current context.
    bool wide_context();
    // Last known context without polling.
    bool wide_context_cached() const { return context_.load(std::memory_order_relaxed); }
    // Sets the context and re-applies the policy.
    void set_wide_context(bool wide);
    // Frame counter value at the last switch to / from a wide context.
    uint64_t last_wide_frame() const { return last_wide_frame_.load(std::memory_order_relaxed); }
    uint64_t last_narrow_frame() const { return last_narrow_frame_.load(std::memory_order_relaxed); }

    // --- view queries (derived from the runtime's live values) ----------------------------
    bool view_active() const;
    bool presentation_active() const;
    // Real extra width per side while the presentation is active, else 0 / 0. Either pointer may be null.
    void effective_extra_pixels(int* left, int* right) const;
    // Raw extras of the expanded view regardless of context (0 when the view is not expanded).
    int extra_left() const;
    int extra_right() const;

    // --- presented frames -------------------------------------------------------------------
    void notify_present() { frame_counter_.fetch_add(1, std::memory_order_relaxed); }
    uint64_t frame_counter() const { return frame_counter_.load(std::memory_order_relaxed); }

    // --- viewport ---------------------------------------------------------------------------
    // Polls the context, then computes the placement (see widescreen_viewport()). Invalid
    // arguments (null rectangles, non-positive sizes) leave the outputs untouched.
    void compute_effective_viewport(int tex_w, int tex_h, int win_w, int win_h,
                                    SDL_Rect* out_src, SDL_Rect* out_dst,
                                    int* out_base_w, int* out_base_h);

    // --- function-entry hook ----------------------------------------------------------------
    // Chains this module in front of whatever hook the runtime slot holds (the previous one runs
    // first). Idempotent. Only the shared instance() can install it (the hook is a plain function
    // pointer); returns false for another instance, a missing slot or a missing on_fn_entry.
    bool install_fn_entry_hook();
    // Restores the previous hook if ours is still the current one. Returns true when restored.
    bool remove_fn_entry_hook();

private:
    friend void widescreen_fn_entry_trampoline(uint32_t);
    HudAnchoring& hud() const;

    WidescreenBindings bindings_;
    WidescreenProfile profile_;
    std::atomic<bool> context_{false};
    std::atomic<uint64_t> frame_counter_{0};
    std::atomic<uint64_t> last_wide_frame_{0};
    std::atomic<uint64_t> last_narrow_frame_{0};
    FnEntryHook prev_fn_entry_hook_ = nullptr;
};

// Pure placement math behind Widescreen::compute_effective_viewport().
//   Expanded texture (tex_w * 2 > tex_h * 3) and wide context: the whole texture, scaled
//     uniformly into the window (base = unscaled texture size).
//   Expanded texture and narrow context: the central 240x160 area only (3:2), scaled uniformly.
//   Native texture: the whole texture as 240x160, scaled uniformly.
// tex_h is assumed to be a multiple of 160 (integer prescale factor = tex_h / 160, at least 1).
void widescreen_viewport(bool wide_context, int tex_w, int tex_h, int win_w, int win_h,
                         SDL_Rect* out_src, SDL_Rect* out_dst, int* out_base_w, int* out_base_h);

// Same as Widescreen::instance().compute_effective_viewport(...): the signature of
// GameHostConfig::compute_viewport, so a host can assign it directly.
void widescreen_compute_viewport(int tex_w, int tex_h, int win_w, int win_h,
                                 SDL_Rect* out_src, SDL_Rect* out_dst,
                                 int* out_base_w, int* out_base_h);

// Signed sprite X of an OBJ attr1 (9-bit two's complement: 256..511 are -256..-1).
int widescreen_obj_x(uint16_t attr1);

// Ready-made providers that use Widescreen::instance() and its profile. Assign them to the profile
// when the generic behaviour is what the game needs.
//   default_tilemap_provider    keep the hardware-wrapped entry (kWsTilemapKeepWrapped) while the
//                               presentation is active, decline otherwise.
//   default_bg_x_provider       expanded view only. Narrow context: every margin column is
//                               suppressed (-1). Wide context: margin columns are suppressed only for
//                               the layers in profile.centered_bg_layers.
//   default_obj_attr_x_provider expanded view only: sprites parked at raw X >= 256 get their signed X.
int default_tilemap_provider(int bg, int hw_x, int screen_y, uint16_t* out_entry);
int default_bg_x_provider(int bg, int output_x, int screen_y, int* out_hw_x);
int default_obj_attr_x_provider(int oam_index, uint16_t attr0, uint16_t attr1, uint16_t attr2, int* out_x);

} // namespace gbaimp
