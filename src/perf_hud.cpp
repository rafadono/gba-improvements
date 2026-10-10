#include "gbaimp/perf_hud.h"
#include "gbaimp/frame_interpolator.h"
#include "gbaimp/screen_filters.h"
#include "gbaimp/hooks.h"
#include <SDL.h>
#ifdef SDL_RenderPresent
#undef SDL_RenderPresent
#endif
#ifdef SDL_PollEvent
#undef SDL_PollEvent
#endif
#ifdef SDL_UpdateTexture
#undef SDL_UpdateTexture
#endif
#ifdef SDL_RenderCopy
#undef SDL_RenderCopy
#endif

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>

namespace gbaimp {

namespace {

// 5x7 ASCII font (from ASCII 32 to 126)
const uint8_t kFont5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // '%'
    {0x36, 0x49, 0x55, 0x22, 0x50}, // '&'
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '\''
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // ')'
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // '+'
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // '-'
    {0x00, 0x60, 0x60, 0x00, 0x00}, // '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // '1'
    {0x42, 0x61, 0x51, 0x49, 0x46}, // '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // '6'
    {0x01, 0x71, 0x09, 0x05, 0x03}, // '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // '9'
    {0x00, 0x36, 0x36, 0x00, 0x00}, // ':'
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ';'
    {0x08, 0x14, 0x22, 0x41, 0x00}, // '<'
    {0x14, 0x14, 0x14, 0x14, 0x14}, // '='
    {0x00, 0x41, 0x22, 0x14, 0x08}, // '>'
    {0x02, 0x01, 0x51, 0x09, 0x06}, // '?'
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // '@'
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 'F'
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 'L'
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 'R'
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 'V'
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 'X'
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 'Z'
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // '['
    {0x02, 0x04, 0x08, 0x10, 0x20}, // '\'
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // ']'
    {0x04, 0x02, 0x01, 0x02, 0x04}, // '^'
    {0x40, 0x40, 0x40, 0x40, 0x40}, // '_'
    {0x00, 0x01, 0x02, 0x04, 0x00}, // '`'
    {0x20, 0x54, 0x54, 0x54, 0x78}, // 'a'
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // 'b'
    {0x38, 0x44, 0x44, 0x44, 0x20}, // 'c'
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // 'd'
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 'e'
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // 'f'
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 'g'
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 'h'
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 'i'
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // 'j'
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // 'k'
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 'l'
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // 'm'
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 'n'
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 'o'
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // 'p'
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // 'q'
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 'r'
    {0x48, 0x54, 0x54, 0x54, 0x20}, // 's'
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // 't'
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 'u'
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 'v'
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 'w'
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 'x'
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 'y'
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // 'z'
    {0x00, 0x08, 0x36, 0x41, 0x00}, // '{'
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // '|'
    {0x00, 0x41, 0x36, 0x08, 0x00}, // '}'
    {0x08, 0x08, 0x2A, 0x1C, 0x08}  // '~'
};

} // namespace

PerfHud& PerfHud::instance() {
    static PerfHud s_instance;
    return s_instance;
}

PerfHud::PerfHud() {
    perf_freq_ = SDL_GetPerformanceFrequency();
    last_counter_ = SDL_GetPerformanceCounter();
    fps_window_start_ = last_counter_;
    for (size_t i = 0; i < kHistorySize; ++i) {
        history_[i] = 16.67f;
    }
}

void PerfHud::set_mode(PerfHudMode mode) {
    settings_.mode = mode;
}

