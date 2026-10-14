#include "gbaimp/game_host.h"

#include "gbaimp/audio_device.h"
#include "gbaimp/audio_pipeline.h"
#include "gbaimp/frame_interpolator.h"
#include "gbaimp/perf_hud.h"
#include "gbaimp/screen_filters.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace gbaimp {
namespace {

enum { kCntPoll, kCntUpdateTexture, kCntRenderCopy, kCntRenderPresent, kCntQueueAudio, kCntOpenAudioDevice, kCntN };
std::atomic<unsigned long long> g_calls[kCntN];
inline void count_call(int i) { g_calls[i].fetch_add(1, std::memory_order_relaxed); }

// The configuration is copied on install. Handlers read it through an atomically published
// pointer; superseded copies are intentionally leaked (an install is rare and tiny).
std::atomic<const GameHostConfig*> g_config{nullptr};
std::atomic<bool> g_log_counters{false};
std::mutex g_install_mutex;

const GameHostConfig& cfg() {
    static const GameHostConfig kDefault;
    const GameHostConfig* c = g_config.load(std::memory_order_acquire);
    return c ? *c : kDefault;
}

bool logging_enabled(const char* env_name) {
    if (!env_name || !*env_name) return false;
    const char* v = std::getenv(env_name);
    return v && *v && std::strcmp(v, "0") != 0;
}

void print_counters() {
    const GameHostConfig& c = cfg();
    std::fprintf(stderr,
                 "[%s] sdl intercept calls: SDL_PollEvent=%llu SDL_UpdateTexture=%llu "
                 "SDL_RenderCopy=%llu SDL_RenderPresent=%llu SDL_QueueAudio=%llu SDL_OpenAudioDevice=%llu\n",
                 c.tag,
                 g_calls[kCntPoll].load(), g_calls[kCntUpdateTexture].load(),
                 g_calls[kCntRenderCopy].load(), g_calls[kCntRenderPresent].load(),
                 g_calls[kCntQueueAudio].load(), g_calls[kCntOpenAudioDevice].load());
    const AudioWrapStats a = audio_wrap_stats();
    std::fprintf(stderr,
                 "[%s] audio: devices_wrapped=%llu callbacks=%llu callback_frames_processed=%llu "
                 "queue_calls=%llu level rms %.1f -> %.1f peak %d -> %d (over %llu samples)\n",
                 c.tag,
                 static_cast<unsigned long long>(a.devices_wrapped),
                 static_cast<unsigned long long>(a.callbacks),
                 static_cast<unsigned long long>(a.frames_processed),
                 static_cast<unsigned long long>(a.queue_calls),
                 a.rms_before, a.rms_after, a.peak_before, a.peak_after,
                 static_cast<unsigned long long>(a.metered_samples));
    if (c.print_stats) c.print_stats(c.tag);
}

// --- handlers ----------------------------------------------------------------------------

int poll_event_handler(SDL_Event* event) {
    count_call(kCntPoll);
    const GameHostConfig& c = cfg();
    if (c.on_poll) c.on_poll();
    while (true) {
        const int res = sdl_real().poll_event(event);
        if (!res) return 0;
        if (event) {
            if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) &&
                event->key.keysym.sym == SDLK_ESCAPE) {
                event->key.keysym.scancode = SDL_SCANCODE_ESCAPE;
            }
            if (c.on_event && c.on_event(*event)) continue;
            if (PerfHud::instance().handle_mouse_event(*event)) continue;
        }
        return res;
    }
}

int update_texture_handler(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch) {
    count_call(kCntUpdateTexture);
    if (!texture || !pixels) return sdl_real().update_texture(texture, rect, pixels, pitch);

    uint32_t format = 0;
    int access = 0, tex_w = 0, tex_h = 0;
    if (SDL_QueryTexture(texture, &format, &access, &tex_w, &tex_h) == 0 &&
        format == SDL_PIXELFORMAT_RGB24 && pitch >= tex_w * 3) {
        const GameHostConfig& c = cfg();
        if (tex_h == 160 && (tex_w == 240 || tex_w == 284) && c.on_native_frame &&
            (!c.wants_native_frame || c.wants_native_frame())) {
            c.on_native_frame(pixels, pitch, tex_w, tex_h);   // raw PPU framebuffer
        }

        auto& filters = ScreenFilters::instance();
        if (filters.settings().color_profile != ColorProfile::Raw) {
            static std::vector<uint8_t> s_color_buf;
            const size_t needed = static_cast<size_t>(pitch) * tex_h;
            if (s_color_buf.size() < needed) s_color_buf.resize(needed);
            std::memcpy(s_color_buf.data(), pixels, needed);
            filters.apply_color_correction(s_color_buf.data(), tex_w, tex_h);
            pixels = s_color_buf.data();
        }
    }
    return sdl_real().update_texture(texture, rect, pixels, pitch);
}

