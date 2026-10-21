#define SDL_MAIN_HANDLED  // the test provides plain main(); no SDL_main redirection
#include "test_harness.h"
#include "gbaimp/game_host.h"

#include <vector>

// Linux: the test executable is linked with -Wl,--wrap=SDL_* (see CMakeLists.txt) and expands the
// shim macro once. Windows: install_game_host() patches the IAT of this executable instead.
#if !defined(_WIN32)
GBAIMP_DEFINE_LD_WRAPS()
#endif

using namespace gbaimp;

namespace {
int g_poll_hook = 0, g_native_frames = 0, g_viewport_calls = 0, g_after_image = 0;
int g_present_begin = 0, g_overlay = 0, g_before_present = 0;
int g_last_win_w = 0, g_last_win_h = 0;
SDL_Rect g_last_dst{};
bool g_native_wanted = true;

void on_poll() { ++g_poll_hook; }
bool wants_native() { return g_native_wanted; }
void on_native(const void*, int, int, int) { ++g_native_frames; }
void viewport(int tex_w, int tex_h, int win_w, int win_h, SDL_Rect* src, SDL_Rect* dst, int* bw, int* bh) {
    ++g_viewport_calls;
    default_game_viewport(tex_w, tex_h, win_w, win_h, src, dst, bw, bh);
    dst->x += 4;   // visibly different from the default placement
}
void after_image(SDL_Renderer*, const SDL_Rect& dst) { ++g_after_image; g_last_dst = dst; }
void present_begin() { ++g_present_begin; }
void overlay(SDL_Renderer*, int w, int h) { ++g_overlay; g_last_win_w = w; g_last_win_h = h; }
void before_present(SDL_Renderer*) { ++g_before_present; }
} // namespace

int main() {
    SDL_SetMainReady();

    // --- pure helpers ---------------------------------------------------------------
    CHECK(is_gba_game_texture(SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 240, 160));
    CHECK(is_gba_game_texture(SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 284, 160));
    CHECK(is_gba_game_texture(SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 256, 160));    // 16:10
    CHECK(is_gba_game_texture(SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 427, 160));    // 16:9 wide
    CHECK(is_gba_game_texture(SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 569, 160));    // 32:9
    CHECK(is_gba_game_texture(SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 240, 180));    // taller view
    CHECK(!is_gba_game_texture(SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 239, 160));   // below native
    CHECK(!is_gba_game_texture(SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 1000, 160));  // wider than 4:1
    CHECK(!is_gba_game_texture(SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 4096, 2048)); // too big
    CHECK(!is_gba_game_texture(SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, 240, 160));
    CHECK(is_gba_game_texture(SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 720, 480));   // 240*3 x 160*3
    CHECK(is_gba_game_texture(SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 852, 480));   // 284*3 x 160*3
    CHECK(!is_gba_game_texture(SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 720, 100));   // below native height
    CHECK(is_gba_game_texture(SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 1119, 480));   // 373 x 3
    CHECK(!is_gba_game_texture(SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 0, 0));

    {   // 3:2 texture into a 1920x1080 window: height-limited, centred, full texture as source.
        SDL_Rect src{}, dst{};
        int bw = 0, bh = 0;
        default_game_viewport(240, 160, 1920, 1080, &src, &dst, &bw, &bh);
        CHECK_EQ(bw, 240); CHECK_EQ(bh, 160);
        CHECK_EQ(src.w, 240); CHECK_EQ(src.h, 160);
        CHECK_EQ(dst.h, 1080); CHECK_EQ(dst.w, 1620);
        CHECK_EQ(dst.x, (1920 - 1620) / 2); CHECK_EQ(dst.y, 0);
    }
    {   // an integer-prescaled 284-wide target reports its unscaled base width.
        SDL_Rect src{}, dst{};
        int bw = 0, bh = 0;
        default_game_viewport(852, 480, 1000, 1000, &src, &dst, &bw, &bh);
        CHECK_EQ(bw, 284); CHECK_EQ(bh, 160);
        CHECK_EQ(src.w, 852); CHECK_EQ(src.h, 480);
        CHECK_EQ(dst.w, 1000); CHECK(dst.h < 1000);
    }

    // --- install + handlers through real SDL calls (dummy video driver) --------------
    GameHostConfig c;
    c.tag = "gbaimp-test";
    c.on_poll = &on_poll;
    c.wants_native_frame = &wants_native;
    c.on_native_frame = &on_native;
    c.compute_viewport = &viewport;
    c.after_game_image = &after_image;
    c.on_present_begin = &present_begin;
    c.render_present_overlay = &overlay;
    c.before_present = &before_present;

    const GameHostStats before = game_host_stats();
    install_game_host(c);
    install_game_host(c);   // idempotent: replaces the configuration, does not stack handlers

    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Window* win = SDL_CreateWindow("t", 0, 0, 640, 480, SDL_WINDOW_HIDDEN);
    CHECK(win != nullptr);
    SDL_Renderer* r = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    CHECK(r != nullptr);
    if (r) {
        SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 240, 160);
        CHECK(tex != nullptr);
        std::vector<uint8_t> pixels(240 * 160 * 3, 0x80);

        SDL_Event ev;
        SDL_PollEvent(&ev);
        CHECK_EQ(g_poll_hook, 1);

        CHECK_EQ(SDL_UpdateTexture(tex, nullptr, pixels.data(), 240 * 3), 0);
        CHECK_EQ(g_native_frames, 1);
        g_native_wanted = false;
        SDL_UpdateTexture(tex, nullptr, pixels.data(), 240 * 3);
        CHECK_EQ(g_native_frames, 1);   // gated by wants_native_frame

        SDL_RenderClear(r);
        CHECK_EQ(SDL_RenderCopy(r, tex, nullptr, nullptr), 0);
        CHECK_EQ(g_viewport_calls, 1);
        CHECK_EQ(g_after_image, 1);
        CHECK_EQ(g_last_dst.x, (640 - 640) / 2 + 4);   // 640x480 window, 3:2 -> 640x426, +4 from the hook

        // A texture that is not the game image is passed through untouched.
        SDL_Texture* other = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, 16, 16);
        if (other) {
            SDL_RenderCopy(r, other, nullptr, nullptr);
            CHECK_EQ(g_viewport_calls, 1);
            SDL_DestroyTexture(other);
        }

        SDL_RenderPresent(r);
        CHECK_EQ(g_present_begin, 1);
        CHECK_EQ(g_overlay, 1);
        CHECK_EQ(g_before_present, 1);
        CHECK_EQ(g_last_win_w, 640);
        CHECK_EQ(g_last_win_h, 480);

        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(r);
    }
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();

    const GameHostStats after = game_host_stats();
    CHECK(after.poll_event > before.poll_event);
    CHECK(after.update_texture >= before.update_texture + 2);
    CHECK(after.render_copy > before.render_copy);
    CHECK(after.render_present > before.render_present);

    return testh::finish("game_host");
}
