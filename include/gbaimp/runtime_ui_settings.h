#pragma once
// Adapter between gbaimp::SettingsRegistry and recomp-ui / gbarecomp::RunOptions.
//
// Header-only and only meaningful in a host that builds recomp-ui: it needs "recomp_runtime_ui.h"
// on the include path and GBARECOMP_RUNTIME_UI defined. The library itself does not compile or link
// it, and it does not include gbarecomp's runtime.h: RuntimeUiBinding::apply() is a template over
// the host's gbarecomp::RunOptions (the host includes "runtime.h" itself).
//
// What a host does:
//   static gbaimp::SettingsRegistry g_settings;            // see host_settings.h
//   static gbaimp::RuntimeUiBinding g_ui(g_settings);
//   ... register_standard_settings(g_settings); add host items; g_settings.load_ini();
//   g_ui.rebuild();                                         // after the last item / choice change
//   g_ui.apply(opts);                                       // fills opts.ui_extra_items / ui_get / ui_set /
//                                                           //   ui_action / ui_enabled / ui_get_text / ui_set_text
//   // and gbaimp::runtime_menu_toggle(g_active_runtime_ui) for the open / close / toggle helpers.
// The binding (and the registry it points at) must outlive run_game(): recomp-ui keeps pointers into
// the item array and the strings. The RunOptions callbacks are plain C function pointers, so only one
// binding can be active per process (the last constructed / make_active() one).
#if defined(GBARECOMP_RUNTIME_UI)
#include "gbaimp/host_settings.h"
#include "recomp_runtime_ui.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace gbaimp {

class RuntimeUiBinding {
public:
    explicit RuntimeUiBinding(SettingsRegistry& registry) : registry_(&registry) { make_active(); }

    // The menu row that is greyed out by default (the host's own pause menu provides it).
    void set_disabled_item(const char* key) { disabled_key_ = key ? key : ""; }

    // Builds the recomp-ui item array from the registry (menu settings only, in registration
    // order). Call again after changing choices (e.g. a dynamic font list), before apply().
    void rebuild() {
        items_.clear();
        choice_ptrs_.clear();
        std::vector<const Setting*> shown;
        for (const Setting& s : registry_->settings()) {
            if (s.in_menu && !s.menu_section.empty()) shown.push_back(&s);
        }
        std::size_t total = 0;
        for (const Setting* s : shown) total += s->choices.size();
        choice_ptrs_.reserve(total + 1);
        items_.reserve(shown.size());
        std::vector<std::pair<std::size_t, std::size_t>> choice_first;
        for (const Setting* s : shown) {
            RecompRuntimeUiItem it{};
            it.key = s->id.c_str();
            it.section = s->menu_section.c_str();
            it.label = s->label.c_str();
            it.description = s->tooltip.c_str();
            switch (s->kind) {
                case SettingKind::Bool: it.type = RECOMP_RUNTIME_UI_BOOL; break;
                case SettingKind::Int: it.type = RECOMP_RUNTIME_UI_INT; break;
                case SettingKind::Choice: it.type = RECOMP_RUNTIME_UI_CHOICE; break;
                case SettingKind::Action: it.type = RECOMP_RUNTIME_UI_ACTION; break;
                case SettingKind::Text: it.type = RECOMP_RUNTIME_UI_TEXT; break;
            }
            if (s->kind == SettingKind::Action || s->kind == SettingKind::Text) {
                it.minimum = it.maximum = it.step = 0;
            } else {
                it.minimum = s->min; it.maximum = s->max; it.step = s->step;
            }
            if (s->kind == SettingKind::Choice) {
                const std::size_t first = choice_ptrs_.size();
                for (const std::string& c : s->choices) choice_ptrs_.push_back(c.c_str());
                it.choice_count = s->choices.size();
                choice_first.push_back({items_.size(), first});
            }
            items_.push_back(it);
        }
        // Pointers into choice_ptrs_ are taken only now, when it can no longer reallocate.
        for (const auto& cf : choice_first) items_[cf.first].choices = choice_ptrs_.data() + cf.second;
    }

    const RecompRuntimeUiItem* items() const { return items_.data(); }
    std::size_t item_count() const { return items_.size(); }

    void make_active() { active() = this; }

    // Fills the ui_* fields of the host's gbarecomp::RunOptions.
    template <class RunOptions>
    void apply(RunOptions& opts) {
        make_active();
        opts.ui_extra_items = items();
        opts.ui_extra_item_count = item_count();
        opts.ui_get = &ui_get;
        opts.ui_set = &ui_set;
        opts.ui_action = &ui_action;
        opts.ui_enabled = &ui_enabled;
        opts.ui_get_text = &ui_get_text;
        opts.ui_set_text = &ui_set_text;
    }

    // The C callbacks (also usable directly).
    static int ui_get(const char* key, int* value_out) {
        if (!key || !value_out || !active()) return 0;
        return active()->registry_->get(key, *value_out) ? 1 : 0;
    }
    static int ui_set(const char* key, int value) {
        if (!key || !active()) return 0;
        return active()->registry_->set(key, value) ? 1 : 0;
    }
    static int ui_action(const char* key) {
        if (!key || !active()) return 0;
        return active()->registry_->run_action(key) ? 1 : 0;
    }
    static int ui_enabled(const char* key) {
        if (!key || !active()) return 1;
        if (!active()->disabled_key_.empty() && active()->disabled_key_ == key) return 0;
        return active()->registry_->is_enabled(key) ? 1 : 0;
    }
    static int ui_get_text(const char* key, char* buf, std::size_t buf_len) {
        if (!key || !buf || buf_len == 0 || !active()) return 0;
        std::string v;
        if (!active()->registry_->get_text(key, v)) return 0;
        std::snprintf(buf, buf_len, "%s", v.c_str());
        return 1;
    }
    static int ui_set_text(const char* key, const char* value) {
        if (!key || !value || !active()) return 0;
        return active()->registry_->set_text(key, value) ? 1 : 0;
    }

private:
    static RuntimeUiBinding*& active() { static RuntimeUiBinding* p = nullptr; return p; }

    SettingsRegistry* registry_;
    std::string disabled_key_ = "system.resume";
    std::vector<RecompRuntimeUiItem> items_;
    std::vector<const char*> choice_ptrs_;
};

// Open / closed state of the runtime menu created by recomp_runtime_ui_create_standard() (the pointer
// the host keeps from runtime_ui_glue.h's RuntimeUiGlueOptions / active_out). All accept nullptr.
inline bool runtime_menu_is_open(RecompRuntimeUi* ui) { return ui && recomp_runtime_ui_is_open(ui); }
inline void runtime_menu_open(RecompRuntimeUi* ui) {
    if (ui && !recomp_runtime_ui_is_open(ui)) recomp_runtime_ui_open(ui);
}
inline void runtime_menu_close(RecompRuntimeUi* ui) {
    if (ui && recomp_runtime_ui_is_open(ui)) recomp_runtime_ui_close(ui);
}
inline void runtime_menu_toggle(RecompRuntimeUi* ui) {
    if (!ui) return;
    if (recomp_runtime_ui_is_open(ui)) recomp_runtime_ui_close(ui); else recomp_runtime_ui_open(ui);
}

} // namespace gbaimp

#endif // GBARECOMP_RUNTIME_UI
