#include "gbaimp/text_history.h"
#include "gbaimp/host_settings.h"
#include "font5x7.h"

#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace gbaimp {

namespace {

bool has(const std::vector<int>& v, int value) {
    return std::find(v.begin(), v.end(), value) != v.end();
}

void set_color(SDL_Renderer* r, const TextHistoryColor& c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

std::string local_time_hms() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%H:%M:%S");
    return ss.str();
}

} // namespace

std::vector<std::string> wrap_text_fixed_advance(const std::string& text, int max_pixel_w, int char_advance) {
    std::vector<std::string> lines;
    if (text.empty() || max_pixel_w <= 0 || char_advance <= 0) return lines;

    std::istringstream stream(text);
    std::string word, cur;
    int cur_w = 0;
    while (stream >> word) {
        const int word_w = static_cast<int>(word.size()) * char_advance;
        if (cur.empty()) {
            cur = word;
            cur_w = word_w;
        } else if (cur_w + char_advance + word_w <= max_pixel_w) {
            cur += " " + word;
            cur_w += char_advance + word_w;
        } else {
            lines.push_back(cur);
            cur = word;
            cur_w = word_w;
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

TextHistory& TextHistory::instance() {
    static TextHistory s_instance;
    return s_instance;
}

void TextHistory::toggle_open() {
    if (!enabled_) return;
    set_open(!open_);
}

void TextHistory::set_open(bool open) {
    open_ = open;
    if (open) {
        scroll_ = max_scroll_;
        stick_to_end_ = true;
    }
}

bool TextHistory::push_entry(const std::string& speaker, const std::string& text) {
    return push_impl(speaker, text, nullptr);
}

bool TextHistory::push_entry(const std::string& speaker, const std::string& text, const std::string& timestamp) {
    return push_impl(speaker, text, &timestamp);
}

bool TextHistory::push_impl(const std::string& speaker, const std::string& text, const std::string* timestamp) {
    if (text.empty()) return false;
    const int n = static_cast<int>(entries_.size());
    for (int i = n - 1; i >= 0 && i >= n - cfg_.dedup_window; --i) {
        if (entries_[i].text == text && entries_[i].speaker == speaker) return false;
    }
    entries_.push_back({speaker, text, timestamp ? *timestamp : (timestamp_fn_ ? timestamp_fn_() : local_time_hms())});
    if (cfg_.max_entries > 0 && entries_.size() > cfg_.max_entries) {
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - cfg_.max_entries));
    }
    return true;
}

void TextHistory::clear() {
    entries_.clear();
    scroll_ = 0;
    max_scroll_ = 0;
}

void TextHistory::scroll_by(int pixels) {
    stick_to_end_ = false;
    scroll_ = std::clamp(scroll_ + pixels, 0, std::max(0, max_scroll_));
}

int TextHistory::bar_width_for(int win_w) const {
    const TextHistoryStyle& s = cfg_.style;
    const int hi = std::max(s.bar_min_width, s.bar_max_width);
    return std::clamp((win_w * s.bar_width_percent) / 100, s.bar_min_width, hi);
}

bool TextHistory::handle_event(const SDL_Event& event) {
    if (!enabled_) return false;
    if (event.type == SDL_QUIT || event.type == SDL_APP_TERMINATING) return false;
    const TextHistoryControls& c = cfg_.controls;

    switch (event.type) {
    case SDL_KEYDOWN: {
        const int k = static_cast<int>(event.key.keysym.sym);
        if (has(c.toggle_keys, k)) { toggle_open(); return true; }
        if (!open_) return false;
        if (has(c.close_keys, k)) { set_open(false); return true; }
        if (has(c.scroll_up_keys, k)) scroll_by(-c.line_step);
        else if (has(c.scroll_down_keys, k)) scroll_by(c.line_step);
        else if (has(c.page_up_keys, k)) scroll_by(-c.page_step);
        else if (has(c.page_down_keys, k)) scroll_by(c.page_step);
        return true;   // swallow input while reading
    }
    case SDL_KEYUP:
        return open_;
    case SDL_CONTROLLERBUTTONDOWN: {
        const int b = event.cbutton.button;
        if (has(c.toggle_buttons, b)) { toggle_open(); return true; }
        if (!open_) return false;
        if (has(c.close_buttons, b)) set_open(false);
        else if (has(c.scroll_up_buttons, b)) scroll_by(-c.line_step);
        else if (has(c.scroll_down_buttons, b)) scroll_by(c.line_step);
        return true;
    }
    case SDL_JOYBUTTONDOWN: {
        const int b = event.jbutton.button;
        if (has(c.toggle_joy_buttons, b)) { toggle_open(); return true; }
        if (!open_) return false;
        if (has(c.close_joy_buttons, b)) set_open(false);
        return true;
    }
    case SDL_MOUSEWHEEL:
        if (!open_) return false;
        scroll_by(-event.wheel.y * c.wheel_step);
        return true;
    case SDL_MOUSEBUTTONDOWN:
        if (!open_) return false;
        if (event.button.button == SDL_BUTTON_LEFT) {
            if (c.close_on_outside_click && event.button.x > bar_w_) { set_open(false); return true; }
            if (event.button.y <= cfg_.style.header_height && event.button.x >= bar_w_ - 140) {
                set_open(false);
                return true;
            }
        }
        return true;
    default:
        return open_;
    }
}

void TextHistory::draw_text(SDL_Renderer* r, int x, int y, const std::string& s, const TextHistoryColor& c,
                            int scale) const {
    if (s.empty() || !r || scale <= 0) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    set_color(r, c);
    int cur_x = x;
    for (char ch : s) {
        detail::draw_glyph_5x7(r, cur_x, y, ch, scale);
        cur_x += 6 * scale;
    }
}

void TextHistory::render_sidebar(SDL_Renderer* renderer, int win_w, int win_h) {
    if (!enabled_ || !open_ || !renderer || win_w <= 0 || win_h <= 0) return;
    const TextHistoryStyle& st = cfg_.style;

    // Raw window coordinates: leave logical size / viewport / scale.
    SDL_RenderSetLogicalSize(renderer, 0, 0);
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    set_color(renderer, st.backdrop);
    SDL_Rect full = {0, 0, win_w, win_h};
    SDL_RenderFillRect(renderer, &full);

    const int bar_w = bar_width_for(win_w);
    bar_w_ = bar_w;

    set_color(renderer, st.panel);
    SDL_Rect panel = {0, 0, bar_w, win_h};
    SDL_RenderFillRect(renderer, &panel);

    set_color(renderer, st.accent);
    SDL_RenderDrawLine(renderer, bar_w - 1, 0, bar_w - 1, win_h);
    set_color(renderer, st.accent_shadow);
    SDL_RenderDrawLine(renderer, bar_w - 2, 0, bar_w - 2, win_h);

    const int header_h = st.header_height;
    set_color(renderer, st.header_bg);
    SDL_Rect header = {0, 0, bar_w - 1, header_h};
    SDL_RenderFillRect(renderer, &header);
    set_color(renderer, st.header_line);
    SDL_RenderDrawLine(renderer, 0, header_h, bar_w - 1, header_h);

    draw_text(renderer, 20, 14, st.title, st.title_color, 2);
    draw_text(renderer, bar_w - 130, 20, st.close_hint, st.close_hint_color, 1);
    draw_text(renderer, 20, header_h + 6, st.scroll_hint, st.scroll_hint_color, 1);

    // Layout with exact pixel wrapping.
    const int entry_start_y = header_h + 30;
    const int content_h = std::max(1, win_h - entry_start_y - 24);
    const int font_scale = (win_h >= st.large_font_min_window_height) ? 2 : 1;
    const int char_advance = 6 * font_scale;
    const int line_h = 8 * font_scale + 4;
    const int max_text_w = bar_w - 48;

    struct Prepared {
        const TextHistoryEntry* e;
        std::vector<std::string> lines;
        int height;
    };
    std::vector<Prepared> prepared;
    prepared.reserve(entries_.size());
    int total_h = 0;
    for (const TextHistoryEntry& e : entries_) {
        Prepared p{&e, wrap_text_fixed_advance(e.text, max_text_w, char_advance), 0};
        p.height = (7 * font_scale + 8) + static_cast<int>(p.lines.size()) * line_h + 14;
        total_h += p.height;
        prepared.push_back(std::move(p));
    }

    max_scroll_ = std::max(0, total_h - content_h);
    if (stick_to_end_) {
        scroll_ = max_scroll_;
        stick_to_end_ = false;
    }
    scroll_ = std::clamp(scroll_, 0, max_scroll_);

    if (prepared.empty()) {
        draw_text(renderer, 26, entry_start_y + 40, st.empty_title, st.empty_title_color, font_scale);
        draw_text(renderer, 26, entry_start_y + 70, st.empty_hint, st.scroll_hint_color, 1);
    }

    int cur_y = entry_start_y - scroll_;
    for (const Prepared& p : prepared) {
        if (cur_y + p.height > entry_start_y && cur_y < win_h - 10) {
            TextHistoryColor spk = st.default_speaker_color;
            for (const auto& sc : st.speaker_colors) {
                if (sc.first == p.e->speaker) { spk = sc.second; break; }
            }
            draw_text(renderer, 22, cur_y, p.e->speaker, spk, font_scale);
            draw_text(renderer, bar_w - (font_scale == 2 ? 110 : 70), cur_y + 2, p.e->timestamp, st.timestamp_color, 1);

            int text_y = cur_y + (7 * font_scale + 8);
            for (const std::string& line : p.lines) {
                if (text_y >= entry_start_y - line_h && text_y < win_h - 10) {
                    draw_text(renderer, 26, text_y, line, st.text_color, font_scale);
                }
                text_y += line_h;
            }
            set_color(renderer, st.separator);
            SDL_RenderDrawLine(renderer, 20, cur_y + p.height - 4, bar_w - 24, cur_y + p.height - 4);
        }
        cur_y += p.height;
    }

    if (max_scroll_ > 0) {
        const int sb_w = 6, sb_x = bar_w - 10, track_h = content_h;
        const int thumb_h = std::max(28, (track_h * content_h) / total_h);
        const int thumb_y = entry_start_y + (scroll_ * std::max(0, track_h - thumb_h)) / max_scroll_;
        set_color(renderer, st.scrollbar_track);
        SDL_Rect track = {sb_x, entry_start_y, sb_w, track_h};
        SDL_RenderFillRect(renderer, &track);
        set_color(renderer, st.scrollbar_thumb);
        SDL_Rect thumb = {sb_x, thumb_y, sb_w, thumb_h};
        SDL_RenderFillRect(renderer, &thumb);
    }

    SDL_RenderSetLogicalSize(renderer, 0, 0);
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
}

void register_text_history_settings(SettingsRegistry& reg, TextHistory& history,
                                    const TextHistorySettingsOptions& o) {
    Setting en;
    en.id = o.enable_id;
    en.menu_section = o.menu_section;
    en.label = o.enable_label;
    en.tooltip = o.enable_tooltip;
    en.kind = SettingKind::Bool;
    en.min = 0; en.max = 1; en.step = 1;
    en.ini_section = o.ini_section;
    en.ini_key = o.ini_key;
    en.get = [&history] { return history.is_enabled() ? 1 : 0; };
    en.set = [&history](int v) { history.set_enabled(v != 0); };
    reg.add(std::move(en));

    Setting open;
    open.id = o.open_id;
    open.menu_section = o.menu_section;
    open.label = o.open_label;
    open.tooltip = o.open_tooltip;
    open.kind = SettingKind::Action;
    open.min = 0; open.max = 0; open.step = 0;
    open.action = [&history] { history.set_open(true); };
    open.save_after_action = false;
    reg.add(std::move(open));
}

} // namespace gbaimp
