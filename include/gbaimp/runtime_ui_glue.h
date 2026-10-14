#pragma once
// Tuning of the runtime settings menu that recomp-ui's recomp_runtime_ui_create_standard()
// builds inside gbarecomp's run_game().
//
// Neither gbarecomp nor recomp-ui offers a host-side hook for the view-mode labels or the
// RESUME row, so the call itself is interposed:
//   GNU ld / clang   link with `-Wl,--wrap=recomp_runtime_ui_create_standard` and expand
//                    GBAIMP_RUNTIME_UI_WRAP() once;
//   MSVC             compile recomp-ui's recomp_runtime_settings.c with
//                    `-Drecomp_runtime_ui_create_standard=<prefix>_real_recomp_runtime_ui_create_standard`
//                    (per-source compile definition; no third-party file is edited) and expand
//                    GBAIMP_RUNTIME_UI_RENAME_FORWARDER(<that name>) once.
// Both paths run the same gbaimp::create_standard_ui(), so the menu is identical.
//
// This header is header-only and only meaningful in a host that builds recomp-ui: it needs
// "recomp_runtime_ui.h" on the include path and GBARECOMP_RUNTIME_UI defined. The library itself
// does not compile or link it.
#if defined(GBARECOMP_RUNTIME_UI)
#include "recomp_runtime_ui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gbaimp {

struct RuntimeUiGlueOptions {
    const char* tag = "gbaimp";                         // prefix of the diagnostic line
    const char* log_env = nullptr;                      // env var that enables the diagnostic line
    const char* native_view_label = "3:2 (Native)";
    const char* fixed_view_label = "16:9 (Widescreen)";
    bool remove_resume_row = true;
};

using RuntimeUiCreateFn = RecompRuntimeUi* (*)(const RecompRuntimeUiStandardConfig*);

// Applies the host's view-mode labels / feature set to a copy of `standard`, forwards to the
// original `real` constructor and stores the created menu in *active_out (when non-null).
inline RecompRuntimeUi* create_standard_ui(const RecompRuntimeUiStandardConfig* standard,
                                           RuntimeUiCreateFn real,
                                           const RuntimeUiGlueOptions& options,
                                           RecompRuntimeUi** active_out) {
    if (!standard) return nullptr;
    RecompRuntimeUiStandardConfig cfg = *standard;
    if (options.remove_resume_row) cfg.features &= ~static_cast<uint64_t>(RECOMP_RUNTIME_UI_STANDARD_RESUME);
    cfg.view_modes = RECOMP_RUNTIME_UI_VIEW_MODE_NATIVE | RECOMP_RUNTIME_UI_VIEW_MODE_FIXED_16_9;
    cfg.native_view_label = options.native_view_label;
    cfg.fixed_view_label = options.fixed_view_label;
    RecompRuntimeUi* ui = real(&cfg);
    if (active_out) *active_out = ui;
    const char* log = options.log_env ? std::getenv(options.log_env) : nullptr;
    if (log && *log && std::strcmp(log, "0") != 0) {
        std::fprintf(stderr, "[%s] recomp_runtime_ui_create_standard intercepted (menu=%p, view labels set%s)\n",
                     options.tag, static_cast<void*>(ui), options.remove_resume_row ? ", RESUME removed" : "");
    }
    return ui;
}

} // namespace gbaimp

// GNU ld / clang: define __wrap_recomp_runtime_ui_create_standard. `options_expr` is an expression
// of type gbaimp::RuntimeUiGlueOptions, `active_expr` an expression of type RecompRuntimeUi**
// (or nullptr). Expand at global scope, once, in one .cpp.
#if defined(__GNUC__) || defined(__clang__)
#define GBAIMP_RUNTIME_UI_WRAP(options_expr, active_expr)                                                   \
    extern "C" {                                                                                           \
    RecompRuntimeUi* __real_recomp_runtime_ui_create_standard(const RecompRuntimeUiStandardConfig*);       \
    RecompRuntimeUi* __wrap_recomp_runtime_ui_create_standard(const RecompRuntimeUiStandardConfig* s) {    \
        return gbaimp::create_standard_ui(s, &__real_recomp_runtime_ui_create_standard, (options_expr),    \
                                          (active_expr));                                                  \
    }                                                                                                      \
    }
#else
#define GBAIMP_RUNTIME_UI_WRAP(options_expr, active_expr)
#endif

// MSVC: define recomp_runtime_ui_create_standard, forwarding to the renamed original
// `real_name` (the symbol given to -Drecomp_runtime_ui_create_standard=... for recomp-ui's source).
#define GBAIMP_RUNTIME_UI_RENAME_FORWARDER(real_name, options_expr, active_expr)                           \
    extern "C" RecompRuntimeUi* real_name(const RecompRuntimeUiStandardConfig*);                           \
    extern "C" RecompRuntimeUi* recomp_runtime_ui_create_standard(const RecompRuntimeUiStandardConfig* s) {\
        return gbaimp::create_standard_ui(s, &real_name, (options_expr), (active_expr));                   \
    }

#endif // GBARECOMP_RUNTIME_UI
