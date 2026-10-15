# Starting a new host project with the build kit

A host is a repository that combines a statically recompiled GBA game (built with gbarecomp) with this library. Every host used to carry
about 300 lines of identical build plumbing: library linking, the `-Wl,--wrap=SDL_*` link options on Linux, the MSVC rename of the
recomp-ui symbol, the post-build copy of the executable and `SDL2.dll` to the repository root, a second executable for native unit tests with
the same flags, plus `build.ps1`, `build.sh`, a Dockerfile and a CI workflow. The build kit moves all of that into the library:

| Piece | Location |
|---|---|
| CMake function `gbaimp_add_host()` | `cmake/GbaimpHost.cmake` |
| Templates (`CMakeLists.txt`, `build.ps1`, `build.sh`, `docker/Dockerfile`, `.github/workflows/ci.yml`) | `templates/host/*.in` |
| Instantiation script | `tools/new_host/new_host.cmake` |

## Step by step

1. **Create the repository** and add the three dependencies as submodules (paths are what the templates expect):
   `gbarecomp/`, `external/gba-improvements/` (this library) and, optionally, `external/recomp-ui/`.
2. **Generate the build files** from the library checkout:
   ```
   cmake -DOUT_DIR=<host repo> -DGAME_EXE=MyGameRecomp -DSYMBOL_PREFIX=mygame \
         -DROM_FILE="My Game (USA).gba" -DRECOMP_CONFIG=mygame.toml \
         -P external/gba-improvements/tools/new_host/new_host.cmake
   ```
   This writes `CMakeLists.txt`, `build.ps1`, `build.sh`, `docker/Dockerfile` and `.github/workflows/ci.yml`. It refuses to overwrite existing
   files unless you pass `-DFORCE=ON`, and fails (writing nothing) if a template still contains an unresolved `@PLACEHOLDER@`.
3. **Write the game-specific sources** the generated `CMakeLists.txt` lists: `src/main.cpp`, `src/game_config.cpp`,
   `src/gbaimp_integration.cpp`, `src/sdl_intercepts.cpp` (the `GBAIMP_DEFINE_LD_WRAPS()` expansion and the Windows install call),
   `src/runtime_ui_standard.cpp`, `src/widescreen_adapter.cpp`, `src/ram_overlay_dispatch.cpp`, `src/scaffold_stub.cpp` (an empty stand-in
   used until `generated/` exists) and `tests/test_all_modules.cpp`. Edit the `SOURCES` / `TEST_SOURCES` lists to match; drop the ones you do not
   need. On MSVC the template also expects `src/msvc_compat.h` (force-included); remove those lines if you do not have one.
4. **Add the recompiler inputs**: `config/mygame.toml`, `symbols/imported_symbols.tsv`, the ROM in `roms/` (never commit it) and, optionally,
   a BIOS image in `bios/`.
5. **Recompile and build**: `./build.sh --recompile` (or `.\build.ps1 -Recompile`) generates `generated/`, then `./build.sh` / `.\build.ps1`
   builds. The executable and `SDL2.dll` are copied to the repository root.
6. **Commit** the generated build files; later changes to the shared plumbing arrive by updating the submodule (the CMake function), and by
   regenerating the scripts with `-DFORCE=ON` and reviewing the diff.

## Placeholders

| Placeholder | Meaning | Example |
|---|---|---|
| `@GAME_EXE@` | Executable target, output file name and log tag in the scripts | `MyGameRecomp` |
| `@PROJECT_NAME@` | CMake `project()` name (default: `GAME_EXE`) | `MyGameRecomp` |
| `@SYMBOL_PREFIX@` | Prefix for the MSVC symbol rename (lowercase form is used for the function, uppercase for the macro) | `mygame` |
| `@SYMBOL_PREFIX_UPPER@` | Uppercase prefix, derived (used for CMake variable names) | `MYGAME` |
| `@TEST_TARGET@` | Native unit-test executable (default: `<prefix lowercase>_unit_tests`) | `mygame_unit_tests` |
| `@ROM_FILE@` | Default ROM file name inside `roms/` (`build.ps1 -RomPath`); `build.sh` takes the first `roms/*.gba` | `My Game (USA).gba` |
| `@RECOMP_CONFIG@` | gbarecomp config file name inside `config/` | `mygame.toml` |

`GAME_EXE`, `SYMBOL_PREFIX` and `PROJECT_NAME` must match `[A-Za-z][A-Za-z0-9_]*`.

