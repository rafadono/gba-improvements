// Minimal gba-improvements host.
//
// Uses ONLY this library and SDL2 (no recompilation runtime). It draws a
// synthetic 240x160 RGB24 pattern into a streaming texture and runs it through
// the same calls a real host makes from its SDL intercepts:
//   - ScreenFilters::apply_color_correction  (before SDL_UpdateTexture)
//   - ScreenFilters::render_mask             (after the game image is drawn)
//   - FrameInterpolator::on_present + PerfHud::on_frame_present (before present)
//   - gbaimp::process_audio_block            (before audio is queued)
//
// Usage: gbaimp_example_minimal [--frames N] [--headless]
//   --frames N   run N frames then exit with 0 (default 120, 0 = until closed)
//   --headless   no window: dummy SDL drivers and an offscreen software renderer
//                (also selected automatically if SDL_VIDEODRIVER=dummy)

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "gbaimp/audio_dsp.h"
#include "gbaimp/audio_pipeline.h"
#include "gbaimp/frame_interpolator.h"
#include "gbaimp/perf_hud.h"
#include "gbaimp/screen_filters.h"

namespace {

constexpr int kGbaW = 240;
constexpr int kGbaH = 160;
constexpr int kSampleRate = 32768;

void draw_test_pattern(std::vector<uint8_t>& rgb, int frame) {
    for (int y = 0; y < kGbaH; ++y) {
        for (int x = 0; x < kGbaW; ++x) {
            uint8_t* p = &rgb[(static_cast<size_t>(y) * kGbaW + x) * 3];
            // Horizontal colour ramp, vertical ramp, checker, and a moving bar.
            p[0] = static_cast<uint8_t>(x * 255 / (kGbaW - 1));
            p[1] = static_cast<uint8_t>(y * 255 / (kGbaH - 1));
            p[2] = static_cast<uint8_t>(((x / 16) + (y / 16)) & 1 ? 200 : 40);
            const int bar = (frame * 3) % kGbaW;
            if (x >= bar && x < bar + 8) p[0] = p[1] = p[2] = 255;
        }
    }
}

uint64_t checksum(const uint8_t* d, size_t n) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 1099511628211ull; }
    return h;
}

} // namespace

