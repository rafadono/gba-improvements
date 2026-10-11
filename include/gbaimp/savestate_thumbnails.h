#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gbaimp {

struct SavestateMetadata {
    int slot = 0;
    bool exists = false;
    std::string timestamp;
    std::string image_path;
    int width = 0;
    int height = 0;
};

class SavestateThumbnailManager {
public:
    // Independently constructible (tests, multi-instance hosts); instance() stays the shared default.
    SavestateThumbnailManager();

    static SavestateThumbnailManager& instance();

    void capture_thumbnail(int slot, const uint32_t* rgba_pixels, int width, int height);
    SavestateMetadata get_slot_metadata(int slot);
    std::string get_thumbnail_path(int slot) const;

    // Directory where slot thumbnails are stored (default "saves/thumbnails")
    void set_base_directory(const std::string& dir);
    const std::string& base_directory() const { return base_dir_; }

private:
    void ensure_save_directory();

    std::string base_dir_ = "saves/thumbnails";
};

} // namespace gbaimp