void PerfHud::set_position(PerfHudPosition pos) {
    settings_.position = pos;
    float hud_w = current_width();
    float hud_h = current_height();
    int win_w = last_win_w_ > 0 ? last_win_w_ : 1280;
    int win_h = last_win_h_ > 0 ? last_win_h_ : 720;
    switch (pos) {
        case PerfHudPosition::TopLeft:
            settings_.custom_x = 16.0f;
            settings_.custom_y = 16.0f;
            break;
        case PerfHudPosition::TopRight:
            settings_.custom_x = std::max(0.0f, static_cast<float>(win_w) - hud_w - 16.0f);
            settings_.custom_y = 16.0f;
            break;
        case PerfHudPosition::BottomLeft:
            settings_.custom_x = 16.0f;
            settings_.custom_y = std::max(0.0f, static_cast<float>(win_h) - hud_h - 16.0f);
            break;
        case PerfHudPosition::BottomRight:
            settings_.custom_x = std::max(0.0f, static_cast<float>(win_w) - hud_w - 16.0f);
            settings_.custom_y = std::max(0.0f, static_cast<float>(win_h) - hud_h - 16.0f);
            break;
        case PerfHudPosition::BottomBlackBar:
            settings_.custom_x = std::max(0.0f, (static_cast<float>(win_w) - hud_w) * 0.5f);
            settings_.custom_y = std::max(0.0f, static_cast<float>(win_h) - hud_h - 16.0f);
            break;
        case PerfHudPosition::FreeDrag:
        default:
            break;
    }
    calculated_x_ = settings_.custom_x;
    calculated_y_ = settings_.custom_y;
}

float PerfHud::current_width() const {
    if (settings_.mode == PerfHudMode::FpsOnly) {
        return 88.0f;
    } else if (settings_.mode == PerfHudMode::FpsAndFrametime) {
        return 164.0f;
    } else if (settings_.mode == PerfHudMode::FullWithGraph) {
        return 200.0f;
    }
    return 170.0f;
}

float PerfHud::current_height() const {
    if (settings_.mode == PerfHudMode::FpsOnly) {
        return 24.0f;
    } else if (settings_.mode == PerfHudMode::FpsAndFrametime) {
        return 44.0f;
    } else if (settings_.mode == PerfHudMode::FullWithGraph) {
        return 88.0f;
    }
    return 36.0f;
}

bool PerfHud::handle_mouse_event(const SDL_Event& event) {
    if (settings_.mode == PerfHudMode::Off) {
        is_hovered_ = false;
        is_dragging_ = false;
        return false;
    }

    float hud_w = current_width();
    float hud_h = current_height();
    int cur_w = last_win_w_ > 0 ? last_win_w_ : 1280;
    int cur_h = last_win_h_ > 0 ? last_win_h_ : 720;

    auto to_render_coords = [&](int in_x, int in_y, Uint32 windowID, int& out_x, int& out_y) {
        SDL_Window* win = SDL_GetWindowFromID(windowID);
        if (!win && last_window_) win = last_window_;
        if (win) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(win, &ww, &wh);
            if (ww > 0 && wh > 0) {
                out_x = (in_x * cur_w) / ww;
                out_y = (in_y * cur_h) / wh;
                return;
            }
        }
        out_x = in_x;
        out_y = in_y;
    };

    if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (event.button.button == SDL_BUTTON_LEFT) {
            int mx = 0, my = 0;
            to_render_coords(event.button.x, event.button.y, event.button.windowID, mx, my);

            if (mx >= calculated_x_ && mx <= calculated_x_ + hud_w &&
                my >= calculated_y_ && my <= calculated_y_ + hud_h) {
                is_dragging_ = true;
                drag_offset_x_ = static_cast<float>(mx) - calculated_x_;
                drag_offset_y_ = static_cast<float>(my) - calculated_y_;
                settings_.position = PerfHudPosition::FreeDrag;
                settings_.custom_x = calculated_x_;
                settings_.custom_y = calculated_y_;
                SDL_ShowCursor(SDL_ENABLE);
                return true;
            }
        }
    } else if (event.type == SDL_MOUSEMOTION) {
        int mx = 0, my = 0;
        to_render_coords(event.motion.x, event.motion.y, event.motion.windowID, mx, my);

        is_hovered_ = (mx >= calculated_x_ && mx <= calculated_x_ + hud_w &&
                       my >= calculated_y_ && my <= calculated_y_ + hud_h);
        if (is_hovered_) {
            SDL_ShowCursor(SDL_ENABLE);
        }

        if (is_dragging_) {
            settings_.custom_x = std::clamp(static_cast<float>(mx) - drag_offset_x_, 0.0f, static_cast<float>(cur_w - hud_w));
            settings_.custom_y = std::clamp(static_cast<float>(my) - drag_offset_y_, 0.0f, static_cast<float>(cur_h - hud_h));
            calculated_x_ = settings_.custom_x;
            calculated_y_ = settings_.custom_y;
            return true;
        }
    } else if (event.type == SDL_MOUSEBUTTONUP) {
        if (event.button.button == SDL_BUTTON_LEFT && is_dragging_) {
            is_dragging_ = false;
            notify_settings_changed();
            return true;
        }
    }

    return false;
}

