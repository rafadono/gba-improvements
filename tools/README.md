# Tools

Game-agnostic helper scripts for a GBA static-recompilation project. All scripts
are Python 3 (standard library only) and take paths and game-specific values on
the command line. Hex values accept `0x...`. Defaults resolve relative to
`--root` (default: current directory). Run `<script> --help` for details.

| Script | Purpose |
|---|---|
| `trace_control_flow.py` | Follow branches from the entry point and scan literal pools to discover reachable ARM/Thumb functions; writes a TSV. |
| `merge_coverage_toml.py` | Merge functions from symbol files, the recompiler cache and ROM prologue scanning into a recompiler TOML's `[[extra_func]]` list. |
| `audit_code_coverage.py` | Detect function prologues in the ROM, compare to known symbols and write a Markdown coverage report. |
| `gen_ram_dispatch.py` | Generate a native IWRAM/EWRAM dispatcher (`ram_overlay_dispatch.h/.cpp`) from cached recompiler `.c` output. |
| `gen_symbols.py` | Extract `task_*`/`mode_*` function symbols from the ROM's debug descriptor tables. |
| `verify_rom_hash/` | Self-contained C++ tool (built by CMake with `GBAIMP_BUILD_TOOLS`, ON for a top-level build, OFF as a subproject): `verify_rom_hash <rom> <expected_sha1>`; exit 0 match, 1 mismatch, 2 usage/IO error. Its tests (`verify_rom_hash_sha1_vectors`, `verify_rom_hash_cli`) run under `ctest`. |

The `capture/` folder holds the generic parts of the headless capture harness (runners `run_capture.sh` / `run_capture.ps1`, `montage.py`, `stroke_runs.py`); see `docs/CAPTURE_HARNESS.md`.

## Examples

```sh
python tools/trace_control_flow.py --rom roms/game.gba --code-limit 0x08140000 --entry-point 0x080000C0
python tools/merge_coverage_toml.py --toml config/game.toml --rom roms/game.gba --target 3650
python tools/audit_code_coverage.py --rom roms/game.gba --toml config/game.toml --game-name "My Game"
python tools/gen_ram_dispatch.py --symbol-prefix mygame --game-name "My Game"
python tools/gen_symbols.py roms/game.gba -o symbols/task_mode_symbols.txt
```

## Options

Shared: `--root` (all scripts except `gen_symbols.py`), `--rom` / `--rom-base` (default `0x08000000`; not in `gen_ram_dispatch.py`).

- `trace_control_flow.py`: `--rom` (required), `--code-limit` (`0x08140000`), `--entry-point` (`0x080000C0`), `--seeds`, `-o/--output`.
- `merge_coverage_toml.py`: `--toml` (`<root>/config/recomp.toml`), `--symbols-dir`, `--cache-dir`, `--rom`, `--prologue-scan-limit` (`0x140000`), `--target` (`3650`).
- `audit_code_coverage.py`: `--toml`, `--symbols-tsv`, `--report`, `--game-name` (`GBA game`), `--scan-limit` (`0x200000`).
- `gen_ram_dispatch.py`: `--cache-dir`, `--cache-pattern`, `--out-dir`, `--symbol-prefix` (`recomp`; yields `<prefix>_ram_dispatch_hook` and `<prefix>_install_ram_dispatch`), `--game-name`, `--runtime-header`.
- `gen_symbols.py`: positional `rom`, `-o`, `--code-size` (`0x122000`), `--task-prefix` (`task_`), `--mode-prefix` (`mode_`).
