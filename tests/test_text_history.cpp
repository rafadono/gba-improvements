#include "test_harness.h"
#include "gbaimp/host_settings.h"
#include "gbaimp/text_history.h"

#include <SDL.h>
#include <string>
#include <vector>

using namespace gbaimp;

namespace {

SDL_Event key_down(SDL_Keycode k) {
    SDL_Event e; SDL_zero(e);
    e.type = SDL_KEYDOWN; e.key.keysym.sym = k;
    return e;
}
SDL_Event key_up(SDL_Keycode k) {
    SDL_Event e = key_down(k);
    e.type = SDL_KEYUP;
    return e;
}
SDL_Event pad_down(int button) {
    SDL_Event e; SDL_zero(e);
    e.type = SDL_CONTROLLERBUTTONDOWN; e.cbutton.button = static_cast<Uint8>(button);
    return e;
}
SDL_Event joy_down(int button) {
    SDL_Event e; SDL_zero(e);
    e.type = SDL_JOYBUTTONDOWN; e.jbutton.button = static_cast<Uint8>(button);
    return e;
}
SDL_Event wheel(int y) {
    SDL_Event e; SDL_zero(e);
    e.type = SDL_MOUSEWHEEL; e.wheel.y = y;
    return e;
}
SDL_Event click(int x, int y, Uint8 button = SDL_BUTTON_LEFT) {
    SDL_Event e; SDL_zero(e);
    e.type = SDL_MOUSEBUTTONDOWN; e.button.button = button; e.button.x = x; e.button.y = y;
    return e;
}
SDL_Event plain(Uint32 type) {
    SDL_Event e; SDL_zero(e);
    e.type = type;
    return e;
}

struct Rgb { int r, g, b; };
Rgb pixel(SDL_Renderer* ren, int x, int y) {
    SDL_Rect r = { x, y, 1, 1 };
    uint32_t px = 0;
    SDL_RenderReadPixels(ren, &r, SDL_PIXELFORMAT_ARGB8888, &px, sizeof(px));
    return { int((px >> 16) & 0xFF), int((px >> 8) & 0xFF), int(px & 0xFF) };
}
void clear_white(SDL_Renderer* ren) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderClear(ren);
}
bool within(int a, int b, int tol) { return a >= b - tol && a <= b + tol; }

void fill(TextHistory& h, int n) {
    for (int i = 0; i < n; ++i) {
        h.push_entry("Speaker", "Entry number " + std::to_string(i) +
                     " with enough words to wrap over several lines inside the narrow sidebar panel");
    }
}

} // namespace

