#include "test_harness.h"
#include "gbaimp/frame_interpolator.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

using namespace gbaimp;

static uint32_t read_px(SDL_Renderer* ren, int x, int y) {
    SDL_Rect r = { x, y, 1, 1 };
    uint32_t px = 0;
    SDL_RenderReadPixels(ren, &r, SDL_PIXELFORMAT_ARGB8888, &px, sizeof(px));
    return px;
}

static double now_ms() {
    return 1000.0 * double(SDL_GetPerformanceCounter()) / double(SDL_GetPerformanceFrequency());
}

int main() {
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    const bool video = SDL_Init(SDL_INIT_VIDEO) == 0;

    // --- Defaults and setters -------------------------------------------------------
    {
        FrameInterpolator f;
        CHECK(f.settings().mode == FramerateMode::Fps60);
        CHECK(f.settings().smoothing == MotionSmoothingMode::SmoothBlend);
        f.set_smoothing(MotionSmoothingMode::MotionAdaptive);
        CHECK(f.settings().smoothing == MotionSmoothingMode::MotionAdaptive);
        f.set_smoothing(MotionSmoothingMode::Off);
        CHECK(f.settings().smoothing == MotionSmoothingMode::Off);
        f.set_mode(FramerateMode::Fps120);
        CHECK(f.settings().mode == FramerateMode::Fps120);
        f.settings().mode = FramerateMode::Fps144;                 // live-editable settings
        CHECK_NEAR(f.target_fps(), 144.0, 1e-9);
    }

    // --- Target rate per mode ----------------------------------------------------------
    {
        FrameInterpolator f;
        struct Row { FramerateMode m; double fps; };
        const Row rows[] = {
            {FramerateMode::Fps60, 60.0}, {FramerateMode::Fps120, 120.0},
            {FramerateMode::Fps144, 144.0},
            {FramerateMode::DisplayNative, 60.0},   // 60 until a display has been queried
            {FramerateMode::Uncapped, 240.0},
        };
        for (const Row& r : rows) {
            f.set_mode(r.m);
            CHECK_NEAR(f.target_fps(), r.fps, 1e-9);
            CHECK_NEAR(f.target_frametime_ms(), 1000.0 / r.fps, 1e-9);
        }
        // Out-of-range mode falls back to 60.
        f.settings().mode = static_cast<FramerateMode>(99);
        CHECK_NEAR(f.target_fps(), 60.0, 1e-9);
    }

    // --- Display info is safe without a renderer/window ---------------------------------
    {
        FrameInterpolator f;
        CHECK_EQ(f.detected_display_info(nullptr), std::string("1920x1080 @ 60Hz"));
    }

    // --- Rendering paths on a software renderer (no window) ------------------------------
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, 64, 48, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    CHECK(ren != nullptr);
    if (ren) {
        auto paint = [&](uint8_t v) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(ren, v, v, v, 255);
            SDL_RenderClear(ren);
        };

        FrameInterpolator f;
        f.on_present(nullptr, nullptr);                          // ignored

        // 60 fps: nothing drawn, no pacing.
        f.set_mode(FramerateMode::Fps60);
        paint(40);
        const uint32_t before = read_px(ren, 5, 5);
        f.on_present(ren, nullptr);
        f.on_present(ren, nullptr);
        CHECK_EQ(read_px(ren, 5, 5), before);

        // No window attached: display info keeps the defaults.
        CHECK_EQ(f.detected_display_info(ren), std::string("1920x1080 @ 60Hz"));

        // 120 fps with smoothing: a present never returns earlier than one sub-frame
        // (8.33 ms) after the previous one.
        f.set_mode(FramerateMode::Fps120);
        f.set_smoothing(MotionSmoothingMode::SmoothBlend);
        paint(40);
        f.on_present(ren, nullptr);
        const double t0 = now_ms();
        f.on_present(ren, nullptr);
        const double elapsed = now_ms() - t0;
        CHECK(elapsed >= 8.0);            // paced to the 120 Hz period
        CHECK(elapsed < 1000.0);          // and it does not hang

        // Smoothing off: no pacing wait, so two presents are quick.
        f.set_smoothing(MotionSmoothingMode::Off);
        f.on_present(ren, nullptr);
        const double t1 = now_ms();
        f.on_present(ren, nullptr);
        CHECK(now_ms() - t1 < 7.0);

        // Every mode/smoothing combination runs through on_present.
        const FramerateMode modes[] = {FramerateMode::Fps144, FramerateMode::DisplayNative, FramerateMode::Uncapped};
        const MotionSmoothingMode sm[] = {MotionSmoothingMode::Off, MotionSmoothingMode::SmoothBlend, MotionSmoothingMode::MotionAdaptive};
        for (FramerateMode m : modes)
            for (MotionSmoothingMode s : sm) {
                f.set_mode(m);
                f.set_smoothing(s);
                paint(90);
                f.on_present(ren, nullptr);
                f.on_present(ren, nullptr);
            }
        CHECK(true);

        // --- Blending against the previous presented frame ---------------------------------
        {
            FrameInterpolator g;
            std::vector<uint32_t> seen;                     // pixel (5,5) at each extra present
            std::vector<uint32_t> seen_r;                   // pixel (40,30)
            g.set_present_hook([&](SDL_Renderer* r) {
                seen.push_back(read_px(r, 5, 5));
                seen_r.push_back(read_px(r, 40, 30));
            });
            auto chan = [](uint32_t px) { return int(px & 0xFF); };
            auto frame = [&](uint8_t bg, uint8_t rect_v) {
                paint(bg);
                SDL_SetRenderDrawColor(ren, rect_v, rect_v, rect_v, 255);
                SDL_Rect rc = { 32, 24, 16, 12 };
                SDL_RenderFillRect(ren, &rc);
            };

            // 60 Hz and smoothing Off: no readback at all.
            g.set_mode(FramerateMode::Fps60);
            frame(40, 40); g.on_present(ren, nullptr); frame(200, 200); g.on_present(ren, nullptr);
            CHECK_EQ(g.readbacks(), uint64_t(0));
            g.set_mode(FramerateMode::Fps120);
            g.set_smoothing(MotionSmoothingMode::Off);
            frame(40, 40); g.on_present(ren, nullptr); frame(200, 200); g.on_present(ren, nullptr);
            CHECK_EQ(g.readbacks(), uint64_t(0));
            CHECK_EQ(g.extra_presents(), uint64_t(0));

            // SmoothBlend at 120 Hz: first frame only primes the history.
            g.set_smoothing(MotionSmoothingMode::SmoothBlend);
            frame(40, 40);
            g.on_present(ren, nullptr);
            CHECK_EQ(g.readbacks(), uint64_t(1));
            CHECK_EQ(g.extra_presents(), uint64_t(0));
            CHECK_EQ(chan(read_px(ren, 5, 5)), 40);          // back buffer untouched

            // Second frame: one intermediate present = 50/50 of previous (40) and current (200).
            frame(200, 80);
            g.on_present(ren, nullptr);
            CHECK_EQ(g.extra_presents(), uint64_t(1));
            CHECK_EQ(seen.size(), size_t(1));
            if (!seen.empty()) {
                CHECK(std::abs(chan(seen[0]) - 120) <= 2);
                CHECK(std::abs(chan(seen_r[0]) - 60) <= 2);  // (40 + 80) / 2
            }
            // The real current frame is restored for the host to present.
            CHECK_EQ(chan(read_px(ren, 5, 5)), 200);
            CHECK_EQ(chan(read_px(ren, 40, 30)), 80);

            // The "previous" frame is the last real frame (200), not the blend.
            seen.clear();
            frame(100, 100);
            g.on_present(ren, nullptr);
            CHECK_EQ(seen.size(), size_t(1));
            if (!seen.empty()) CHECK(std::abs(chan(seen[0]) - 150) <= 2);   // (200 + 100) / 2

            // MotionAdaptive: big changes blend, tiny changes keep the current value.
            g.set_smoothing(MotionSmoothingMode::MotionAdaptive);
            seen.clear(); seen_r.clear();
            frame(100, 100);
            g.on_present(ren, nullptr);                      // identical frame
            CHECK_EQ(seen.size(), size_t(1));
            if (!seen.empty()) CHECK_EQ(chan(seen[0]), 100);
            seen.clear(); seen_r.clear();
            frame(106, 220);                                 // bg: +6 (static), rect: +120 (motion)
            g.on_present(ren, nullptr);
            CHECK_EQ(seen.size(), size_t(1));
            if (!seen.empty()) {
                CHECK_EQ(chan(seen[0]), 106);                // below the motion threshold: sharp
                CHECK(std::abs(chan(seen_r[0]) - 160) <= 2); // (100 + 220) / 2
            }
            CHECK_EQ(chan(read_px(ren, 40, 30)), 220);

            // 240 Hz: three intermediates at 1/4, 2/4, 3/4 of the way to the current frame.
            g.set_smoothing(MotionSmoothingMode::SmoothBlend);
            g.set_mode(FramerateMode::Uncapped);
            seen.clear();
            frame(40, 40);
            g.on_present(ren, nullptr);
            CHECK_EQ(seen.size(), size_t(3));
            if (seen.size() == 3) {
                // previous bg = 106 -> current 40
                CHECK(std::abs(chan(seen[0]) - (106 + (40 - 106) / 4)) <= 3);
                CHECK(std::abs(chan(seen[1]) - (106 + (40 - 106) / 2)) <= 3);
                CHECK(std::abs(chan(seen[2]) - (106 + (40 - 106) * 3 / 4)) <= 3);
            }

            // Turning smoothing Off and on again drops the stale history.
            g.set_mode(FramerateMode::Fps120);
            g.set_smoothing(MotionSmoothingMode::Off);
            g.on_present(ren, nullptr);
            g.set_smoothing(MotionSmoothingMode::SmoothBlend);
            const uint64_t extras_before = g.extra_presents();
            frame(250, 250);
            g.on_present(ren, nullptr);
            CHECK_EQ(g.extra_presents(), extras_before);     // primes only, no blend with old frame

            // A long stall (> 100 ms) also restarts history instead of blending across it.
            SDL_Delay(130);
            frame(10, 10);
            g.on_present(ren, nullptr);
            CHECK_EQ(g.extra_presents(), extras_before);

            // Renderer state is preserved (viewport, clip, scale).
            SDL_Rect vp = { 4, 4, 40, 30 }, clip = { 6, 6, 10, 10 };
            SDL_RenderSetViewport(ren, &vp);
            SDL_RenderSetClipRect(ren, &clip);
            SDL_RenderSetScale(ren, 2.0f, 2.0f);
            SDL_Rect vb, cb;
            SDL_RenderGetViewport(ren, &vb);
            SDL_RenderGetClipRect(ren, &cb);
            frame(30, 30);
            g.on_present(ren, nullptr);
            frame(60, 60);
            g.on_present(ren, nullptr);
            SDL_Rect vp2; SDL_RenderGetViewport(ren, &vp2);
            CHECK(vp2.x == vb.x && vp2.y == vb.y && vp2.w == vb.w && vp2.h == vb.h);
            SDL_Rect cl2; SDL_RenderGetClipRect(ren, &cl2);
            CHECK(SDL_RenderIsClipEnabled(ren) == SDL_TRUE && cl2.x == cb.x && cl2.y == cb.y && cl2.w == cb.w && cl2.h == cb.h);
            float sx = 0, sy = 0; SDL_RenderGetScale(ren, &sx, &sy);
            CHECK_NEAR(sx, 2.0f, 1e-6f);
            SDL_RenderSetScale(ren, 1.0f, 1.0f);
            SDL_RenderSetClipRect(ren, nullptr);
            SDL_RenderSetViewport(ren, nullptr);

            // Rendering into a texture target: no-op.
            SDL_Texture* target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 8, 8);
            if (target) {
                const uint64_t rb = g.readbacks();
                SDL_SetRenderTarget(ren, target);
                g.on_present(ren, nullptr);
                SDL_SetRenderTarget(ren, nullptr);
                CHECK_EQ(g.readbacks(), rb);
                SDL_DestroyTexture(target);
            }

            // A renderer with a different output size restarts history without blending
            // across sizes, then blends normally at the new size.
            SDL_Surface* surf2 = SDL_CreateRGBSurfaceWithFormat(0, 32, 24, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_Renderer* ren2 = surf2 ? SDL_CreateSoftwareRenderer(surf2) : nullptr;
            CHECK(ren2 != nullptr);
            if (ren2) {
                g.release_resources();                       // ren stays alive: free its textures
                std::vector<uint32_t> seen2;
                g.set_present_hook([&](SDL_Renderer* r) { seen2.push_back(read_px(r, 2, 2)); });
                auto frame2 = [&](uint8_t v) {
                    SDL_SetRenderDrawColor(ren2, v, v, v, 255);
                    SDL_RenderClear(ren2);
                };
                frame2(0);   g.on_present(ren2, nullptr);
                frame2(100); g.on_present(ren2, nullptr);
                CHECK_EQ(seen2.size(), size_t(1));
                if (!seen2.empty()) CHECK(std::abs(chan(seen2[0]) - 50) <= 2);
                CHECK_EQ(chan(read_px(ren2, 2, 2)), 100);
                g.release_resources();
                SDL_DestroyRenderer(ren2);
            }
            if (surf2) SDL_FreeSurface(surf2);
            g.release_resources();
        }
    }

    // --- With a (dummy) window: the display mode is queried and reported ------------------
    if (video) {
        SDL_Window* win = SDL_CreateWindow("t", 0, 0, 64, 48, SDL_WINDOW_HIDDEN);
        SDL_Renderer* wren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : nullptr;
        if (wren) {
            FrameInterpolator f;
            f.set_mode(FramerateMode::DisplayNative);
            const std::string info = f.detected_display_info(wren);
            int w = 0, h = 0, hz = 0;
            CHECK_EQ(std::sscanf(info.c_str(), "%dx%d @ %dHz", &w, &h, &hz), 3);
            CHECK(w > 0 && h > 0 && hz > 0);
            CHECK_NEAR(f.target_fps(), double(hz), 1e-9);      // DisplayNative follows the display
            CHECK_EQ(f.detected_display_info(wren), info);     // stable once queried
        }
        if (wren) SDL_DestroyRenderer(wren);
        if (win) SDL_DestroyWindow(win);
    }

    if (ren) SDL_DestroyRenderer(ren);
    if (surf) SDL_FreeSurface(surf);
    SDL_Quit();
    return testh::finish("frame_interpolator");
}
