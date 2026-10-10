#include "test_harness.h"
#include "gbaimp/savestate_thumbnails.h"
#include <filesystem>
#include <vector>

using namespace gbaimp;
namespace fs = std::filesystem;

int main() {
    testh::TempDir tmp("thumbs");
    tmp.enter();   // default "saves/thumbnails" must land here, not in the source tree

    auto& mgr = SavestateThumbnailManager::instance();
    CHECK(mgr.base_directory() == "saves/thumbnails");

    // Custom base directory is created and used for paths.
    const std::string dir = tmp.file("custom/thumbs");
    mgr.set_base_directory(dir);
    CHECK(mgr.base_directory() == dir);
    CHECK(fs::is_directory(dir));
    {
        std::string p = mgr.get_thumbnail_path(3);
        CHECK(p == dir + "/slot_3.bmp");
    }

    // Trailing separator is not doubled.
    mgr.set_base_directory(dir + "/");
    CHECK(mgr.get_thumbnail_path(1) == dir + "/slot_1.bmp");
    mgr.set_base_directory(dir);

    // Empty slot metadata.
    {
        SavestateMetadata m = mgr.get_slot_metadata(3);
        CHECK_EQ(m.slot, 3);
        CHECK(!m.exists);
        CHECK(m.timestamp == "Empty Slot");
        CHECK(m.image_path == dir + "/slot_3.bmp");
    }

    // Invalid captures are ignored.
    mgr.capture_thumbnail(3, nullptr, 4, 4);
    uint32_t px[16] = {};
    mgr.capture_thumbnail(3, px, 0, 4);
    mgr.capture_thumbnail(3, px, 4, -1);
    CHECK(!fs::exists(dir + "/slot_3.bmp"));

    // Valid capture writes a BMP and flips the slot to existing.
    for (auto& p : px) p = 0xFF3366CCu;
    mgr.capture_thumbnail(3, px, 4, 4);
    CHECK(fs::exists(dir + "/slot_3.bmp"));
    CHECK(fs::file_size(dir + "/slot_3.bmp") > 54);   // BMP header + pixels
    {
        SavestateMetadata m = mgr.get_slot_metadata(3);
        CHECK(m.exists);
        CHECK(!m.timestamp.empty());
        CHECK(m.timestamp != "Empty Slot");
        CHECK_EQ(m.timestamp.size(), size_t(19));      // "YYYY-MM-DD HH:MM:SS"
    }
    CHECK(!mgr.get_slot_metadata(4).exists);           // other slots unaffected

    // Empty string resets to the default directory.
    mgr.set_base_directory("");
    CHECK(mgr.base_directory() == "saves/thumbnails");
    CHECK(fs::is_directory(tmp.file("saves/thumbnails")));

    return testh::finish("savestate_thumbnails");
}
