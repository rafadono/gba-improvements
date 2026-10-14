#include "test_harness.h"
#include "gbaimp/host_main.h"
#include "gbaimp/msvc_compat.h"
#include "gbaimp/scaffold_stub.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

GBAIMP_SCAFFOLD_EMPTY_DISPATCH_TABLE()
namespace gamex { GBAIMP_WEAK void save_gamex_config() {} }

namespace {
struct Opts { int value = 0; bool edited = false; };

struct Trace {
    std::vector<std::string> events;
    std::vector<std::string> run_args;
    int run_argc = -1;
    char** run_argv = nullptr;
    int run_value = -1;
    bool run_edited = false;
    bool run_argv_terminated = false;
};

using Events = std::vector<std::string>;

gbaimp::HostMainConfig<Opts> make(Trace& t, char** orig_argv) {
    gbaimp::HostMainConfig<Opts> c;
    c.title = "Title";
    c.banner_name = "TitleRecomp";
    c.install_sdl_interposers = [&t] { t.events.push_back("sdl"); return true; };
    c.install_ram_dispatch = [&t] {
        t.events.push_back(std::getenv("GBARECOMP_RAM_OVERLAY_HEAL") ? "ram+heal" : "ram-noheal");
    };
    c.create_options = [&t] { t.events.push_back("opts"); Opts o; o.value = 7; return o; };
    c.run_game = [&t, orig_argv](int argc, char** argv, const Opts& o) {
        t.events.push_back("run");
        t.run_argc = argc;
        t.run_argv = argv;
        t.run_value = o.value;
        t.run_edited = o.edited;
        t.run_args.clear();
        for (int i = 0; i < argc; ++i) t.run_args.push_back(argv[i]);
        t.run_argv_terminated = argv == orig_argv || argv[argc] == nullptr;
        return 42;
    };
    return c;
}

void clear_heal() {
#if defined(_WIN32)
    _putenv("GBARECOMP_RAM_OVERLAY_HEAL=");   // "NAME=" removes the variable on Windows
#else
    unsetenv("GBARECOMP_RAM_OVERLAY_HEAL");
#endif
}
} // namespace

int main() {
    // --- set_env_default ------------------------------------------------------------
    clear_heal();
    CHECK(!std::getenv("GBARECOMP_RAM_OVERLAY_HEAL"));
    CHECK(gbaimp::set_env_default("GBARECOMP_RAM_OVERLAY_HEAL", "1"));
    CHECK(std::strcmp(std::getenv("GBARECOMP_RAM_OVERLAY_HEAL"), "1") == 0);
    CHECK(!gbaimp::set_env_default("GBARECOMP_RAM_OVERLAY_HEAL", "9"));   // never overrides
    CHECK(std::strcmp(std::getenv("GBARECOMP_RAM_OVERLAY_HEAL"), "1") == 0);

    // --- build_run_args --------------------------------------------------------------
    char a0[] = "game", a1[] = "rom.gba";
    char* argv[] = {a0, a1, nullptr};
    auto args = gbaimp::build_run_args(2, argv, {"--x", "--y"});
    CHECK_EQ(args.size(), 4u);
    CHECK(args[0] == "game" && args[3] == "--y");
    CHECK_EQ(gbaimp::build_run_args(0, argv, {}).size(), 0u);

    // --- direct run: original argc/argv, fixed order, heal defaulted -------------------
    {
        clear_heal();
        Trace t;
        auto c = make(t, argv);
        CHECK_EQ(gbaimp::run_host_main(c, 2, argv), 42);
        CHECK(t.events == (Events{"sdl", "ram+heal", "opts", "run"}));
        CHECK(t.run_argv == argv);
        CHECK_EQ(t.run_argc, 2);
        CHECK_EQ(t.run_value, 7);
    }
    // --- the heal default can be disabled ---------------------------------------------
    {
        clear_heal();
        Trace t;
        auto c = make(t, argv);
        c.default_ram_overlay_heal = false;
        gbaimp::run_host_main(c, 2, argv);
        CHECK(t.events[1] == "ram-noheal");
    }
    // --- harness args appended, argv rebuilt -------------------------------------------
    {
        Trace t;
        auto c = make(t, argv);
        c.extra_args = [&t] { t.events.push_back("extra"); return std::vector<std::string>{"--load", "s1"}; };
        CHECK_EQ(gbaimp::run_host_main(c, 2, argv), 42);
        CHECK(t.events == (Events{"sdl", "ram+heal", "extra", "opts", "run"}));   // extra args before the options
        CHECK(t.run_args == (Events{"game", "rom.gba", "--load", "s1"}));
        CHECK(t.run_argv != argv);
        CHECK(t.run_argv_terminated);
    }
    // --- empty harness result keeps the original argv -----------------------------------
    {
        Trace t;
        auto c = make(t, argv);
        c.extra_args = [] { return std::vector<std::string>{}; };
        gbaimp::run_host_main(c, 2, argv);
        CHECK(t.run_argv == argv);
    }
    // --- launcher: preboot may edit args and options -----------------------------------
    {
        Trace t;
        auto c = make(t, argv);
        c.extra_args = [] { return std::vector<std::string>{"--h"}; };
        c.launcher_preboot = [&t](std::vector<std::string>& a, Opts& o) {
            t.events.push_back("preboot");
            a.push_back("--rom=chosen");
            o.edited = true;
            return false;
        };
        CHECK_EQ(gbaimp::run_host_main(c, 2, argv), 42);
        CHECK(t.events == (Events{"sdl", "ram+heal", "opts", "preboot", "run"}));
        CHECK(t.run_args == (Events{"game", "rom.gba", "--h", "--rom=chosen"}));
        CHECK(t.run_edited);
    }
    // --- launcher without harness args still copies argv (the original launcher flow) ----
    {
        Trace t;
        auto c = make(t, argv);
        c.launcher_preboot = [](std::vector<std::string>&, Opts&) { return false; };
        gbaimp::run_host_main(c, 2, argv);
        CHECK(t.run_argv != argv);
        CHECK(t.run_args == (Events{"game", "rom.gba"}));
        CHECK(t.run_argv_terminated);
    }
    // --- user quits in the launcher: exit 0, game never runs -----------------------------
    {
        Trace t;
        auto c = make(t, argv);
        c.launcher_preboot = [](std::vector<std::string>&, Opts&) { return true; };
        CHECK_EQ(gbaimp::run_host_main(c, 2, argv), 0);
        CHECK(std::find(t.events.begin(), t.events.end(), "run") == t.events.end());
    }
    // --- optional steps may be empty ---------------------------------------------------------
    {
        gbaimp::HostMainConfig<Opts> c;
        c.create_options = [] { return Opts{}; };
        c.run_game = [](int, char**, const Opts&) { return 3; };
        CHECK_EQ(gbaimp::run_host_main(c, 2, argv), 3);
    }

    // --- msvc_compat: __builtin_clz exists and agrees with the definition ----------------------
    CHECK_EQ(__builtin_clz(1u), 31);
    CHECK_EQ(__builtin_clz(0x80000000u), 0);
    CHECK_EQ(__builtin_clz(0x00010000u), 15);
    CHECK_EQ(__builtin_clz(0xFFFFFFFFu), 0);
    CHECK_EQ(__builtin_clz(static_cast<uint8_t>(0x10)), 27);   // narrower arguments promote

    // --- scaffold -----------------------------------------------------------------------
    CHECK_EQ(kDispatchTableLen, 0u);
    CHECK(kDispatchTable[0].fn == nullptr);
    gamex::save_gamex_config();

    return testh::finish("host_boot");
}
