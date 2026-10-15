#pragma once
// Scrollable text-history sidebar ("conversation log" / "backlog") for any SDL host.
//
// A TextHistory keeps a bounded list of entries {speaker, text, timestamp}, draws them as a
// translucent panel docked to the left edge of the window (dimmed backdrop, header, word-wrapped
// entries, scroll bar) with a built-in 5x7 bitmap font, and handles the hotkeys, scrolling and the
// input blocking while it is open. It knows nothing about any game: the host decides what an entry
// is (it reads its own game memory, a script engine, ...) and calls push_entry().
//
// Host adoption:
//     gbaimp::TextHistory& log = gbaimp::TextHistory::instance();     // or an own object
//     log.config().style.title = "STORY LOG";                          // optional look / keys
//     log.push_entry("Speaker", "Line of text");                       // from the host's observer
//     // SDL event handler (gbaimp::SdlHandlers::on_event or the host's own):
//     if (log.handle_event(ev)) return true;                           // swallowed while open
//     // after the game image, before the present:
//     log.render_sidebar(renderer, window_w, window_h);
//     // menu: gbaimp::register_text_history_settings(registry, log);   // optional
// The library needs no gbarecomp header. Not thread safe: use it from the SDL thread.
#include <SDL_gamecontroller.h>
#include <SDL_keycode.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

struct SDL_Renderer;
union SDL_Event;

namespace gbaimp {

class SettingsRegistry;

struct TextHistoryEntry {
    std::string speaker;
    std::string text;
    std::string timestamp;   // "HH:MM:SS" local time unless a timestamp source is set
};

struct TextHistoryColor {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

// Hotkeys. Empty lists disable that action. Values are SDL key codes (SDLK_*), SDL game controller
// buttons (SDL_CONTROLLER_BUTTON_*) and raw joystick button numbers (the fallback for devices SDL
// has no controller mapping for).
struct TextHistoryControls {
    std::vector<int> toggle_keys{SDLK_F2};
    std::vector<int> close_keys{SDLK_ESCAPE};            // only while open
    std::vector<int> scroll_up_keys{SDLK_UP};
    std::vector<int> scroll_down_keys{SDLK_DOWN};
    std::vector<int> page_up_keys{SDLK_PAGEUP};
    std::vector<int> page_down_keys{SDLK_PAGEDOWN};
    std::vector<int> toggle_buttons{SDL_CONTROLLER_BUTTON_BACK};
    std::vector<int> close_buttons{SDL_CONTROLLER_BUTTON_B};
    std::vector<int> scroll_up_buttons{SDL_CONTROLLER_BUTTON_DPAD_UP};
    std::vector<int> scroll_down_buttons{SDL_CONTROLLER_BUTTON_DPAD_DOWN};
    std::vector<int> toggle_joy_buttons{4, 6, 8};
    std::vector<int> close_joy_buttons{1};
    int line_step = 36;    // pixels per arrow press / controller step
    int page_step = 180;   // pixels per page key
    int wheel_step = 48;   // pixels per mouse wheel notch
    bool close_on_outside_click = true;   // left click right of the panel closes it
};

// Look and texts. The defaults are a dark panel with a sky-blue accent and English captions.
struct TextHistoryStyle {
    // Panel width: percent of the window width, clamped to [bar_min_width, bar_max_width].
    int bar_width_percent = 40;
    int bar_min_width = 380;
    int bar_max_width = 600;
    int header_height = 56;
    int large_font_min_window_height = 900;   // windows at least this tall use the 2x font

    // Captions (the bitmap font has ASCII 32..126 only; other characters draw as spaces).
    std::string title = "CONVERSATION LOG";
    std::string close_hint = "[ESC/B] CLOSE";
    std::string scroll_hint = "MOUSE WHEEL / ARROWS / DPAD TO SCROLL";
    std::string empty_title = "No dialogue recorded yet.";
    std::string empty_hint = "Advance conversations to read back story lines.";

