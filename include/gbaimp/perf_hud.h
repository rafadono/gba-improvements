#pragma once

#include <cstdint>
#include <cstddef>

#include <SDL_rect.h>

struct SDL_Renderer;
struct SDL_Window;
union SDL_Event;

namespace gbaimp {

enum class PerfHudMode : int {
    Off = 0,
    FpsOnly = 1,
    FpsAndFrametime = 2,
    FullWithGraph = 3
};

enum class PerfHudPosition : int {
    TopLeft = 0,
    TopRight = 1,
    BottomLeft = 2,
    BottomRight = 3,
    BottomBlackBar = 4,
    FreeDrag = 5
};

enum class PerfHudTheme : int {
    GlassDark = 0,
    Neon = 1,
    Solid = 2,
    Minimal = 3
};

struct PerfHudSettings {
    PerfHudMode mode = PerfHudMode::FullWithGraph;
    PerfHudPosition position = PerfHudPosition::TopRight;
    PerfHudTheme theme = PerfHudTheme::GlassDark;
    float custom_x = 20.0f;
    float custom_y = 20.0f;
    bool show_target_line = true;
};

class PerfHud {
public:
    // Independently constructible (tests, multi-instance hosts); instance() stays the shared default.
    PerfHud();

    static PerfHud& instance();

    const PerfHudSettings& settings() const { return settings_; }
    PerfHudSettings& settings() { return settings_; }

    void set_mode(PerfHudMode mode);
    void set_position(PerfHudPosition pos);
    void set_theme(PerfHudTheme theme);
    void cycle_mode();

    bool handle_mouse_event(const SDL_Event& event);
    float current_width() const;
    float current_height() const;

    int last_window_width() const { return last_win_w_; }
    int last_window_height() const { return last_win_h_; }
    float calculated_x() const { return calculated_x_; }
    float calculated_y() const { return calculated_y_; }
    bool is_dragging() const { return is_dragging_; }
    bool is_hovered() const { return is_hovered_; }

    void on_frame_present(SDL_Renderer* renderer);

    // Rect (window pixels) where the game image is drawn; the HUD anchors to it.
    void set_game_viewport(const SDL_Rect& r) { game_viewport_ = r; }
    const SDL_Rect& game_viewport() const { return game_viewport_; }

private:

    void record_frame();
    void update_drag(int mouse_x, int mouse_y, bool mouse_down, float hud_w, float hud_h);
    void render_hud(SDL_Renderer* renderer, int win_w, int win_h, int vp_x, int vp_y, int vp_w, int vp_h);

    void draw_text(SDL_Renderer* renderer, int x, int y, const char* str, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int scale = 1);
    void draw_char(SDL_Renderer* renderer, int x, int y, char c, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int scale = 1);

    PerfHudSettings settings_;

    uint64_t perf_freq_ = 0;
    uint64_t last_counter_ = 0;
    uint64_t fps_window_start_ = 0;
    int fps_frame_count_ = 0;

    float current_fps_ = 60.0f;
    float current_frametime_ms_ = 16.67f;
    float min_frametime_ms_ = 16.67f;
    float max_frametime_ms_ = 16.67f;
    float avg_frametime_ms_ = 16.67f;

    static constexpr size_t kHistorySize = 120;
    float history_[kHistorySize] = {};
    size_t history_head_ = 0;

    bool is_dragging_ = false;
    bool is_hovered_ = false;
    float drag_offset_x_ = 0.0f;
    float drag_offset_y_ = 0.0f;
    float calculated_x_ = 20.0f;
    float calculated_y_ = 20.0f;
    int last_win_w_ = 1280;
    int last_win_h_ = 720;
    SDL_Window* last_window_ = nullptr;

    bool last_f10_state_ = false;
    SDL_Rect game_viewport_ = { 0, 0, 240, 160 };
};

} // namespace gbaimp
