#include "gbaimp/savestate_thumbnails.h"
#include <SDL.h>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace gbaimp {

SavestateThumbnailManager& SavestateThumbnailManager::instance() {
    static SavestateThumbnailManager s_instance;
    return s_instance;
}

SavestateThumbnailManager::SavestateThumbnailManager() {
    ensure_save_directory();
}

void SavestateThumbnailManager::ensure_save_directory() {
    std::error_code ec;
    std::filesystem::create_directories(base_dir_, ec);
}

void SavestateThumbnailManager::set_base_directory(const std::string& dir) {
    base_dir_ = dir.empty() ? std::string("saves/thumbnails") : dir;
    ensure_save_directory();
}

std::string SavestateThumbnailManager::get_thumbnail_path(int slot) const {
    std::string dir = base_dir_;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    return dir + "slot_" + std::to_string(slot) + ".bmp";
}

void SavestateThumbnailManager::capture_thumbnail(int slot, const uint32_t* rgba_pixels, int width, int height) {
    if (!rgba_pixels || width <= 0 || height <= 0) return;

    ensure_save_directory();
    std::string path = get_thumbnail_path(slot);

    // Create SDL Surface from RGBA pixels
    Uint32 rmask = 0x000000FF;
    Uint32 gmask = 0x0000FF00;
    Uint32 bmask = 0x00FF0000;
    Uint32 amask = 0xFF000000;

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        const_cast<void*>(reinterpret_cast<const void*>(rgba_pixels)),
        width, height, 32, width * 4,
        rmask, gmask, bmask, amask
    );

    if (surface) {
        SDL_SaveBMP(surface, path.c_str());
        SDL_FreeSurface(surface);
    }
}

SavestateMetadata SavestateThumbnailManager::get_slot_metadata(int slot) {
    SavestateMetadata meta;
    meta.slot = slot;
    meta.image_path = get_thumbnail_path(slot);

    std::error_code ec;
    if (std::filesystem::exists(meta.image_path, ec)) {
        meta.exists = true;
        auto ftime = std::filesystem::last_write_time(meta.image_path, ec);
        if (!ec) {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            std::time_t ctime = std::chrono::system_clock::to_time_t(sctp);
            std::tm tm_buf;
#if defined(_WIN32)
            localtime_s(&tm_buf, &ctime);
#else
            localtime_r(&ctime, &tm_buf);
#endif
            std::ostringstream ss;
            ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
            meta.timestamp = ss.str();
        }
    } else {
        meta.exists = false;
        meta.timestamp = "Empty Slot";
    }

    return meta;
}

} // namespace gbaimp
