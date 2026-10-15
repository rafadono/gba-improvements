#!/usr/bin/env bash
# Generic headless capture run (Linux / macOS / Git Bash) -- see docs/CAPTURE_HARNESS.md.
# The host executable reads variables named <PREFIX>_CAPTURE_DIR, <PREFIX>_CAPTURE_FRAMES, ...
# where PREFIX is whatever the host passed as CaptureConfig::env_prefix.
#
# Usage: run_capture.sh -p PREFIX -e EXE -r ROM [options]
#   -p PREFIX   environment prefix of the host (required, or set CAPTURE_PREFIX)
#   -e EXE      host executable (required)
#   -r ROM      ROM path passed as --rom      -b BIOS  BIOS path passed as --bios (optional)
#   -d DIR      output dir (default captures/run)     -f FRAMES  e.g. "300,600-610,1200"
#   -s SCRIPT   input script file or inline text      -S SET     "key=v;key=v" (host settings)
#   -w WxH      output size (e.g. 852x480)            -m MODE    output|native|both
#   -V VARIANTS "852x480 key=0|960x640" (one output image per entry at each captured frame)
#   -l STATE    load savestate file                   -v PUMP:SLOT  save state (pump count since process start)
#   -P LIST     save states by PPU frame "300:1,900:2" (<PREFIX>_SAVESTATE_SAVE_PPU)
#   -L FRAME    PPU frame of the file given with -l (default: read from its .meta sidecar, 0 = boot)
#   -F          fast-forward                          -x FRAME   quit at emulated frame
#   -t SEC      timeout (default 240)                 -a ARGS    extra arguments for the host
set -euo pipefail
PREFIX=${CAPTURE_PREFIX:-} EXE= ROM= BIOS= EXTRA=
DIR=captures/run VARIANTS= FRAMES= SCRIPT= SETS= WIN= MODE=output LOAD= SAVE= SAVEPPU= LOADPPU= FAST= EXITF= TMO=240
while getopts "p:e:r:b:d:f:s:S:w:V:m:l:v:P:L:Fx:t:a:" o; do
  case $o in
    p) PREFIX=$OPTARG;; e) EXE=$OPTARG;; r) ROM=$OPTARG;; b) BIOS=$OPTARG;; d) DIR=$OPTARG;;
    f) FRAMES=$OPTARG;; s) SCRIPT=$OPTARG;; S) SETS=$OPTARG;; w) WIN=$OPTARG;; V) VARIANTS=$OPTARG;;
    m) MODE=$OPTARG;; l) LOAD=$OPTARG;; v) SAVE=$OPTARG;; P) SAVEPPU=$OPTARG;; L) LOADPPU=$OPTARG;; F) FAST=1;; x) EXITF=$OPTARG;;
    t) TMO=$OPTARG;; a) EXTRA=$OPTARG;;
    *) exit 2;;
  esac
done
[ -n "$PREFIX" ] || { echo "missing -p PREFIX (the host's env prefix)" >&2; exit 2; }
[ -n "$EXE" ] || { echo "missing -e EXE" >&2; exit 2; }
PREFIX=${PREFIX%_}
mkdir -p "$DIR"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
setv() { export "${PREFIX}_$1=$2"; }
setv CAPTURE_DIR "$PWD/$DIR"
setv CAPTURE_MODE "$MODE"
[ -n "$FRAMES" ] && { setv CAPTURE_FRAMES "$FRAMES"; setv EXIT_AFTER_CAPTURE 1; }
[ -n "$SCRIPT" ] && { if [ -f "$SCRIPT" ]; then setv INPUT_SCRIPT "$PWD/$SCRIPT"; else setv INPUT_SCRIPT "$SCRIPT"; fi; }
[ -n "$SETS" ] && setv SET "$SETS"
[ -n "$WIN" ] && setv WINDOW_SIZE "$WIN"
[ -n "$VARIANTS" ] && setv CAPTURE_VARIANTS "$VARIANTS"
[ -n "$LOAD" ] && setv SAVESTATE_LOAD "$PWD/$LOAD"
[ -n "$SAVE" ] && setv SAVESTATE_SAVE "$SAVE"
[ -n "$SAVEPPU" ] && setv SAVESTATE_SAVE_PPU "$SAVEPPU"
[ -n "$LOADPPU" ] && setv SAVESTATE_LOAD_PPU_FRAME "$LOADPPU"
# Where the runtime writes the slots (<rom>.state<N>): lets the host write the .meta sidecars.
[ -n "$ROM" ] && [ -n "$SAVEPPU" ] && setv SAVESTATE_ROM "$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")"
[ -n "$FAST" ] && setv FAST 1
[ -n "$EXITF" ] && setv EXIT_FRAME "$EXITF"
ARGS=()
[ -n "$ROM" ] && ARGS+=(--rom "$ROM")
[ -n "$BIOS" ] && ARGS+=(--bios "$BIOS")
set +e
# shellcheck disable=SC2086
timeout "$TMO" "$EXE" "${ARGS[@]}" $EXTRA >"$DIR/run.log" 2>"$DIR/run.log.err"
echo "exit code: $?  (logs: $DIR/run.log, $DIR/run.log.err)"
ls -1 "$DIR"/*.png 2>/dev/null || true
