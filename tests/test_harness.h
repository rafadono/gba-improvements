// Minimal assert harness shared by the gbaimp unit tests (no external framework).
#pragma once

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace testh {

inline int& failures() { static int n = 0; return n; }
inline int& checks() { static int n = 0; return n; }

inline void report(bool ok, const char* expr, const char* file, int line) {
    ++checks();
    if (!ok) {
        ++failures();
        std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, expr);
    }
}

inline int finish(const char* suite) {
    std::printf("[%s] %d checks, %d failed\n", suite, checks(), failures());
    return failures() == 0 ? 0 : 1;
}

// Scratch directory, removed on destruction. enter() also chdir()s into it so
// modules that use relative default paths (keybinds.ini, saves/thumbnails, ...)
// never touch the source tree.
class TempDir {
public:
    explicit TempDir(const std::string& tag) {
        namespace fs = std::filesystem;
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("gbaimp_" + tag + "_" + std::to_string(stamp));
        fs::create_directories(path_);
        old_cwd_ = fs::current_path();
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::current_path(old_cwd_, ec);
        std::filesystem::remove_all(path_, ec);
    }
    void enter() { std::filesystem::current_path(path_); }
    std::string str() const { return path_.string(); }
    std::string file(const std::string& name) const { return (path_ / name).string(); }

private:
    std::filesystem::path path_;
    std::filesystem::path old_cwd_;
};

} // namespace testh

#define CHECK(expr) ::testh::report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::testh::report(((a) == (b)), #a " == " #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) ::testh::report((((a) - (b)) <= (tol) && ((b) - (a)) <= (tol)), #a " ~= " #b, __FILE__, __LINE__)