int main(int argc, char** argv) {
    int frames = 120;
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--headless")) headless = true;
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else { std::fprintf(stderr, "Usage: %s [--frames N] [--headless]\n", argv[0]); return 2; }
    }
    if (const char* drv = SDL_getenv("SDL_VIDEODRIVER")) {
        if (!std::strcmp(drv, "dummy")) headless = true;
    }
    if (headless) {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        if (frames <= 0) frames = 120;  // never run forever without a window
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Surface* offscreen = nullptr;
    SDL_Renderer* renderer = nullptr;
    const int out_w = kGbaW * 4, out_h = kGbaH * 4;
    if (headless) {
        offscreen = SDL_CreateRGBSurfaceWithFormat(0, out_w, out_h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (offscreen) renderer = SDL_CreateSoftwareRenderer(offscreen);
    } else {
        window = SDL_CreateWindow("gba-improvements minimal host", SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, out_w, out_h, SDL_WINDOW_RESIZABLE);
        if (window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (window && !renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        std::fprintf(stderr, "Could not create renderer: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, kGbaW, kGbaH);
    if (!tex) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return 1;
    }

    // Configure the modules (a real host loads these from its own settings).
    auto& filters = gbaimp::ScreenFilters::instance();
    filters.set_color_profile(gbaimp::ColorProfile::Agb001);
    filters.set_mask_type(gbaimp::ScreenMaskType::LcdGrid);
    filters.set_mask_intensity(0.4f);
    gbaimp::FrameInterpolator::instance().set_mode(gbaimp::FramerateMode::Fps120);
    gbaimp::PerfHud::instance().set_mode(gbaimp::PerfHudMode::FullWithGraph);
    gbaimp::AudioDsp::instance().apply_preset(gbaimp::EqPreset::CrispModern);

    std::vector<uint8_t> raw(static_cast<size_t>(kGbaW) * kGbaH * 3);
    std::vector<uint8_t> corrected;
    std::vector<int16_t> audio(2 * (kSampleRate / 60));  // one 60 Hz frame, stereo
    double phase = 0.0;
    int frames_done = 0;
    uint64_t raw_sum = 0, corrected_sum = 0, out_sum = 0;
    double peak_in = 0.0, peak_out = 0.0;
    const uint64_t t0 = SDL_GetPerformanceCounter();

    bool quit = false;
    for (int frame = 0; !quit && (frames <= 0 || frame < frames); ++frame) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) quit = true;
            else if (gbaimp::PerfHud::instance().handle_mouse_event(ev)) continue;
        }

        // 1. "Game" frame -> colour correction on a copy -> texture upload.
        draw_test_pattern(raw, frame);
        corrected = raw;
        filters.apply_color_correction(corrected.data(), kGbaW, kGbaH);
        SDL_UpdateTexture(tex, nullptr, corrected.data(), kGbaW * 3);
        if (frame == 0) {
            raw_sum = checksum(raw.data(), raw.size());
            corrected_sum = checksum(corrected.data(), corrected.size());
        }

        // 2. Draw the game image into the largest aspect-correct integer-scaled rect.
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(renderer, &ow, &oh);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        const int scale_x = ow / kGbaW, scale_y = oh / kGbaH;
        int scale = scale_x < scale_y ? scale_x : scale_y;
        if (scale < 1) scale = 1;
        SDL_Rect dst{(ow - kGbaW * scale) / 2, (oh - kGbaH * scale) / 2, kGbaW * scale, kGbaH * scale};
        gbaimp::PerfHud::instance().set_game_viewport(dst);
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        filters.render_mask(renderer, &dst, kGbaW, kGbaH);

        // 3. Generated tone through the audio chain (what SDL_QueueAudio would carry).
        for (size_t i = 0; i + 1 < audio.size(); i += 2) {
            const double s = 0.5 * std::sin(phase);
            phase += 2.0 * 3.14159265358979 * 440.0 / kSampleRate;
            audio[i] = static_cast<int16_t>(s * 32767.0 * 0.8);
            audio[i + 1] = static_cast<int16_t>(s * 32767.0 * 0.4);  // panned
            peak_in = std::fmax(peak_in, std::fabs(static_cast<double>(audio[i])));
        }
        gbaimp::process_audio_block(audio.data(), audio.size());
        for (int16_t v : audio) peak_out = std::fmax(peak_out, std::fabs(static_cast<double>(v)));

        // 4. Present: interpolator + HUD, then the real present.
        gbaimp::FrameInterpolator::instance().on_present(renderer, &dst);
        gbaimp::PerfHud::instance().on_frame_present(renderer);
        SDL_RenderPresent(renderer);
        ++frames_done;
    }

    if (headless && offscreen) {
        std::vector<uint8_t> px(static_cast<size_t>(out_w) * out_h * 4);
        if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), out_w * 4) == 0)
            out_sum = checksum(px.data(), px.size());
    }
    const double secs = static_cast<double>(SDL_GetPerformanceCounter() - t0) /
                        static_cast<double>(SDL_GetPerformanceFrequency());

    std::printf("gbaimp minimal host: %d frames in %.2f s (%.1f fps) [%s, video driver %s]\n", frames_done, secs,
                secs > 0 ? frames_done / secs : 0.0, headless ? "headless" : "windowed",
                SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?");
    std::printf("  frame0 checksum raw=%016llx corrected=%016llx (%s)\n", (unsigned long long)raw_sum,
                (unsigned long long)corrected_sum, raw_sum != corrected_sum ? "color correction applied" : "unchanged");
    if (headless) std::printf("  final frame checksum=%016llx\n", (unsigned long long)out_sum);
    std::printf("  audio peak in=%.0f out=%.0f (int16)\n", peak_in, peak_out);

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    if (offscreen) SDL_FreeSurface(offscreen);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
