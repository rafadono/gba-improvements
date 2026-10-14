#pragma once
// Declarative settings registry, "standard panel" and .ini persistence shared by every host.
//
// Every host used to carry the same ~800 lines: a table of menu items, a get/set/action callback
// chain keyed by strings, and a hand written .ini reader/writer. This module owns that code.
//
//   SettingsRegistry   an ordered list of `Setting` (id "section.key", menu section, label, tooltip,
//                      kind bool/int/choice/action/text, range, choices, .ini section/key, and the
//                      get/set/action callbacks), with get/set by id, change notification, and
//                      .ini save / load.
//   register_standard_settings()
//                      registers once every setting of the library's modules (screen filters, pixel
//                      art upscaler, frame interpolation, performance HUD, audio DSP, HD audio,
//                      gamepad / keyboard mapping, dialog turbo) with the menu ids and .ini keys the
//                      hosts have always used, so .ini files already on disk keep loading.
//
// The file name, the optional header and the section order are parameters. A host adds its own
// items and keys with SettingsRegistry::add() (any callbacks), IniExtension (free form lines, e.g.
// a font name) and add_ignored_ini_key() / add_ignored_id() (legacy keys that are accepted and
// dropped). Nothing here depends on gbarecomp or recomp-ui; the translation to recomp-ui items and
// gbarecomp::RunOptions lives in the header-only adapter "gbaimp/runtime_ui_settings.h".
//
// Minimal host:
//   gbaimp::SettingsRegistry reg;
//   reg.set_ini_path("mygame_config.ini");
//   reg.set_section_order({"Video", "Audio", "Controls", "Gameplay", "Diagnostics"});
//   gbaimp::register_standard_settings(reg);
//   reg.load_ini();                      // before the menu is shown; writes the defaults if no file
//   ... reg.set("video.screen_mask", 2);  // saves the file unless a load is in progress
//
// Threading: not thread safe; use it from the thread that runs the menu (the modules' own
// setters are what is safe against the audio thread).
#include <cstddef>
#include <functional>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace gbaimp {

class AudioDsp;
class DialogTurbo;
class FrameInterpolator;
class HdAudioPlayer;
class InputEnhancements;
class PerfHud;
class PixelArtUpscaler;
class ScreenFilters;

enum class SettingKind { Bool, Int, Choice, Action, Text };

struct Setting {
    std::string id;                    // "video.fps_target"; also the key of the runtime menu item
    // Menu. An empty `menu_section` (or in_menu == false) keeps the setting out of the menu.
    std::string menu_section;          // "Display and Graphics"
    std::string label;
    std::string tooltip;
    SettingKind kind = SettingKind::Int;
    int min = 0;                       // clamped on set() for Int / Choice (Bool is value != 0)
    int max = 1;
    int step = 1;
    std::vector<std::string> choices;  // Choice kind; max is normally choices.size() - 1
    bool in_menu = true;

    // .ini persistence. An empty `ini_key` (or ini_save == false) means the value is never written;
    // it is still read when present. `ini_aliases` are extra keys accepted on load only.
    std::string ini_section;           // "Video"
    std::string ini_key;               // "fps_target"
    std::vector<std::string> ini_aliases;
    bool ini_save = true;

    // Value access. Int / Bool / Choice use get/set, Action uses `action`, Text uses get_text/set_text.
    std::function<int()> get;
    std::function<void(int)> set;
    std::function<void()> action;
    std::function<bool(std::string&)> get_text;
    std::function<bool(const std::string&)> set_text;
    // Greyed out when it returns false (null = enabled).
    std::function<bool()> enabled;
    // Actions: write the .ini after running (the historical behaviour of the reset buttons).
    bool save_after_action = true;
};

// Free form lines of a host specific section (e.g. a font name stored by name). write() emits the
// lines of `section` (placed before the registered keys of that section); read() gets every key of
// the file that no registered setting claimed and returns true when it consumed it.
struct IniExtension {
    std::string section;
    std::function<void(std::ostream&)> write;
    std::function<bool(const std::string& key, const std::string& raw_value)> read;
};

struct IniLoadStats {
    bool file_found = false;
    int applied = 0;    // registered settings / extension keys set
    int ignored = 0;    // legacy keys accepted and dropped
    int unknown = 0;    // keys nobody claims, or values that are not integers
};

class SettingsRegistry {
public:
    // ---- registration ------------------------------------------------------------
    // Appends `s`, or replaces the setting with the same id in place (keeping its position).
    Setting& add(Setting s);
    bool remove(const std::string& id);
    Setting* find(const std::string& id);
    const Setting* find(const std::string& id) const;
    const std::vector<Setting>& settings() const { return settings_; }
    // Renames a menu section on every setting that uses it ("Display and Graphics" -> "Graphics").
    void rename_menu_section(const std::string& from, const std::string& to);
    // Ids that the menu may send but that no longer exist: set() accepts and drops them,
    // get() reports "not handled".
    void add_ignored_id(const std::string& id);

    // ---- value access (what the menu callbacks call) ------------------------------
    // Both return false for an unknown id, a kind mismatch or a missing callback.
    bool get(const std::string& id, int& value_out) const;
    // Clamps to [min, max] (Int / Choice), normalizes Bool to 0/1, runs the setting's setter, notifies
    // the change listener and, unless a load is in progress, writes the .ini (see set_autosave()).
    bool set(const std::string& id, int value);
    bool run_action(const std::string& id);
    bool get_text(const std::string& id, std::string& out) const;
    bool set_text(const std::string& id, const std::string& value);
    bool is_enabled(const std::string& id) const;   // unknown ids count as enabled

    // Called after every successful set()/run_action()/set_text() (also during a load).
    void set_change_listener(std::function<void(const Setting&)> fn) { listener_ = std::move(fn); }

