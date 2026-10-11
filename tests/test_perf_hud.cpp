#include "test_harness.h"
#include "gbaimp/hooks.h"
#include "gbaimp/perf_hud.h"
#include <SDL.h>
#include <cstdlib>
#include <vector>

using namespace gbaimp;

namespace {

SDL_Event mouse_down(int x, int y, Uint8 button = SDL_BUTTON_LEFT) {
    SDL_Event e; SDL_zero(e);
    e.type = SDL_MOUSEBUTTONDOWN;
    e.button.button = button; e.button.x = x; e.button.y = y; e.button.windowID = 0;
    return e;
}
SDL_Event mouse_up(int x, int y, Uint8 button = SDL_BUTTON_LEFT) {
    SDL_Event e = mouse_down(x, y, button);
    e.type = SDL_MOUSEBUTTONUP;
    return e;
}
SDL_Event mouse_move(int x, int y) {
    SDL_Event e; SDL_zero(e);
    e.type = SDL_MOUSEMOTION;
    e.motion.x = x; e.motion.y = y; e.motion.windowID = 0;
    return e;
}

struct Rgb { int r, g, b; };

// Read one pixel back from the software renderer.
Rgb pixel(SDL_Renderer* ren, int x, int y) {
    SDL_Rect r = { x, y, 1, 1 };
    uint32_t px = 0;
    SDL_RenderReadPixels(ren, &r, SDL_PIXELFORMAT_ARGB8888, &px, sizeof(px));
    return { int((px >> 16) & 0xFF), int((px >> 8) & 0xFF), int(px & 0xFF) };
}

void clear(SDL_Renderer* ren) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
}

} // namespace