int render_copy_handler(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect) {
    count_call(kCntRenderCopy);
    if (!renderer || !texture) return sdl_real().render_copy(renderer, texture, srcrect, dstrect);

    // Only the final backbuffer is intercepted; an offscreen target passes straight through.
    if (SDL_GetRenderTarget(renderer) != nullptr) return sdl_real().render_copy(renderer, texture, srcrect, dstrect);

    uint32_t format = 0;
    int access = 0, tex_w = 0, tex_h = 0;
    if (SDL_QueryTexture(texture, &format, &access, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0)
        return sdl_real().render_copy(renderer, texture, srcrect, dstrect);
    if (!is_gba_game_texture(format, access, tex_w, tex_h))
        return sdl_real().render_copy(renderer, texture, srcrect, dstrect);

    int out_w = 0, out_h = 0;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    if (out_w <= 0 || out_h <= 0) return sdl_real().render_copy(renderer, texture, srcrect, dstrect);

    // Always normalise the renderer to full-window 1:1 pixel coordinates.
    int lw = 0, lh = 0;
    SDL_RenderGetLogicalSize(renderer, &lw, &lh);
    if (lw != 0 || lh != 0) SDL_RenderSetLogicalSize(renderer, 0, 0);
    float sx = 1.0f, sy = 1.0f;
    SDL_RenderGetScale(renderer, &sx, &sy);
    if (sx != 1.0f || sy != 1.0f) SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_RenderSetViewport(renderer, nullptr);

    const GameHostConfig& c = cfg();
    SDL_Rect src{};
    SDL_Rect dst{};
    int base_w = 240;
    int base_h = 160;
    (c.compute_viewport ? c.compute_viewport : &default_game_viewport)(tex_w, tex_h, out_w, out_h,
                                                                       &src, &dst, &base_w, &base_h);
    PerfHud::instance().set_game_viewport(dst);

    // Clear letterbox / pillarbox areas outside the game viewport to pure black.
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    if (dst.x > 0) { SDL_Rect bar = {0, 0, dst.x, out_h}; SDL_RenderFillRect(renderer, &bar); }
    if (dst.x + dst.w < out_w) { SDL_Rect bar = {dst.x + dst.w, 0, out_w - (dst.x + dst.w), out_h}; SDL_RenderFillRect(renderer, &bar); }
    if (dst.y > 0) { SDL_Rect bar = {0, 0, out_w, dst.y}; SDL_RenderFillRect(renderer, &bar); }
    if (dst.y + dst.h < out_h) { SDL_Rect bar = {0, dst.y + dst.h, out_w, out_h - (dst.y + dst.h)}; SDL_RenderFillRect(renderer, &bar); }

    const int res = sdl_real().render_copy(renderer, texture, &src, &dst);

    ScreenFilters::instance().render_mask(renderer, &dst, base_w, base_h);
    if (res == 0 && c.after_game_image) c.after_game_image(renderer, dst);
    return res;
}

void render_present_handler(SDL_Renderer* renderer) {
    count_call(kCntRenderPresent);
    // With diagnostics on, also report every 300 presents (~5 s), so the counters are
    // visible even when the process is killed instead of exiting normally.
    if (g_log_counters.load(std::memory_order_relaxed) &&
        g_calls[kCntRenderPresent].load(std::memory_order_relaxed) % 300 == 1) {
        print_counters();
    }
    if (!renderer) return;
    const GameHostConfig& c = cfg();
    if (c.on_present_begin) c.on_present_begin();

    FrameInterpolator::instance().on_present(renderer, &PerfHud::instance().game_viewport());
    PerfHud::instance().on_frame_present(renderer);

    if (c.render_present_overlay) {
        int win_w = 0, win_h = 0;
        SDL_GetRendererOutputSize(renderer, &win_w, &win_h);
        c.render_present_overlay(renderer, win_w, win_h);
    }
    if (c.before_present) c.before_present(renderer);
    sdl_real().render_present(renderer);
}

// Direct-queue audio (GBARECOMP_AUDIO_DIRECT=1): processed with the channel count / rate of
// the device as it was actually opened.
int queue_audio_handler(SDL_AudioDeviceID dev, const void* data, Uint32 len) {
    count_call(kCntQueueAudio);
    return queue_audio_processed(dev, data, len);
}

// Default audio path of gbarecomp: a pull callback. The library substitutes a trampoline that
// runs the original callback and processes the buffer it filled on SDL's audio thread.
SDL_AudioDeviceID open_audio_device_handler(const char* device, int iscapture, const SDL_AudioSpec* desired,
                                            SDL_AudioSpec* obtained, int allowed_changes) {
    count_call(kCntOpenAudioDevice);
    return open_audio_device_processed(device, iscapture, desired, obtained, allowed_changes);
}

} // namespace