    TextHistoryColor backdrop{0, 0, 0, 130};
    TextHistoryColor panel{10, 15, 26, 240};
    TextHistoryColor accent{56, 189, 248, 220};          // right border
    TextHistoryColor accent_shadow{14, 116, 144, 120};
    TextHistoryColor header_bg{15, 23, 42, 250};
    TextHistoryColor header_line{51, 65, 85, 200};
    TextHistoryColor title_color{56, 189, 248, 255};
    TextHistoryColor close_hint_color{148, 163, 184, 220};
    TextHistoryColor scroll_hint_color{100, 116, 139, 200};
    TextHistoryColor empty_title_color{148, 163, 184, 255};
    TextHistoryColor text_color{241, 245, 249, 255};
    TextHistoryColor timestamp_color{100, 116, 139, 180};
    TextHistoryColor separator{30, 41, 59, 140};
    TextHistoryColor scrollbar_track{51, 65, 85, 180};
    TextHistoryColor scrollbar_thumb{56, 189, 248, 240};
    TextHistoryColor default_speaker_color{251, 191, 36, 255};
    // Exact speaker name -> name color (everything else uses default_speaker_color).
    std::vector<std::pair<std::string, TextHistoryColor>> speaker_colors;
};

struct TextHistoryConfig {
    TextHistoryControls controls;
    TextHistoryStyle style;
    size_t max_entries = 200;   // oldest entries are dropped beyond this (0 = unlimited)
    int dedup_window = 3;       // push_entry ignores an entry equal to one of the last N (0 = off)
};

class TextHistory {
public:
    TextHistory() = default;   // independent instances are fine (tests, multi-window hosts)
    static TextHistory& instance();

    TextHistoryConfig& config() { return cfg_; }
    const TextHistoryConfig& config() const { return cfg_; }

    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enable) { enabled_ = enable; }   // disabled: no hotkeys, no drawing

    bool is_open() const { return open_; }
    void toggle_open();               // no-op while disabled
    void set_open(bool open);         // opening scrolls to the newest entry

    // Appends an entry. Returns false when ignored (empty text, or a duplicate inside the dedup
    // window). The timestamp is taken from the timestamp source (default: local time HH:MM:SS).
    bool push_entry(const std::string& speaker, const std::string& text);
    bool push_entry(const std::string& speaker, const std::string& text, const std::string& timestamp);
    void clear();                     // entries and scroll position
    const std::vector<TextHistoryEntry>& entries() const { return entries_; }
    void set_timestamp_source(std::function<std::string()> fn) { timestamp_fn_ = std::move(fn); }

    // Hotkeys and scrolling. Returns true when the event must not reach the game. While the panel
    // is open every keyboard, controller, joystick and mouse event is swallowed (SDL_QUIT and
    // SDL_APP_TERMINATING never are); while it is closed only the toggle events are.
    bool handle_event(const SDL_Event& event);

    // Draws the dim backdrop and the sidebar in raw window pixels (it resets the renderer's logical
    // size, viewport and scale, as a present-time overlay needs). No-op while closed or disabled.
    void render_sidebar(SDL_Renderer* renderer, int win_w, int win_h);

    // Scroll state, in pixels. max_scroll() is measured by the last render_sidebar() (0 before it).
    int scroll_offset() const { return scroll_; }
    int max_scroll() const { return max_scroll_; }
    void scroll_by(int pixels);
    // Panel width the last render used (0 before the first render); bar_width_for() computes it.
    int bar_width() const { return bar_w_; }
    int bar_width_for(int win_w) const;

private:
    bool push_impl(const std::string& speaker, const std::string& text, const std::string* timestamp);
    void draw_text(SDL_Renderer* r, int x, int y, const std::string& s, const TextHistoryColor& c, int scale) const;

    TextHistoryConfig cfg_;
    bool enabled_ = true;
    bool open_ = false;
    int scroll_ = 0;
    int max_scroll_ = 0;
    int bar_w_ = 0;
    bool stick_to_end_ = false;   // set by set_open(true): the next render lands on the newest entry
    std::vector<TextHistoryEntry> entries_;
    std::function<std::string()> timestamp_fn_;
};

// Greedy word wrap for a fixed-advance font: words are separated by whitespace (newlines and runs
// of spaces collapse), a word wider than the line is never split. Used by the sidebar; public so
// hosts and tests can predict the layout.
std::vector<std::string> wrap_text_fixed_advance(const std::string& text, int max_pixel_w, int char_advance);

// Names, labels and .ini placement of the two settings register_text_history_settings() adds.
// The defaults are the ids and .ini keys hosts already use for this feature.
struct TextHistorySettingsOptions {
    std::string enable_id = "dialogue.backlog_enable";
    std::string open_id = "dialogue.open_backlog";
    std::string menu_section = "Dialogue & Story";
    std::string enable_label = "Conversation Log (Backlog)";
    std::string enable_tooltip = "Translucent story history (press F2 or the gamepad Back button)";
    std::string open_label = "Open Conversation Log";
    std::string open_tooltip = "View transcript and story history";
    std::string ini_section = "Dialogue";
    std::string ini_key = "backlog_enable";
};

// Adds a Bool "enable" setting (get/set = TextHistory::is_enabled / set_enabled, saved to the .ini)
// and an Action "open" setting (set_open(true), no .ini write). Opt in: register_standard_settings()
// does not call it. `history` must outlive the registry.
void register_text_history_settings(SettingsRegistry& reg, TextHistory& history,
                                    const TextHistorySettingsOptions& options = TextHistorySettingsOptions());

} // namespace gbaimp