int main() {
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_Init(SDL_INIT_VIDEO);   // dummy driver; the module also tolerates no video at all

    // --- Defaults and simple setters ------------------------------------------
    {
        PerfHud h;
        CHECK(h.settings().mode == PerfHudMode::FullWithGraph);
        CHECK(h.settings().position == PerfHudPosition::TopRight);
        CHECK(h.settings().theme == PerfHudTheme::GlassDark);
        CHECK(h.settings().show_target_line);
        CHECK(!h.is_dragging());
        CHECK(!h.is_hovered());
        CHECK_EQ(h.last_window_width(), 1280);
        CHECK_EQ(h.last_window_height(), 720);

        h.set_theme(PerfHudTheme::Neon);    CHECK(h.settings().theme == PerfHudTheme::Neon);
        h.set_theme(PerfHudTheme::Solid);   CHECK(h.settings().theme == PerfHudTheme::Solid);
        h.set_theme(PerfHudTheme::Minimal); CHECK(h.settings().theme == PerfHudTheme::Minimal);

        // cycle_mode walks all four modes and wraps.
        h.set_mode(PerfHudMode::Off);
        h.cycle_mode(); CHECK(h.settings().mode == PerfHudMode::FpsOnly);
        h.cycle_mode(); CHECK(h.settings().mode == PerfHudMode::FpsAndFrametime);
        h.cycle_mode(); CHECK(h.settings().mode == PerfHudMode::FullWithGraph);
        h.cycle_mode(); CHECK(h.settings().mode == PerfHudMode::Off);

        // Box size per mode.
        h.set_mode(PerfHudMode::FpsOnly);
        CHECK_NEAR(h.current_width(), 88.0f, 1e-3f);  CHECK_NEAR(h.current_height(), 24.0f, 1e-3f);
        h.set_mode(PerfHudMode::FpsAndFrametime);
        CHECK_NEAR(h.current_width(), 164.0f, 1e-3f); CHECK_NEAR(h.current_height(), 44.0f, 1e-3f);
        h.set_mode(PerfHudMode::FullWithGraph);
        CHECK_NEAR(h.current_width(), 200.0f, 1e-3f); CHECK_NEAR(h.current_height(), 88.0f, 1e-3f);

        // Viewport round trip (default is the native GBA screen).
        CHECK_EQ(h.game_viewport().w, 240);
        CHECK_EQ(h.game_viewport().h, 160);
        SDL_Rect vp = { 10, 20, 960, 640 };
        h.set_game_viewport(vp);
        CHECK_EQ(h.game_viewport().x, 10);
        CHECK_EQ(h.game_viewport().y, 20);
        CHECK_EQ(h.game_viewport().w, 960);
        CHECK_EQ(h.game_viewport().h, 640);
    }

    // --- set_position anchors (window assumed 1280x720, full-size box 200x88) ---
    {
        PerfHud h;
        h.set_mode(PerfHudMode::FullWithGraph);
        h.set_position(PerfHudPosition::TopLeft);
        CHECK_NEAR(h.settings().custom_x, 16.0f, 1e-3f);   CHECK_NEAR(h.settings().custom_y, 16.0f, 1e-3f);
        h.set_position(PerfHudPosition::TopRight);
        CHECK_NEAR(h.settings().custom_x, 1064.0f, 1e-3f); CHECK_NEAR(h.settings().custom_y, 16.0f, 1e-3f);
        h.set_position(PerfHudPosition::BottomLeft);
        CHECK_NEAR(h.settings().custom_x, 16.0f, 1e-3f);   CHECK_NEAR(h.settings().custom_y, 616.0f, 1e-3f);
        h.set_position(PerfHudPosition::BottomRight);
        CHECK_NEAR(h.settings().custom_x, 1064.0f, 1e-3f); CHECK_NEAR(h.settings().custom_y, 616.0f, 1e-3f);
        h.set_position(PerfHudPosition::BottomBlackBar);
        CHECK_NEAR(h.settings().custom_x, 540.0f, 1e-3f);  CHECK_NEAR(h.settings().custom_y, 616.0f, 1e-3f);
        CHECK_NEAR(h.calculated_x(), 540.0f, 1e-3f);
        CHECK_NEAR(h.calculated_y(), 616.0f, 1e-3f);
        // FreeDrag keeps whatever coordinates are current.
        h.set_position(PerfHudPosition::FreeDrag);
        CHECK(h.settings().position == PerfHudPosition::FreeDrag);
        CHECK_NEAR(h.settings().custom_x, 540.0f, 1e-3f);
    }

    // --- Mouse dragging -----------------------------------------------------------
    {
        int saved = 0;
        hooks().save_settings = [&] { ++saved; };

        PerfHud h;
        h.set_mode(PerfHudMode::Off);
        CHECK(!h.handle_mouse_event(mouse_down(20, 20)));          // off: events are ignored
        CHECK(!h.is_dragging());

        h.set_mode(PerfHudMode::FullWithGraph);
        h.set_position(PerfHudPosition::TopLeft);                  // box at (16,16) 200x88

        CHECK(!h.handle_mouse_event(mouse_down(900, 500)));        // outside the box
        CHECK(!h.is_dragging());
        CHECK(!h.handle_mouse_event(mouse_down(20, 20, SDL_BUTTON_RIGHT)));  // wrong button
        CHECK(!h.is_dragging());

        CHECK(!h.handle_mouse_event(mouse_move(900, 500)));
        CHECK(!h.is_hovered());
        CHECK(!h.handle_mouse_event(mouse_move(30, 30)));          // hover only, not consumed
        CHECK(h.is_hovered());

        CHECK(h.handle_mouse_event(mouse_down(20, 20)));           // grab with offset (4,4)
        CHECK(h.is_dragging());
        CHECK(h.settings().position == PerfHudPosition::FreeDrag);

        CHECK(h.handle_mouse_event(mouse_move(120, 220)));
        CHECK_NEAR(h.settings().custom_x, 116.0f, 1e-3f);
        CHECK_NEAR(h.settings().custom_y, 216.0f, 1e-3f);
        CHECK_NEAR(h.calculated_x(), 116.0f, 1e-3f);
        CHECK_NEAR(h.calculated_y(), 216.0f, 1e-3f);

        CHECK(h.handle_mouse_event(mouse_move(5000, 5000)));       // clamped inside the window
        CHECK_NEAR(h.settings().custom_x, 1080.0f, 1e-3f);         // 1280 - 200
        CHECK_NEAR(h.settings().custom_y, 632.0f, 1e-3f);          // 720 - 88
        CHECK(h.handle_mouse_event(mouse_move(-500, -500)));
        CHECK_NEAR(h.settings().custom_x, 0.0f, 1e-3f);
        CHECK_NEAR(h.settings().custom_y, 0.0f, 1e-3f);

        CHECK_EQ(saved, 0);                                        // nothing persisted mid-drag
        CHECK(!h.handle_mouse_event(mouse_up(0, 0, SDL_BUTTON_RIGHT)));  // wrong button keeps dragging
        CHECK(h.is_dragging());
        CHECK(h.handle_mouse_event(mouse_up(0, 0)));
        CHECK(!h.is_dragging());
        CHECK_EQ(saved, 1);                                        // persisted exactly once on release

        CHECK(!h.handle_mouse_event(mouse_up(0, 0)));              // release without a drag
        CHECK_EQ(saved, 1);

        // Release with no host hook installed must be safe.
        hooks() = Hooks{};
        PerfHud h2;
        h2.set_position(PerfHudPosition::TopLeft);
        CHECK(h2.handle_mouse_event(mouse_down(20, 20)));
        CHECK(h2.handle_mouse_event(mouse_up(20, 20)));
    }

    // --- Rendering into a software renderer ---------------------------------------
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, 320, 200, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    CHECK(ren != nullptr);
    if (ren) {
        // Null renderer is ignored.
        { PerfHud h; h.on_frame_present(nullptr); }

        // Solid theme: opaque background box at the TopLeft anchor.
        {
            PerfHud h;
            h.set_theme(PerfHudTheme::Solid);
            h.set_position(PerfHudPosition::TopLeft);
            clear(ren);
            h.on_frame_present(ren);
            CHECK_EQ(h.last_window_width(), 320);
            CHECK_EQ(h.last_window_height(), 200);
            CHECK_NEAR(h.calculated_x(), 16.0f, 1e-3f);
            CHECK_NEAR(h.calculated_y(), 16.0f, 1e-3f);
            Rgb in = pixel(ren, 16 + 190, 16 + 3);     // inside the box, clear of the text
            CHECK_EQ(in.r, 12); CHECK_EQ(in.g, 12); CHECK_EQ(in.b, 16);
            Rgb out = pixel(ren, 300, 150);            // outside
            CHECK_EQ(out.r, 0); CHECK_EQ(out.g, 0); CHECK_EQ(out.b, 0);
            bool any_text = false;                     // text pixels were drawn
            for (int y = 16 + 5; y < 16 + 30 && !any_text; ++y)
                for (int x = 16 + 8; x < 16 + 100; ++x) {
                    Rgb p = pixel(ren, x, y);
                    if (p.r > 100 || p.g > 100) { any_text = true; break; }
                }
            CHECK(any_text);
            CHECK(!h.is_dragging());
        }

        // Translucent glass theme differs from solid; Minimal draws no box.
        {
            PerfHud h;
            h.set_position(PerfHudPosition::TopLeft);
            h.set_theme(PerfHudTheme::GlassDark);
            clear(ren);
            h.on_frame_present(ren);
            Rgb glass = pixel(ren, 16 + 190, 16 + 3);
            CHECK(glass.b > 0 && glass.b != 16);

            h.set_theme(PerfHudTheme::Minimal);
            clear(ren);
            h.on_frame_present(ren);
            Rgb mini = pixel(ren, 16 + 190, 16 + 3);
            CHECK_EQ(mini.r, 0); CHECK_EQ(mini.g, 0); CHECK_EQ(mini.b, 0);
        }

        // Anchors follow the actual surface size on each present.
        {
            PerfHud h;
            h.set_theme(PerfHudTheme::Solid);
            h.set_position(PerfHudPosition::TopRight);   // computed against the default 1280x720
            clear(ren);
            h.on_frame_present(ren);
            CHECK_NEAR(h.calculated_x(), 320.0f - 200.0f - 16.0f, 1e-3f);
            h.set_position(PerfHudPosition::BottomLeft);
            clear(ren);
            h.on_frame_present(ren);
            CHECK_NEAR(h.calculated_y(), 200.0f - 88.0f - 16.0f, 1e-3f);

            // BottomBlackBar falls back to the corner when the bar is too short...
            h.set_game_viewport({0, 0, 320, 200});
            h.set_position(PerfHudPosition::BottomBlackBar);
            clear(ren);
            h.on_frame_present(ren);
            CHECK_NEAR(h.calculated_x(), 320.0f - 200.0f - 16.0f, 1e-3f);
            // ...and centers in a tall enough bar.
            h.set_game_viewport({0, 0, 320, 40});
            clear(ren);
            h.on_frame_present(ren);
            CHECK_NEAR(h.calculated_x(), (320.0f - 200.0f) * 0.5f, 1e-3f);
            CHECK_NEAR(h.calculated_y(), 40.0f + (160.0f - 88.0f) * 0.5f, 1e-3f);
        }

        // Every mode and theme renders without crashing.
        {
            PerfHud h;
            for (int m = 0; m <= 3; ++m)
                for (int t = 0; t <= 3; ++t) {
                    h.set_mode(static_cast<PerfHudMode>(m));
                    h.set_theme(static_cast<PerfHudTheme>(t));
                    clear(ren);
                    h.on_frame_present(ren);
                }
            CHECK(true);
        }

        // Off draws nothing.
        {
            PerfHud h;
            h.set_mode(PerfHudMode::Off);
            clear(ren);
            h.on_frame_present(ren);
            bool dirty = false;
            for (int y = 0; y < 200 && !dirty; y += 7)
                for (int x = 0; x < 320; x += 5) {
                    Rgb p = pixel(ren, x, y);
                    if (p.r || p.g || p.b) { dirty = true; break; }
                }
            CHECK(!dirty);
        }

        // F10 (edge-triggered) cycles the mode once per press.
        {
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            CHECK(ks != nullptr);
            if (ks) {
                Uint8* mut = const_cast<Uint8*>(ks);
                PerfHud h;
                h.set_mode(PerfHudMode::FpsOnly);
                mut[SDL_SCANCODE_F10] = 0;
                h.on_frame_present(ren);
                CHECK(h.settings().mode == PerfHudMode::FpsOnly);
                mut[SDL_SCANCODE_F10] = 1;
                h.on_frame_present(ren);
                CHECK(h.settings().mode == PerfHudMode::FpsAndFrametime);
                h.on_frame_present(ren);                   // still held: no second cycle
                CHECK(h.settings().mode == PerfHudMode::FpsAndFrametime);
                mut[SDL_SCANCODE_F10] = 0;
                h.on_frame_present(ren);
                mut[SDL_SCANCODE_F10] = 1;
                h.on_frame_present(ren);
                CHECK(h.settings().mode == PerfHudMode::FullWithGraph);
                mut[SDL_SCANCODE_F10] = 0;
            }
        }

        // Drag after a present uses the real surface size for clamping.
        {
            PerfHud h;
            h.set_position(PerfHudPosition::TopLeft);
            h.on_frame_present(ren);                          // window now 320x200
            CHECK(h.handle_mouse_event(mouse_down(20, 20)));
            CHECK(h.handle_mouse_event(mouse_move(4000, 4000)));
            CHECK_NEAR(h.settings().custom_x, 120.0f, 1e-3f);  // 320 - 200
            CHECK_NEAR(h.settings().custom_y, 112.0f, 1e-3f);  // 200 - 88
            h.handle_mouse_event(mouse_up(0, 0));
        }
    }
    if (ren) SDL_DestroyRenderer(ren);
    if (surf) SDL_FreeSurface(surf);
    SDL_Quit();
    return testh::finish("perf_hud");
}