void PerfHud::set_theme(PerfHudTheme theme) {
    settings_.theme = theme;
}

void PerfHud::cycle_mode() {
    int next = static_cast<int>(settings_.mode) + 1;
    if (next > static_cast<int>(PerfHudMode::FullWithGraph)) {
        next = 0;
    }
    settings_.mode = static_cast<PerfHudMode>(next);
}

void PerfHud::record_frame() {
    const uint64_t now = SDL_GetPerformanceCounter();
    if (perf_freq_ == 0) perf_freq_ = SDL_GetPerformanceFrequency();

    if (last_counter_ > 0 && perf_freq_ > 0) {
        const double delta_sec = static_cast<double>(now - last_counter_) / static_cast<double>(perf_freq_);
        current_frametime_ms_ = static_cast<float>(delta_sec * 1000.0);
    }
    last_counter_ = now;

    history_[history_head_] = current_frametime_ms_;
    history_head_ = (history_head_ + 1) % kHistorySize;

    ++fps_frame_count_;
    const double window_span = static_cast<double>(now - fps_window_start_) / static_cast<double>(perf_freq_);
    if (window_span >= 0.3) {
        current_fps_ = static_cast<float>(fps_frame_count_ / window_span);
        fps_frame_count_ = 0;
        fps_window_start_ = now;

        float sum = 0.0f;
        min_frametime_ms_ = 999.0f;
        max_frametime_ms_ = 0.0f;
        for (size_t i = 0; i < kHistorySize; ++i) {
            float val = history_[i];
            sum += val;
            if (val < min_frametime_ms_) min_frametime_ms_ = val;
            if (val > max_frametime_ms_) max_frametime_ms_ = val;
        }
        avg_frametime_ms_ = sum / static_cast<float>(kHistorySize);
    }
}

void PerfHud::draw_char(SDL_Renderer* renderer, int x, int y, char c, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int scale) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t* col_data = kFont5x7[c - 32];

    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    for (int col = 0; col < 5; ++col) {
        uint8_t bits = col_data[col];
        for (int row = 0; row < 7; ++row) {
            if ((bits >> row) & 1) {
                if (scale == 1) {
                    SDL_RenderDrawPoint(renderer, x + col, y + row);
                } else {
                    SDL_Rect pixel_rect = { x + col * scale, y + row * scale, scale, scale };
                    SDL_RenderFillRect(renderer, &pixel_rect);
                }
            }
        }
    }
}

void PerfHud::draw_text(SDL_Renderer* renderer, int x, int y, const char* str, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int scale) {
    if (!str) return;
    int cur_x = x;
    const int char_step = 6 * scale;
    while (*str) {
        draw_char(renderer, cur_x, y, *str, r, g, b, a, scale);
        cur_x += char_step;
        ++str;
    }
}

void PerfHud::update_drag(int mouse_x, int mouse_y, bool mouse_down, float hud_w, float hud_h) {
    int cur_w = last_win_w_ > 0 ? last_win_w_ : 1280;
    int cur_h = last_win_h_ > 0 ? last_win_h_ : 720;
    if (mouse_down) {
        if (!is_dragging_) {
            if (mouse_x >= calculated_x_ && mouse_x <= calculated_x_ + hud_w &&
                mouse_y >= calculated_y_ && mouse_y <= calculated_y_ + hud_h) {
                is_dragging_ = true;
                drag_offset_x_ = static_cast<float>(mouse_x) - calculated_x_;
                drag_offset_y_ = static_cast<float>(mouse_y) - calculated_y_;
                settings_.position = PerfHudPosition::FreeDrag;
                settings_.custom_x = calculated_x_;
                settings_.custom_y = calculated_y_;
                SDL_ShowCursor(SDL_ENABLE);
            }
        } else {
            settings_.custom_x = std::clamp(static_cast<float>(mouse_x) - drag_offset_x_, 0.0f, static_cast<float>(cur_w - hud_w));
            settings_.custom_y = std::clamp(static_cast<float>(mouse_y) - drag_offset_y_, 0.0f, static_cast<float>(cur_h - hud_h));
            calculated_x_ = settings_.custom_x;
            calculated_y_ = settings_.custom_y;
        }
    } else {
        if (is_dragging_) {
            is_dragging_ = false;
            notify_settings_changed();
        }
    }
}

