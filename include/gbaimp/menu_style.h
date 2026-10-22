#pragma once
// One menu vocabulary for every host: the wording, section names and section order of the reference
// port, applied over register_standard_settings() so a player sees the same menu in every game.
//
//   registry:  gbaimp::register_standard_settings(reg);
//              gbaimp::apply_reference_menu_style(reg);     // wording + "Graphics" + menu order
//              // ... host rows, then (once everything is registered):
//              gbaimp::order_menu_sections(reg, gbaimp::reference_menu_section_order());
//
// Only game independent rows are covered. Rows whose meaning depends on the game (the labels of the
// four GBA buttons, the controller preset names, the HD soundtrack row) are left to the host, which
// edits them through SettingsRegistry::find() or apply_menu_text() with its own table.
// Nothing here depends on gbarecomp or recomp-ui.
#include <string>
#include <vector>

#include "gbaimp/host_settings.h"

namespace gbaimp {

// Wording of one menu row. Empty strings keep the current value. `choices` (Choice rows) replaces the
// list and sets the range to 0..size-1. `has_range` applies min/max/step to Int and Choice rows.
struct MenuTextRow {
    std::string id;
    std::string label;
    std::string tooltip;
    std::vector<std::string> choices;
    bool has_range = false;
    int min = 0;
    int max = 0;
    int step = 1;
};

// Applies `rows` to the registered settings with those ids (unknown ids are skipped, so a host that
// excluded a row can still use the shared table). Returns the number of settings changed.
int apply_menu_text(SettingsRegistry& reg, const std::vector<MenuTextRow>& rows);

// The shared wording: video.*, input.analog_mode, input.deadzone, input.kb_preset, gameplay.turbo_dialog,
// audio.hd_enabled / bgm_volume / sfx_volume / eq_preset / stereo_width / anti_aliasing / limiter, perf.*.
const std::vector<MenuTextRow>& reference_menu_text();

// Menu section order: Graphics, Controller (Gamepad), Keyboard Controls, Gameplay & Assist, Audio,
// Diagnostics & HUD. A host inserts its own sections with insert_section(order, name, "Audio").
const std::vector<std::string>& reference_menu_section_order();

// .ini section order shared by the hosts: Video, Audio, Controls, Gameplay, Diagnostics. A host with
// its own sections inserts them with insert_section().
const std::vector<std::string>& reference_ini_section_order();

// Returns `order` (a menu or .ini section list) with `section` inserted before `before` (appended when
// `before` is not present): host menu sections go before "Audio", host .ini sections before "Controls".
std::vector<std::string> insert_section(std::vector<std::string> order, const std::string& section,
                                            const std::string& before);

// Renames the standard "Display and Graphics" section to "Graphics" and applies reference_menu_text().
void apply_reference_menu_style(SettingsRegistry& reg);

// Reorders the registry so menu sections follow `order` (sections not listed go after the listed ones,
// in their current order). Within a section the registration order, which is also the key order of
// the .ini, is kept. Settings without a menu row follow the last menu section that shares their .ini
// section (the dragged HUD position stays with Diagnostics), or go last.
void order_menu_sections(SettingsRegistry& reg, const std::vector<std::string>& order);

} // namespace gbaimp
