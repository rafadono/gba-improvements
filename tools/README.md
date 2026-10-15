# Tools

Game-agnostic helper scripts for a GBA static-recompilation project. All scripts
are Python 3 (standard library only) and take paths and game-specific values on
the command line. Hex values accept `0x...`. Defaults resolve relative to
`--root` (default: current directory). Run `<script> --help` for details.

| Script | Purpose |
|---|---|
| `new_host/new_host.cmake` | Instantiate `templates/host/` (CMakeLists, build scripts, Dockerfile, CI) for a new game: `cmake -DOUT_DIR=.. -DGAME_EXE=.. -DSYMBOL_PREFIX=.. -DROM_FILE=.. -DRECOMP_CONFIG=.. -P new_host.cmake`; see `docs/NEW_HOST.md`. |
| `trace_control_flow.py` | Follow branches from the entry point and scan literal pools to discover reachable ARM/Thumb functions; writes a TSV. |
| `merge_coverage_toml.py` | Merge functions from symbol files, the recompiler cache and ROM prologue scanning into a recompiler TOML's `[[extra_func]]` list. |
| `audit_code_coverage.py` | Detect function prologues in the ROM, compare to known symbols and write a Markdown coverage report. |
| `gen_ram_dispatch.py` | Generate a native IWRAM/EWRAM dispatcher (`ram_overlay_dispatch.h/.cpp`) from cached recompiler `.c` output. |
| `gen_symbols.py` | Extract `task_*`/`mode_*` function symbols from the ROM's debug descriptor tables. |
| `merge_cache_misses.py` | Append the functions found in the recompiler miss cache (`recomp_cache/**/<addr>_<size>_<t\|a>.c`) to a recompiler TOML as `[[extra_func]]` entries. |
| `update_symbols.py` | Build a symbol TSV (`0xADDR<TAB>arm\|thumb<TAB>name`) from `--seed ADDR:MODE:NAME` symbols and the `thumb_func` lines of a `gen_symbols.py` output. |
| `import_decomp_symbols.py` | Wrapper that runs gbarecomp's `symbol_import/import_decomp_symbols.py` with the project layout (`--id`, `--syms`, `--sections`, `--map`, `--rom`). |
| `png_to_ppm.py` | Convert an 8-bit RGB/RGBA PNG (e.g. a capture frame) to a binary PPM (P6). |
| `gen_font_data.py` | Embed TrueType fonts as C++ byte arrays with indexed `data/size/count` accessors; namespace, header and function names are options. |
| `verify_rom_hash/` | Self-contained C++ tool (built by CMake with `GBAIMP_BUILD_TOOLS`, ON for a top-level build, OFF as a subproject): `verify_rom_hash <rom> <expected_sha1>`; exit 0 match, 1 mismatch, 2 usage/IO error. Its tests (`verify_rom_hash_sha1_vectors`, `verify_rom_hash_cli`) run under `ctest`. |

The `capture/` folder holds the generic parts of the headless capture harness (runners `run_capture.sh` / `run_capture.ps1`, `montage.py`, `stroke_runs.py`); see `docs/CAPTURE_HARNESS.md`.

Tests: `tests/test_gen_ram_dispatch.py` and `tests/test_tools.py` (ctest `gen_ram_dispatch_py`, `tools_py`; standard library `unittest`).

## Examples

```sh
python tools/trace_control_flow.py --rom roms/game.gba --code-limit 0x08140000 --entry-point 0x080000C0
python tools/merge_coverage_toml.py --toml config/game.toml --rom roms/game.gba --target 3650
python tools/audit_code_coverage.py --rom roms/game.gba --toml config/game.toml --game-name "My Game"
python tools/gen_ram_dispatch.py --symbol-prefix mygame --game-name "My Game"
python tools/gen_symbols.py roms/game.gba -o symbols/task_mode_symbols.txt
python tools/update_symbols.py --seed 0x080000C0:arm:crt0_entry
python tools/merge_cache_misses.py --toml config/game.toml
python tools/import_decomp_symbols.py --id ABCD --name "My Game" --syms symbols/game_syms.txt --map symbols/game.map --rom roms/game.gba
python tools/png_to_ppm.py capture/frame.png frame.ppm
python tools/gen_font_data.py --namespace mygame::fonts --include fonts.h Sans.sub.ttf Serif.sub.ttf > fonts_data.cpp
```

## Options

Shared: `--root` (all scripts except `gen_symbols.py`), `--rom` / `--rom-base` (default `0x08000000`; not in `gen_ram_dispatch.py`).

- `trace_control_flow.py`: `--rom` (required), `--code-limit` (`0x08140000`), `--entry-point` (`0x080000C0`), `--seeds`, `-o/--output`.
- `merge_coverage_toml.py`: `--toml` (`<root>/config/recomp.toml`), `--symbols-dir`, `--cache-dir`, `--rom`, `--prologue-scan-limit` (`0x140000`), `--target` (`3650`).
- `audit_code_coverage.py`: `--toml`, `--symbols-tsv`, `--report`, `--game-name` (`GBA game`), `--scan-limit` (`0x200000`).
- `gen_ram_dispatch.py`: `--migrate FILE...` (convert an older generated `ram_overlay_dispatch.cpp` in place to the `gbaimp/ram_dispatch_gbarecomp.h` form and exit), `--cache-dir`, `--cache-pattern`, `--out-dir`, `--symbol-prefix` (`recomp`; yields `<prefix>_ram_dispatch_hook` and `<prefix>_install_ram_dispatch`), `--game-name`, `--runtime-header`.
- `merge_cache_misses.py`: `--root`, `--toml` (the only `<root>/config/*.toml`), `--cache-dir` (`<root>/recomp_cache`), `--min-addr` (`0x08000000`; `0` accepts every address), `--dry-run`.
- `update_symbols.py`: `--root`, `--out` (`<root>/symbols/imported_symbols.tsv`), `--task-syms` (`<root>/symbols/task_mode_symbols.txt`, optional), `--seed ADDR:MODE:NAME` (repeatable). Task symbols win over a seed at the same address.
- `import_decomp_symbols.py`: `--id`, `--syms`, `--rom` (required), `--name`, `--sections`, `--map`, `--out` (`<root>/symbols`), `--importer` (`<root>/gbarecomp/tools/symbol_import/import_decomp_symbols.py`), `--root`; arguments after `--` are passed to the importer. Exit status is the importer's.
- `png_to_ppm.py`: `in.png out.ppm`.
- `gen_font_data.py`: font files in the order the host registers them; `--namespace` (`embedded_fonts`, `a::b` allowed), `--include`, `--data-fn` (`face_data`), `--size-fn` (`face_size`), `--count-fn` (omitted by default), `--banner`.
- `gen_symbols.py`: positional `rom`, `-o`, `--code-size` (`0x122000`), `--task-prefix` (`task_`), `--mode-prefix` (`mode_`).