## `gbaimp_add_host()`

```cmake
include("${GBAIMP_ROOT}/cmake/GbaimpHost.cmake")   # after add_subdirectory() of gbarecomp and the library

gbaimp_add_host(MyGameRecomp
    SYMBOL_PREFIX mygame
    SOURCES src/main.cpp ...                  # game sources of the executable
    TEST_TARGET mygame_unit_tests             # optional
    TEST_SOURCES tests/test_all_modules.cpp ... src/scaffold_stub.cpp
    # optional: GENERATED_DIR, SCAFFOLD_STUB, INCLUDE_DIRS, SDL2_DLL
)
```

It reads `GBARECOMP_ROOT` (required), `SDL2_INCLUDE_DIR`, `RECOMP_UI_DIR` and `GBARECOMP_GENERATED_BIOS_DIR` from the calling scope, and expects the target
`gbaimp::gba_improvements` to exist. What it does for the executable and, where noted, the test target:

- Appends `generated/*.c|cpp` when `generated/dispatch_table.cpp` exists, otherwise the scaffold stub (executable only); compiles generated and BIOS
  sources with warnings off (`/w` or `-w`).
- Include directories (`src`, `generated`, gbarecomp `src`, `src/runtime`, `src/gba`, plus `src/armv4t` for the executable only, SDL2) and a link to
  `gbaimp::gba_improvements`.
- With recomp-ui: `recomp_target_launcher_ui()` and replacement of the stock `runtime_ui_imgui.cpp` by the library's `optional/recomp_ui/runtime_ui_imgui_enhanced.cpp` (override with `ENHANCED_IMGUI_SOURCE <file>`; a host no longer carries its own copy). That file needs `recomp_runtime_ui_internal.h` and `launcher_theme.h` from recomp-ui plus `imgui.h` (Dear ImGui 1.89.7 or newer), which `recomp_target_launcher_ui()` puts on the include path; it is never compiled without recomp-ui.
- Links the gbarecomp runtime stack with `target_link_gbarecomp_runtime_stack()` when available, otherwise the four gbarecomp static libraries
  (inside `--start-group` for the executable on GNU ld); `pthread dl m` on Linux.
- GNU ld: `-Wl,--wrap=` for `SDL_PollEvent`, `SDL_RenderPresent`, `SDL_UpdateTexture`, `SDL_RenderCopy`, `SDL_QueueAudio`, `SDL_OpenAudioDevice`
  (and `recomp_runtime_ui_create_standard`, on both targets because the host's `runtime_ui_standard.cpp` is compiled into the tests too). MSVC: no link options; with recomp-ui, `recomp_runtime_ui_create_standard` is renamed
  to `<prefix>_real_recomp_runtime_ui_create_standard` while compiling `recomp_runtime_settings.c` and `<PREFIX>_RENAME_RUNTIME_UI_CREATE_STANDARD=1` is defined
  on both targets.
- `CXX_SCAN_FOR_MODULES OFF`; post-build copy of the executable to the source root and of `third_party/sdl2/lib/x64/SDL2.dll` (when present) next
  to it and to the source root.

## What the generated scripts do

- `build.ps1` sets up the MSVC environment, finds CMake and Ninja, downloads the SDL2 development package if missing, configures with Ninja Multi-Config
  and builds `-Target` (default: the game). **Every native command is checked: a failing configure, build or recompilation stops the script with that
  command's exit code** (the hand-written copies printed the error and then exited with success).
- `build.sh` does the same on Linux with Ninja or Makefiles, and uses `set -euo pipefail`.
- The scripts run the recompiler on the BIOS only when `bios/*.gba` exists, so the same script works for games without a BIOS image.
- `docker/Dockerfile` is the Ubuntu toolchain image; `ci.yml` builds and tests on Windows and Linux. The optional `tests/test_runtime_functions.py` step runs only
  if the file exists.

Things the hosts did that are not part of the kit: copying game-specific directories (for example a text-overlay toolchain) to the repository root after a build,
and the extra comments in the SDL-interception section of `CMakeLists.txt`. Keep those in the host, after the `gbaimp_add_host()` call.

## Tests of the kit

`ctest` runs `buildkit_host_cmake` (a fixture host with stub gbarecomp / recomp-ui configured for GNU and MSVC branches, with and without recomp-ui, with and without generated
sources; the native variant is also built to check the post-build copies) and `buildkit_new_host` (the instantiation: no unresolved placeholders, no overwrite without
`FORCE`, argument validation).