int main() {
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_Init(SDL_INIT_VIDEO);

    // --- entries: order, dedup, limits, timestamps -------------------------------------------------
    {
        TextHistory h;
        int tick = 0;
        h.set_timestamp_source([&tick] { return "t" + std::to_string(tick++); });
        CHECK(h.is_enabled());
        CHECK(!h.is_open());
        CHECK(h.entries().empty());
        CHECK(!h.push_entry("A", ""));                       // empty text ignored
        CHECK(h.push_entry("A", "one"));
        CHECK(h.push_entry("B", "two"));
        CHECK(h.push_entry("A", "three", "12:00:00"));       // explicit timestamp wins
        CHECK_EQ(h.entries().size(), size_t(3));
        CHECK_EQ(h.entries()[0].text, std::string("one"));
        CHECK_EQ(h.entries()[0].timestamp, std::string("t0"));
        CHECK_EQ(h.entries()[1].speaker, std::string("B"));
        CHECK_EQ(h.entries()[1].timestamp, std::string("t1"));
        CHECK_EQ(h.entries()[2].timestamp, std::string("12:00:00"));

        // duplicates inside the window of 3 are dropped; same text from another speaker is not
        CHECK(!h.push_entry("A", "three"));
        CHECK(!h.push_entry("A", "one"));
        CHECK(h.push_entry("C", "one"));
        CHECK(h.push_entry("A", "four"));
        CHECK(h.push_entry("A", "one"));                     // "one"/A is now older than 3 entries
        h.config().dedup_window = 0;
        CHECK(h.push_entry("A", "one"));                     // dedup off
        h.config().dedup_window = 1;
        CHECK(!h.push_entry("A", "one"));
        CHECK(h.push_entry("A", "two"));

        // bounded list: the oldest entries go first
        TextHistory b;
        b.config().max_entries = 5;
        for (int i = 0; i < 12; ++i) b.push_entry("S", "line " + std::to_string(i));
        CHECK_EQ(b.entries().size(), size_t(5));
        CHECK_EQ(b.entries().front().text, std::string("line 7"));
        CHECK_EQ(b.entries().back().text, std::string("line 11"));
        b.config().max_entries = 2;                          // shrinking applies on the next push
        b.push_entry("S", "line 12");
        CHECK_EQ(b.entries().size(), size_t(2));
        CHECK_EQ(b.entries().front().text, std::string("line 11"));
        b.config().max_entries = 0;                          // unlimited
        for (int i = 0; i < 400; ++i) b.push_entry("S", "x" + std::to_string(i));
        CHECK_EQ(b.entries().size(), size_t(402));

        // default bound is 200
        TextHistory d;
        for (int i = 0; i < 250; ++i) d.push_entry("S", "n" + std::to_string(i));
        CHECK_EQ(d.entries().size(), size_t(200));
        CHECK_EQ(d.entries().front().text, std::string("n50"));

        h.clear();
        CHECK(h.entries().empty());
        CHECK_EQ(h.scroll_offset(), 0);

        // default timestamp: HH:MM:SS
        TextHistory t;
        t.push_entry("A", "x");
        const std::string ts = t.entries()[0].timestamp;
        CHECK_EQ(ts.size(), size_t(8));
        CHECK(ts[2] == ':' && ts[5] == ':');
    }

    // --- word wrap -------------------------------------------------------------------------------
    {
        const auto l = wrap_text_fixed_advance("aaa bbb ccc", 7 * 6, 6);   // "aaa bbb" = 7 cells
        CHECK_EQ(l.size(), size_t(2));
        CHECK_EQ(l[0], std::string("aaa bbb"));
        CHECK_EQ(l[1], std::string("ccc"));
        const auto m = wrap_text_fixed_advance("averyverylongword x", 12, 6);   // never split
        CHECK_EQ(m.size(), size_t(2));
        CHECK_EQ(m[0], std::string("averyverylongword"));
        CHECK(wrap_text_fixed_advance("", 100, 6).empty());
        CHECK(wrap_text_fixed_advance("abc", 0, 6).empty());
        CHECK_EQ(wrap_text_fixed_advance("a\n\n  b", 100, 6).size(), size_t(1));   // whitespace collapses
    }

    // --- hotkeys with synthetic events -------------------------------------------------------------
    {
        TextHistory h;
        // closed: only the toggles are consumed
        CHECK(!h.handle_event(key_down(SDLK_a)));
        CHECK(!h.handle_event(key_down(SDLK_ESCAPE)));
        CHECK(!h.handle_event(key_up(SDLK_a)));
        CHECK(!h.handle_event(wheel(1)));
        CHECK(!h.handle_event(click(10, 10)));
        CHECK(!h.handle_event(plain(SDL_MOUSEMOTION)));
        CHECK(!h.handle_event(pad_down(SDL_CONTROLLER_BUTTON_A)));
        CHECK(!h.is_open());

        CHECK(h.handle_event(key_down(SDLK_F2)));
        CHECK(h.is_open());
        CHECK(h.handle_event(key_down(SDLK_F2)));
        CHECK(!h.is_open());
        CHECK(h.handle_event(pad_down(SDL_CONTROLLER_BUTTON_BACK)));
        CHECK(h.is_open());
        CHECK(h.handle_event(pad_down(SDL_CONTROLLER_BUTTON_B)));            // close
        CHECK(!h.is_open());
        CHECK(h.handle_event(joy_down(6)));
        CHECK(h.is_open());
        CHECK(h.handle_event(joy_down(1)));                                  // close
        CHECK(!h.is_open());
        CHECK(h.handle_event(joy_down(4)));
        CHECK(h.handle_event(joy_down(8)));                                  // toggles off
        CHECK(!h.is_open());
        CHECK(!h.handle_event(joy_down(2)));                                 // not a toggle, closed

        // open: everything is swallowed except quit
        h.set_open(true);
        CHECK(h.handle_event(key_down(SDLK_a)));
        CHECK(h.handle_event(key_down(SDLK_SPACE)));
        CHECK(h.handle_event(key_up(SDLK_a)));
        CHECK(h.handle_event(plain(SDL_MOUSEMOTION)));
        CHECK(h.handle_event(pad_down(SDL_CONTROLLER_BUTTON_A)));
        CHECK(h.handle_event(joy_down(2)));
        CHECK(h.handle_event(wheel(-1)));
        CHECK(!h.handle_event(plain(SDL_QUIT)));
        CHECK(!h.handle_event(plain(SDL_APP_TERMINATING)));
        CHECK(h.is_open());
        CHECK(h.handle_event(key_down(SDLK_ESCAPE)));
        CHECK(!h.is_open());
        CHECK(!h.handle_event(key_down(SDLK_a)));                            // input flows again

        // disabled: nothing is consumed and the panel cannot open
        h.set_enabled(false);
        CHECK(!h.handle_event(key_down(SDLK_F2)));
        CHECK(!h.handle_event(pad_down(SDL_CONTROLLER_BUTTON_BACK)));
        h.toggle_open();
        CHECK(!h.is_open());
        h.set_enabled(true);

        // an open panel that gets disabled stops swallowing
        h.set_open(true);
        h.set_enabled(false);
        CHECK(!h.handle_event(key_down(SDLK_a)));
        h.set_enabled(true);
        h.set_open(false);

        // configurable hotkeys
        TextHistory c;
        c.config().controls.toggle_keys = {SDLK_l, SDLK_F3};
        c.config().controls.close_keys = {SDLK_q};
        c.config().controls.toggle_buttons.clear();
        CHECK(!c.handle_event(key_down(SDLK_F2)));
        CHECK(c.handle_event(key_down(SDLK_l)));
        CHECK(c.is_open());
        CHECK(c.handle_event(key_down(SDLK_ESCAPE)));                        // swallowed but not a close key now
        CHECK(c.is_open());
        CHECK(c.handle_event(key_down(SDLK_q)));
        CHECK(!c.is_open());
        CHECK(c.handle_event(key_down(SDLK_F3)));
        CHECK(c.is_open());
        c.set_open(false);
        CHECK(!c.handle_event(pad_down(SDL_CONTROLLER_BUTTON_BACK)));        // button toggle removed
    }

    // --- scrolling, rendering, input while open ----------------------------------------------------
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, 1000, 600, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    CHECK(ren != nullptr);
    if (ren) {
        // closed or disabled: nothing is drawn
        {
            TextHistory h;
            clear_white(ren);
            h.render_sidebar(ren, 1000, 600);
            CHECK_EQ(pixel(ren, 100, 590).r, 255);
            h.set_open(true);
            h.set_enabled(false);
            h.render_sidebar(ren, 1000, 600);
            CHECK_EQ(pixel(ren, 100, 590).r, 255);
            h.set_enabled(true);
            h.render_sidebar(nullptr, 1000, 600);       // tolerated
            h.render_sidebar(ren, 0, 0);
            CHECK_EQ(pixel(ren, 100, 590).r, 255);
        }
        // open, empty: dim backdrop outside the panel, dark panel inside; panel width rules
        {
            TextHistory h;
            h.set_open(true);
            clear_white(ren);
            SDL_RenderSetLogicalSize(ren, 320, 200);   // an overlay must leave logical coordinates
            h.render_sidebar(ren, 1000, 600);
            int lw = -1, lh = -1;
            SDL_RenderGetLogicalSize(ren, &lw, &lh);
            CHECK(lw == 0 && lh == 0);
            CHECK_EQ(h.bar_width(), 400);              // 40% of 1000
            CHECK(within(pixel(ren, 700, 300).r, 125, 3));   // backdrop only: 255 * (1 - 130/255)
            CHECK(pixel(ren, 100, 590).r < 30);            // panel
            CHECK(pixel(ren, 100, 20).b < 60);             // header strip (title text may cover few pixels)
            CHECK_EQ(h.bar_width_for(500), 380);           // clamped to the minimum
            CHECK_EQ(h.bar_width_for(4000), 600);          // and the maximum
            CHECK_EQ(h.max_scroll(), 0);

            // restyle: wider bar, other panel color, no backdrop
            h.config().style.bar_width_percent = 60;
            h.config().style.backdrop = {0, 0, 0, 0};
            h.config().style.panel = {200, 0, 0, 255};
            clear_white(ren);
            h.render_sidebar(ren, 1000, 600);
            CHECK_EQ(h.bar_width(), 600);
            CHECK_EQ(pixel(ren, 900, 300).r, 255);
            CHECK(pixel(ren, 590, 590).r == 200 && pixel(ren, 590, 590).g == 0);
        }
        // entries are drawn: text pixels appear that the empty panel does not have
        {
            TextHistory h;
            h.config().style.speaker_colors = {{"Hero", {255, 0, 0, 255}}};
            h.push_entry("Hero", "Hello there");
            h.set_open(true);
            clear_white(ren);
            h.render_sidebar(ren, 1000, 600);
            int bright = 0;
            for (int y = 86; y < 130; ++y)
                for (int x = 20; x < 200; ++x)
                    if (pixel(ren, x, y).r > 200) ++bright;
            CHECK(bright > 20);   // speaker name (red) and body text (near white) over the dark panel
            // the speaker color: some pixel of the first text row is pure red-ish (g and b low)
            bool red = false;
            for (int y = 86; y < 100 && !red; ++y)
                for (int x = 22; x < 80 && !red; ++x) {
                    const Rgb p = pixel(ren, x, y);
                    if (p.r > 200 && p.g < 60 && p.b < 60) red = true;
                }
            CHECK(red);
        }
        // scrolling
        {
            TextHistory h;
            fill(h, 30);
            h.set_open(true);
            clear_white(ren);
            h.render_sidebar(ren, 1000, 600);
            const int mx = h.max_scroll();
            CHECK(mx > 500);
            CHECK_EQ(h.scroll_offset(), mx);                 // opens on the newest entry

            CHECK(h.handle_event(key_down(SDLK_UP)));
            CHECK_EQ(h.scroll_offset(), mx - 36);
            CHECK(h.handle_event(key_down(SDLK_PAGEUP)));
            CHECK_EQ(h.scroll_offset(), mx - 36 - 180);
            CHECK(h.handle_event(key_down(SDLK_DOWN)));
            CHECK_EQ(h.scroll_offset(), mx - 180);
            CHECK(h.handle_event(key_down(SDLK_PAGEDOWN)));
            CHECK_EQ(h.scroll_offset(), mx);                 // clamped at the bottom
            CHECK(h.handle_event(key_down(SDLK_DOWN)));
            CHECK_EQ(h.scroll_offset(), mx);
            CHECK(h.handle_event(wheel(2)));                 // wheel up scrolls toward the oldest
            CHECK_EQ(h.scroll_offset(), mx - 96);
            CHECK(h.handle_event(wheel(-1)));
            CHECK_EQ(h.scroll_offset(), mx - 48);
            CHECK(h.handle_event(pad_down(SDL_CONTROLLER_BUTTON_DPAD_UP)));
            CHECK_EQ(h.scroll_offset(), mx - 84);
            CHECK(h.handle_event(pad_down(SDL_CONTROLLER_BUTTON_DPAD_DOWN)));
            CHECK_EQ(h.scroll_offset(), mx - 48);
            h.scroll_by(-100000);
            CHECK_EQ(h.scroll_offset(), 0);                  // clamped at the top
            CHECK(h.handle_event(key_down(SDLK_UP)));
            CHECK_EQ(h.scroll_offset(), 0);

            // rendering keeps the offset (inside range) and the scrollbar thumb sits at the top
            h.scroll_by(200);
            clear_white(ren);
            h.render_sidebar(ren, 1000, 600);
            CHECK_EQ(h.scroll_offset(), 200);
            bool thumb = false;                              // thumb: accent blue in the scroll bar column
            for (int y = 86; y < 560 && !thumb; ++y) thumb = pixel(ren, 395, y).b > 150;
            CHECK(thumb);

            // configurable steps
            h.config().controls.line_step = 10;
            h.handle_event(key_down(SDLK_DOWN));
            CHECK_EQ(h.scroll_offset(), 210);

            // closing and reopening goes back to the newest entry
            h.set_open(false);
            h.set_open(true);
            h.render_sidebar(ren, 1000, 600);
            CHECK_EQ(h.scroll_offset(), h.max_scroll());

            // a shrinking history re-clamps at the next render
            h.scroll_by(-100000);
            h.clear();
            h.render_sidebar(ren, 1000, 600);
            CHECK_EQ(h.max_scroll(), 0);
            CHECK_EQ(h.scroll_offset(), 0);
        }
        // a bigger window scales the font and still lays out
        {
            TextHistory h;
            fill(h, 5);
            h.set_open(true);
            SDL_Surface* big = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_Renderer* bren = big ? SDL_CreateSoftwareRenderer(big) : nullptr;
            CHECK(bren != nullptr);
            if (bren) {
                h.render_sidebar(bren, 1920, 1080);
                CHECK_EQ(h.bar_width(), 600);
                SDL_DestroyRenderer(bren);
            }
            if (big) SDL_FreeSurface(big);
        }
        // mouse closing needs the panel width of a render
        {
            TextHistory h;
            h.set_open(true);
            h.render_sidebar(ren, 1000, 600);                // bar 0..400
            CHECK(h.handle_event(click(200, 300)));          // inside, below the header: swallowed, stays open
            CHECK(h.is_open());
            CHECK(h.handle_event(click(200, 300, SDL_BUTTON_RIGHT)));
            CHECK(h.is_open());
            CHECK(h.handle_event(click(700, 300)));          // outside: closes
            CHECK(!h.is_open());
            h.set_open(true);
            CHECK(h.handle_event(click(300, 30)));           // close hint area in the header
            CHECK(!h.is_open());
            h.set_open(true);
            CHECK(h.handle_event(click(50, 30)));            // header, left: stays
            CHECK(h.is_open());
            h.config().controls.close_on_outside_click = false;
            CHECK(h.handle_event(click(700, 300)));
            CHECK(h.is_open());
        }
        SDL_DestroyRenderer(ren);
    }
    if (surf) SDL_FreeSurface(surf);

    // --- settings helper ----------------------------------------------------------------------------
    {
        TextHistory h;
        SettingsRegistry reg;
        reg.set_autosave(false);
        register_text_history_settings(reg, h);
        const Setting* en = reg.find("dialogue.backlog_enable");
        const Setting* op = reg.find("dialogue.open_backlog");
        CHECK(en != nullptr && op != nullptr);
        if (en && op) {
            CHECK(en->kind == SettingKind::Bool);
            CHECK(op->kind == SettingKind::Action);
            CHECK(!op->save_after_action);
            CHECK_EQ(en->ini_section, std::string("Dialogue"));
            CHECK_EQ(en->ini_key, std::string("backlog_enable"));
        }
        int v = -1;
        CHECK(reg.get("dialogue.backlog_enable", v));
        CHECK_EQ(v, 1);
        CHECK(reg.set("dialogue.backlog_enable", 0));
        CHECK(!h.is_enabled());
        CHECK(reg.ini_text().find("[Dialogue]\nbacklog_enable=0") != std::string::npos);
        reg.load_ini_text("[Dialogue]\nbacklog_enable=1\n");
        CHECK(h.is_enabled());
        CHECK(reg.run_action("dialogue.open_backlog"));
        CHECK(h.is_open());

        // custom ids / ini placement
        TextHistory h2;
        SettingsRegistry reg2;
        TextHistorySettingsOptions o;
        o.enable_id = "ui.history_enable";
        o.open_id = "ui.history_open";
        o.ini_section = "Interface";
        o.ini_key = "history_enable";
        register_text_history_settings(reg2, h2, o);
        CHECK(reg2.find("ui.history_enable") != nullptr);
        CHECK(reg2.find("dialogue.backlog_enable") == nullptr);
        CHECK(reg2.set("ui.history_enable", 0));
        CHECK(!h2.is_enabled());
    }

    SDL_Quit();
    return testh::finish("text_history");
}
