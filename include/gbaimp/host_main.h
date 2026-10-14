#pragma once
// Common start-up sequence of a GBA static-recompilation host executable.
//
// Every host's main() did the same thing:
//   1. register the SDL interposers (must precede the runtime's first SDL call);
//   2. default GBARECOMP_RAM_OVERLAY_HEAL=1 unless the user already set it;
//   3. install the RAM-overlay dispatcher (before run_game: run_game clears the hook on return);
//   4. print "<title> Static Recompilation (<banner_name>)";
//   5. ask the game for extra command-line arguments (optional, e.g. a headless capture harness);
//   6. build the run options;
//   7. either hand over to the launcher UI (preboot returns true = user quit => exit 0) and run the
//      game with the possibly edited argument list, or run directly (original argc/argv when there
//      are no extra arguments).
// run_host_main() performs it. Everything game-specific is a callback. The function is a
// template over the options type, so this header does not include any gbarecomp header and the
// sequence is unit-tested without the runtime.
//
// Typical host (gbarecomp present, RECOMP_LAUNCHER optional):
//
//   #include "runtime.h"
//   #include "launcher_seam.h"          // only when RECOMP_LAUNCHER is defined
//   #define GBAIMP_HOST_MAIN_GBARECOMP  // opt in to the adapter at the end of host_main.h
//   #include "gbaimp/host_main.h"
//   extern "C" bool mygame_install_sdl_interposers();
//   void mygame_install_ram_dispatch();
//   int main(int argc, char** argv) {
//       gbaimp::HostMainConfig<gbarecomp::RunOptions> c;
//       c.title = mygame::GAME_TITLE.data();
//       c.banner_name = "MyGameRecomp";
//       c.install_sdl_interposers = &mygame_install_sdl_interposers;
//       c.install_ram_dispatch = &mygame_install_ram_dispatch;
//       c.extra_args = [] { return mygame::capture::extra_runtime_args(); };   // optional
//       c.create_options = [] { return mygame::create_run_options(); };
//       return gbaimp::run_gbarecomp_host(c, argc, argv);
//   }
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace gbaimp {

// Sets environment variable `name` to `value` unless it already exists (an empty value counts as
// set). Returns true when it was set by this call.
inline bool set_env_default(const char* name, const char* value) {
    if (std::getenv(name)) return false;
#if defined(_WIN32)
    return _putenv((std::string(name) + "=" + value).c_str()) == 0;
#else
    return setenv(name, value, 1) == 0;
#endif
}

// argv as a vector, followed by `extra`.
inline std::vector<std::string> build_run_args(int argc, char** argv, const std::vector<std::string>& extra) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i] ? argv[i] : "");
    args.insert(args.end(), extra.begin(), extra.end());
    return args;
}

template <class Opts>
struct HostMainConfig {
    const char* title = "";                 // printed first (e.g. the game's display title)
    const char* banner_name = "Recomp";     // text in parentheses of the banner line
    // Step 1. Return value ignored (as in the hosts). Optional.
    std::function<bool()> install_sdl_interposers;
    // Step 2: when true, GBARECOMP_RAM_OVERLAY_HEAL defaults to "1".
    bool default_ram_overlay_heal = true;
    // Step 3. Optional.
    std::function<void()> install_ram_dispatch;
    // Step 5. Optional; empty result = no extra arguments.
    std::function<std::vector<std::string>()> extra_args;
    // Step 6. Required.
    std::function<Opts()> create_options;
    // Step 7. Optional: launcher pre-boot. Returns true when the user quit instead of playing.
    // May edit the argument list and the options. Empty = direct run.
    std::function<bool(std::vector<std::string>&, Opts&)> launcher_preboot;
    // Step 7. Required: gbarecomp::run_game or an equivalent.
    std::function<int(int, char**, const Opts&)> run_game;
};

// Returns the process exit code. Exits 0 without running the game when launcher_preboot asks to.
template <class Opts>
inline int run_host_main(const HostMainConfig<Opts>& cfg, int argc, char** argv) {
    if (cfg.install_sdl_interposers) cfg.install_sdl_interposers();
    if (cfg.default_ram_overlay_heal) set_env_default("GBARECOMP_RAM_OVERLAY_HEAL", "1");
    if (cfg.install_ram_dispatch) cfg.install_ram_dispatch();
    std::printf("%s Static Recompilation (%s)\n", cfg.title ? cfg.title : "", cfg.banner_name ? cfg.banner_name : "");

    std::vector<std::string> extra;
    if (cfg.extra_args) extra = cfg.extra_args();
    Opts opts = cfg.create_options();

    if (!cfg.launcher_preboot && extra.empty()) {
        return cfg.run_game(argc, argv, opts);   // unchanged path: the process' own argc/argv
    }
    std::vector<std::string> args = build_run_args(argc, argv, extra);
    if (cfg.launcher_preboot && cfg.launcher_preboot(args, opts)) return 0;
    std::vector<char*> av;
    for (auto& s : args) av.push_back(s.data());
    av.push_back(nullptr);   // argv[argc] == nullptr; argc below excludes it
    return cfg.run_game(static_cast<int>(av.size()) - 1, av.data(), opts);
}

} // namespace gbaimp

// Adapter for hosts that build against gbarecomp. Define GBAIMP_HOST_MAIN_GBARECOMP and include
// "runtime.h" (gbarecomp::RunOptions, gbarecomp::run_game) first; with RECOMP_LAUNCHER defined also
// "launcher_seam.h" (gbarecomp_launcher_preboot).
#if defined(GBAIMP_HOST_MAIN_GBARECOMP)
namespace gbaimp {

// Fills run_game (and, under RECOMP_LAUNCHER, launcher_preboot) with gbarecomp's and runs.
inline int run_gbarecomp_host(HostMainConfig<gbarecomp::RunOptions> cfg, int argc, char** argv) {
    cfg.run_game = [](int ac, char** av, const gbarecomp::RunOptions& o) { return gbarecomp::run_game(ac, av, o); };
#if defined(RECOMP_LAUNCHER)
    cfg.launcher_preboot = [](std::vector<std::string>& a, gbarecomp::RunOptions& o) {
        return gbarecomp_launcher_preboot(a, o) != 0;
    };
#endif
    return run_host_main(cfg, argc, argv);
}

} // namespace gbaimp
#endif