bool is_gba_game_texture(uint32_t format, int access, int tex_w, int tex_h) {
    if (tex_w <= 0 || tex_h <= 0) return false;
    const bool streaming_gba = (access == SDL_TEXTUREACCESS_STREAMING &&
                                format == SDL_PIXELFORMAT_RGB24 &&
                                tex_h == 160 && (tex_w == 240 || tex_w == 284));
    const bool scaled_target = (access == SDL_TEXTUREACCESS_TARGET &&
                                format == SDL_PIXELFORMAT_RGBA8888 &&
                                tex_h % 160 == 0 &&
                                ((tex_w % 284 == 0 && tex_w / 284 == tex_h / 160) ||
                                 (tex_w % 240 == 0 && tex_w / 240 == tex_h / 160)));
    return streaming_gba || scaled_target;
}

void default_game_viewport(int tex_w, int tex_h, int win_w, int win_h,
                           SDL_Rect* out_src, SDL_Rect* out_dst, int* out_base_w, int* out_base_h) {
    if (!out_src || !out_dst || tex_w <= 0 || tex_h <= 0 || win_w <= 0 || win_h <= 0) return;
    const int factor = (tex_h / 160 > 0) ? (tex_h / 160) : 1;
    const int base_w = std::max(1, tex_w / factor);
    const int base_h = 160;
    out_src->x = 0;
    out_src->y = 0;
    out_src->w = tex_w;
    out_src->h = tex_h;
    if (out_base_w) *out_base_w = base_w;
    if (out_base_h) *out_base_h = base_h;
    const float s = std::min(static_cast<float>(win_w) / static_cast<float>(base_w),
                             static_cast<float>(win_h) / static_cast<float>(base_h));
    const int dw = static_cast<int>(static_cast<float>(base_w) * s);
    const int dh = static_cast<int>(static_cast<float>(base_h) * s);
    out_dst->x = (win_w - dw) / 2;
    out_dst->y = (win_h - dh) / 2;
    out_dst->w = dw;
    out_dst->h = dh;
}

GameHostStats game_host_stats() {
    GameHostStats s;
    s.poll_event = g_calls[kCntPoll].load();
    s.update_texture = g_calls[kCntUpdateTexture].load();
    s.render_copy = g_calls[kCntRenderCopy].load();
    s.render_present = g_calls[kCntRenderPresent].load();
    s.queue_audio = g_calls[kCntQueueAudio].load();
    s.open_audio_device = g_calls[kCntOpenAudioDevice].load();
    return s;
}

bool install_game_host(const GameHostConfig& config) {
    std::lock_guard<std::mutex> lock(g_install_mutex);
    GameHostConfig* copy = new GameHostConfig(config);   // published; old copies are leaked on purpose
    if (!copy->tag) copy->tag = "gbaimp";
    g_config.store(copy, std::memory_order_release);

    SdlHandlers h;
    h.poll_event = &poll_event_handler;
    h.update_texture = &update_texture_handler;
    h.render_copy = &render_copy_handler;
    h.render_present = &render_present_handler;
    h.queue_audio = &queue_audio_handler;
    h.open_audio_device = &open_audio_device_handler;
    set_sdl_handlers(h);

    const bool verbose = logging_enabled(copy->log_env);
    static std::once_flag s_once;
    std::call_once(s_once, [verbose] {
        if (verbose) {
            g_log_counters.store(true);
            set_audio_metering(true);   // before/after level meter for the log
            std::atexit(&print_counters);
        }
    });

    bool active = false;
#if defined(_WIN32)
    SdlInstallOptions opt;
    static bool s_verbose;
    s_verbose = verbose;
    opt.log = [](const char* line) { if (s_verbose) std::fprintf(stderr, "%s\n", line); };
    SdlInstallStatus st;
    active = install_sdl_interposers(h, &st, &opt);
    if (!active || st.slots_failed > 0 || st.slots_skipped_foreign > 0) {
        std::fprintf(stderr,
                     "[%s] warning: SDL interposers incomplete (patched=%d failed=%d foreign=%d%s%s); "
                     "enhancements that hook SDL (filters, HUD, widescreen) may be inactive\n",
                     copy->tag, st.slots_patched, st.slots_failed, st.slots_skipped_foreign,
                     st.error.empty() ? "" : " error=", st.error.c_str());
    }
#endif
    return active;
}

} // namespace gbaimp