void PerfHud::render_hud(SDL_Renderer* renderer, int win_w, int win_h, int vp_x, int vp_y, int vp_w, int vp_h) {
    if (settings_.mode == PerfHudMode::Off) return;

    // Determine dimensions based on mode
    float hud_w = current_width();
    float hud_h = current_height();

    // Determine position
    float pos_x = 16.0f;
    float pos_y = 16.0f;

    if (is_dragging_ || settings_.position == PerfHudPosition::FreeDrag) {
        pos_x = std::clamp(settings_.custom_x, 0.0f, static_cast<float>(win_w - hud_w));
        pos_y = std::clamp(settings_.custom_y, 0.0f, static_cast<float>(win_h - hud_h));
    } else {
        switch (settings_.position) {
            case PerfHudPosition::TopLeft:
                pos_x = 16.0f;
                pos_y = 16.0f;
                break;
            case PerfHudPosition::TopRight:
                pos_x = win_w - hud_w - 16.0f;
                pos_y = 16.0f;
                break;
            case PerfHudPosition::BottomLeft:
                pos_x = 16.0f;
                pos_y = win_h - hud_h - 16.0f;
                break;
            case PerfHudPosition::BottomRight:
                pos_x = win_w - hud_w - 16.0f;
                pos_y = win_h - hud_h - 16.0f;
                break;
            case PerfHudPosition::BottomBlackBar: {
                int bar_y = vp_y + vp_h;
                int bar_height = win_h - bar_y;
                if (bar_height >= hud_h + 8.0f) {
                    pos_x = (win_w - hud_w) * 0.5f;
                    pos_y = bar_y + (bar_height - hud_h) * 0.5f;
                } else {
                    pos_x = win_w - hud_w - 16.0f;
                    pos_y = win_h - hud_h - 16.0f;
                }
                break;
            }
            default:
                pos_x = std::clamp(settings_.custom_x, 0.0f, static_cast<float>(win_w - hud_w));
                pos_y = std::clamp(settings_.custom_y, 0.0f, static_cast<float>(win_h - hud_h));
                break;
        }
    }

    calculated_x_ = pos_x;
    calculated_y_ = pos_y;

    int mx = 0, my = 0;
    Uint32 mstate = SDL_GetMouseState(&mx, &my);
    bool mouse_down = (mstate & SDL_BUTTON_LMASK) != 0;

    SDL_Window* win = SDL_RenderGetWindow(renderer);
    if (win) {
        int cur_w = 0, cur_h = 0;
        SDL_GetWindowSize(win, &cur_w, &cur_h);
        if (cur_w > 0 && cur_h > 0) {
            mx = (mx * win_w) / cur_w;
            my = (my * win_h) / cur_h;
        }
    }

    is_hovered_ = (mx >= calculated_x_ && mx <= calculated_x_ + hud_w &&
                   my >= calculated_y_ && my <= calculated_y_ + hud_h);
    if (is_hovered_) {
        SDL_ShowCursor(SDL_ENABLE);
    }

    update_drag(mx, my, mouse_down, hud_w, hud_h);

    pos_x = calculated_x_;
    pos_y = calculated_y_;

    // Styling
    uint8_t bg_r = 15, bg_g = 18, bg_b = 26, bg_a = 215;
    uint8_t border_r = 50, border_g = 70, border_b = 100, border_a = 200;

    if (settings_.theme == PerfHudTheme::Neon) {
        bg_r = 10; bg_g = 14; bg_b = 22; bg_a = 230;
        border_r = 6, border_g = 182, border_b = 212; border_a = 255;
    } else if (settings_.theme == PerfHudTheme::Solid) {
        bg_r = 12; bg_g = 12; bg_b = 16; bg_a = 255;
        border_r = 60; border_g = 60; border_b = 70; border_a = 255;
    } else if (settings_.theme == PerfHudTheme::Minimal) {
        bg_a = 0;
        border_a = 0;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Background and border
    if (bg_a > 0) {
        SDL_Rect bg_rect = { static_cast<int>(pos_x), static_cast<int>(pos_y), static_cast<int>(hud_w), static_cast<int>(hud_h) };
        SDL_SetRenderDrawColor(renderer, bg_r, bg_g, bg_b, bg_a);
        SDL_RenderFillRect(renderer, &bg_rect);

        if (border_a > 0) {
            SDL_SetRenderDrawColor(renderer, border_r, border_g, border_b, border_a);
            SDL_RenderDrawRect(renderer, &bg_rect);
        }

        if (is_dragging_) {
            SDL_SetRenderDrawColor(renderer, 6, 182, 212, 255);
            SDL_Rect hl = { static_cast<int>(pos_x) - 1, static_cast<int>(pos_y) - 1, static_cast<int>(hud_w) + 2, static_cast<int>(hud_h) + 2 };
            SDL_RenderDrawRect(renderer, &hl);
        } else if (is_hovered_) {
            SDL_SetRenderDrawColor(renderer, 56, 189, 248, 180);
            SDL_Rect hl = { static_cast<int>(pos_x), static_cast<int>(pos_y), static_cast<int>(hud_w), static_cast<int>(hud_h) };
            SDL_RenderDrawRect(renderer, &hl);
        }
    }

    const double target_fps = FrameInterpolator::instance().target_fps();
    const double target_ms = FrameInterpolator::instance().target_frametime_ms();

            // Dynamic color code based on active target frametime
            uint8_t stat_r = 16, stat_g = 185, stat_b = 129; // Emerald green
            if (std::abs(current_frametime_ms_ - target_ms) > 2.5f && std::abs(current_frametime_ms_ - target_ms) <= 6.0f) {
                stat_r = 245; stat_g = 158; stat_b = 11; // Amber yellow
            } else if (std::abs(current_frametime_ms_ - target_ms) > 6.0f || current_fps_ < (target_fps * 0.85f)) {
                stat_r = 239; stat_g = 68; stat_b = 68;  // Rose red
            }

            char line_buf[64];

            if (settings_.mode == PerfHudMode::FpsOnly) {
                std::snprintf(line_buf, sizeof(line_buf), "%.1f FPS", current_fps_);
                draw_text(renderer, static_cast<int>(pos_x) + 8, static_cast<int>(pos_y) + 7, line_buf, stat_r, stat_g, stat_b, 255, 2);
            } else {
                // Line 1: FPS and lock status
                std::snprintf(line_buf, sizeof(line_buf), "FPS: %.1f / %.0f", current_fps_, target_fps);
                draw_text(renderer, static_cast<int>(pos_x) + 8, static_cast<int>(pos_y) + 7, line_buf, stat_r, stat_g, stat_b, 255, 1);

                // Target badge / drag cue
                if (is_dragging_) {
                    draw_text(renderer, static_cast<int>(pos_x) + 124, static_cast<int>(pos_y) + 7, "[MOVING]", 6, 182, 212, 255, 1);
                } else if (is_hovered_) {
                    draw_text(renderer, static_cast<int>(pos_x) + 130, static_cast<int>(pos_y) + 7, "[DRAG]", 56, 189, 248, 220, 1);
                } else {
                    const char* lock_str = (std::abs(current_fps_ - target_fps) <= 1.5f) ? "[LOCKED]" : "[VAR]";
                    draw_text(renderer, static_cast<int>(pos_x) + 130, static_cast<int>(pos_y) + 7, lock_str, 120, 140, 170, 220, 1);
                }

                // Line 2: Frametime in ms
                std::snprintf(line_buf, sizeof(line_buf), "TIME: %.2f ms", current_frametime_ms_);
                draw_text(renderer, static_cast<int>(pos_x) + 8, static_cast<int>(pos_y) + 21, line_buf, 220, 225, 235, 255, 1);

                // Line 2 right: min/max
                std::snprintf(line_buf, sizeof(line_buf), "AVG:%.1f", avg_frametime_ms_);
                draw_text(renderer, static_cast<int>(pos_x) + 130, static_cast<int>(pos_y) + 21, line_buf, 140, 160, 185, 220, 1);

                // Line 3: Graph (for FullWithGraph)
                if (settings_.mode == PerfHudMode::FullWithGraph) {
                    const int graph_x = static_cast<int>(pos_x) + 8;
                    const int graph_y = static_cast<int>(pos_y) + 36;
                    const int graph_w = static_cast<int>(hud_w) - 16;
                    const int graph_h = 44;

                    // Graph background
                    SDL_Rect g_rect = { graph_x, graph_y, graph_w, graph_h };
                    SDL_SetRenderDrawColor(renderer, 8, 10, 14, 180);
                    SDL_RenderFillRect(renderer, &g_rect);
                    SDL_SetRenderDrawColor(renderer, 40, 50, 68, 180);
                    SDL_RenderDrawRect(renderer, &g_rect);

                    // Target reference line
                    const float max_display_ms = 33.33f;
                    const int line_target_y = graph_y + graph_h - static_cast<int>((static_cast<float>(target_ms) / max_display_ms) * graph_h);

                    SDL_SetRenderDrawColor(renderer, 6, 182, 212, 160); // Cyan target line
                    SDL_RenderDrawLine(renderer, graph_x, line_target_y, graph_x + graph_w, line_target_y);

                    char target_label[16];
                    std::snprintf(target_label, sizeof(target_label), "%.1fms", target_ms);
                    draw_text(renderer, graph_x + 2, line_target_y - 7, target_label, 6, 182, 212, 180, 1);

                    // Draw history bars
                    const int bar_count = std::min(static_cast<int>(kHistorySize), graph_w);
                    for (int i = 0; i < bar_count; ++i) {
                        size_t idx = (history_head_ + kHistorySize - bar_count + i) % kHistorySize;
                        float ft = history_[idx];
                        if (ft <= 0.0f) continue;

                        int bar_h = static_cast<int>((ft / max_display_ms) * graph_h);
                        bar_h = std::clamp(bar_h, 1, graph_h);

                        int bar_x = graph_x + i;
                        int bar_y = graph_y + graph_h - bar_h;

                        uint8_t br = 16, bg = 185, bb = 129;
                        if (std::abs(ft - target_ms) > 2.5f && std::abs(ft - target_ms) <= 6.0f) {
                            br = 245; bg = 158; bb = 11;
                        } else if (std::abs(ft - target_ms) > 6.0f) {
                            br = 239; bg = 68; bb = 68;
                        }

                        SDL_SetRenderDrawColor(renderer, br, bg, bb, 230);
                        SDL_RenderDrawLine(renderer, bar_x, bar_y, bar_x, graph_y + graph_h - 1);
                    }
                }
            }
}

void PerfHud::on_frame_present(SDL_Renderer* renderer) {
    if (!renderer) return;

    record_frame();

    // Hotkey F10 check to cycle overlay modes
    const uint8_t* keyboard_state = SDL_GetKeyboardState(nullptr);
    bool f10_down = keyboard_state && keyboard_state[SDL_SCANCODE_F10];
    if (f10_down && !last_f10_state_) {
        cycle_mode();
    }
    last_f10_state_ = f10_down;

    if (settings_.mode == PerfHudMode::Off) return;

    int win_w = 0, win_h = 0;
    SDL_GetRendererOutputSize(renderer, &win_w, &win_h);
    last_win_w_ = win_w;
    last_win_h_ = win_h;
    last_window_ = SDL_RenderGetWindow(renderer);

    // Switch to full window coordinates to allow drawing anywhere including black bars
    SDL_RenderSetLogicalSize(renderer, 0, 0);
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);

    render_hud(renderer, win_w, win_h, game_viewport_.x, game_viewport_.y, game_viewport_.w, game_viewport_.h);

    // Keep viewport clean at full window coordinates
    SDL_RenderSetLogicalSize(renderer, 0, 0);
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
}

} // namespace gbaimp