    // ---- .ini persistence -----------------------------------------------------------
    // File written by save_ini() and read by load_ini(). Empty disables both (and autosave).
    void set_ini_path(std::string path) { ini_path_ = std::move(path); }
    const std::string& ini_path() const { return ini_path_; }
    // Comment block written at the top, each line prefixed with "# " (empty = no header, the
    // format the hosts have always written).
    void set_ini_header(std::string header) { ini_header_ = std::move(header); }
    // Sections are written in this order, then any other in order of first appearance.
    void set_section_order(std::vector<std::string> order) { section_order_ = std::move(order); }
    void add_ini_extension(IniExtension ext) { extensions_.push_back(std::move(ext)); }
    // Keys read and dropped without counting as unknown (legacy / retired settings).
    void add_ignored_ini_key(const std::string& key) { ignored_keys_.push_back(key); }
    // When true (default) set() and finished actions write the .ini unless loading or blocked.
    void set_autosave(bool on) { autosave_ = on; }
    // When it returns true nothing is written (e.g. a headless capture run that must not persist).
    void set_save_blocked(std::function<bool()> fn) { save_blocked_ = std::move(fn); }
    // Run at the start of load_ini() (e.g. load the keybinds file before presets are re-applied).
    void add_before_load(std::function<void()> fn) { before_load_.push_back(std::move(fn)); }
    // load_ini() with no file: write the current values (default) or do nothing.
    void set_write_defaults_if_missing(bool on) { write_defaults_ = on; }

    // Serializes every persisted setting: "[Section]\nkey=value\n...", sections separated by one
    // empty line.
    std::string ini_text() const;
    bool save_ini() const;                            // false when no path / blocked / not writable
    // Reads the file (if any), then `overrides` (same "key=value" lines, applied after the file),
    // applying values through set() with autosave suppressed. Lines that are empty, start with
    // '[', '#' or ';', or hold no '=' are skipped; the key is trimmed on the right; the value must
    // start with an integer (as std::stoi) or the line is counted unknown.
    IniLoadStats load_ini(const std::string& overrides = std::string());
    // Same parsing, from a string (no file access, no before-load hooks).
    IniLoadStats load_ini_text(const std::string& text);
    bool loading() const { return loading_; }

private:
    const Setting* by_ini_key(const std::string& key) const;
    void process_text(const std::string& text, IniLoadStats& st);
    void notify(const Setting& s) const { if (listener_) listener_(s); }
    void autosave_now() const;

    std::vector<Setting> settings_;
    std::vector<std::string> ignored_ids_;
    std::vector<std::string> ignored_keys_;
    std::vector<IniExtension> extensions_;
    std::vector<std::function<void()>> before_load_;
    std::function<void(const Setting&)> listener_;
    std::function<bool()> save_blocked_;
    std::vector<std::string> section_order_;
    std::string ini_path_;
    std::string ini_header_;
    bool autosave_ = true;
    bool write_defaults_ = true;
    bool loading_ = false;
};

// The library module instances a registry drives. Null members are skipped (their settings are not
// registered); shared() points them all at the module singletons.
struct SettingsTargets {
    AudioDsp* dsp = nullptr;
    HdAudioPlayer* hd_audio = nullptr;
    PerfHud* perf_hud = nullptr;
    FrameInterpolator* interpolator = nullptr;
    ScreenFilters* filters = nullptr;
    PixelArtUpscaler* upscaler = nullptr;
    InputEnhancements* input = nullptr;
    DialogTurbo* turbo = nullptr;

    static SettingsTargets shared();
};

struct StandardSettingsOptions {
    // Ids that must not be registered (e.g. a host without HD audio drops "audio.hd_enabled").
    std::vector<std::string> exclude;
    // Registers a before-load hook that reloads the keybinds file (as the hosts do ahead of the .ini).
    bool reload_keybinds_before_ini = true;
};

// Registers the standard panel. Ids, menu sections, .ini sections/keys:
//   Video       video.fps_target, video.motion_smoothing, video.xbrz_scale (ini xbrz_scale),
//               video.color_profile, video.screen_mask, video.mask_intensity   (menu "Display and Graphics")
//   Audio       audio.sample_rate, audio.hd_enabled, audio.master_volume, audio.bgm_volume,
//               audio.sfx_volume, audio.eq_preset, audio.stereo_width, audio.anti_aliasing,
//               audio.limiter                                                   (menu "Audio")
//   Controls    input.analog_mode, input.deadzone (ini analog_deadzone), input.gamepad_profile,
//               input.pad_attack / pad_jump / pad_deck_l / pad_deck_r (GBA A, B, L, R),
//               input.reset_pad                                                 (menu "Controller (Gamepad)")
//               input.kb_preset, input.key_a|b|l|r|start|select|up|down|left|right (text, stored in
//               the keybinds file, not the .ini), input.reset_kb                (menu "Keyboard Controls")
//               input.reset_controls (action without a menu row)
//   Gameplay    gameplay.turbo_dialog                                           (menu "Gameplay & Assist")
//   Diagnostics perf.mode (perf_mode), perf.position, perf.theme, hud_custom_x, hud_custom_y (ini only)
//                                                                               (menu "Diagnostics & HUD")
// The legacy ini key "hud_anchoring" is accepted and ignored. Labels are generic; a host that wants
// its own wording edits the Setting returned by find() or calls rename_menu_section().
void register_standard_settings(SettingsRegistry& reg, const SettingsTargets& targets,
                                const StandardSettingsOptions& options = StandardSettingsOptions());
void register_standard_settings(SettingsRegistry& reg);   // SettingsTargets::shared(), default options

} // namespace gbaimp
