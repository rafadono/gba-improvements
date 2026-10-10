#pragma once

#include <cstdint>
#include <string>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Rect;

namespace gbaimp {

enum class FramerateMode : int {
    Fps60 = 0,
    Fps120 = 1,
    Fps144 = 2,
    DisplayNative = 3,
    Uncapped = 4
};

enum class MotionSmoothingMode : int {
    Off = 0,
    SmoothBlend = 1,
    MotionAdaptive = 2
};

struct FrameInterpolatorSettings {
    FramerateMode mode = FramerateMode::Fps60;
    MotionSmoothingMode smoothing = MotionSmoothingMode::SmoothBlend;
};

class FrameInterpolator {
public:
    static FrameInterpolator& instance();

    const FrameInterpolatorSettings& settings() const { return settings_; }
    FrameInterpolatorSettings& settings() { return settings_; }

    void set_mode(FramerateMode mode);
    void set_smoothing(MotionSmoothingMode smoothing);

    double target_fps() const;
    double target_frametime_ms() const;
    std::string detected_display_info(SDL_Renderer* renderer) const;

    void on_present(SDL_Renderer* renderer, const SDL_Rect* viewport);

private:
    FrameInterpolator();
    ~FrameInterpolator();

    void update_display_mode(SDL_Renderer* renderer);
    void sleep_until_target(int64_t target_qpc);

    FrameInterpolatorSettings settings_;
    int detected_hz_ = 60;
    int display_w_ = 1920;
    int display_h_ = 1080;
    bool display_queried_ = false;

    SDL_Texture* prev_frame_tex_ = nullptr;
    int tex_w_ = 0;
    int tex_h_ = 0;
    uint64_t last_present_qpc_ = 0;
};

} // namespace gbaimp
