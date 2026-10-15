#!/usr/bin/env python3
"""Build a symbol TSV (`0xADDR<TAB>arm|thumb<TAB>name`) from seed symbols and a task/mode symbol file.

Usage:
  update_symbols.py [--root DIR] [--out FILE] [--task-syms FILE] [--seed ADDR:MODE:NAME ...]

  --seed ADDR:MODE:NAME   one symbol, e.g. 0x080000C0:arm:crt0_entry (repeatable; MODE is arm or thumb)
  --task-syms FILE        `thumb_func <addr> <name>` lines as written by gen_symbols.py (default:
                          <root>/symbols/task_mode_symbols.txt; skipped when the file does not exist)
  --out FILE              default: <root>/symbols/imported_symbols.tsv

Symbols from --task-syms replace a seed at the same address. Output is sorted by address.
"""

import argparse
import os
import sys


def parse_seed(text):
    """`ADDR:MODE:NAME` -> (addr, mode, name)."""
    parts = text.split(":", 2)
    if len(parts) != 3 or not parts[2]:
        raise argparse.ArgumentTypeError("expected ADDR:MODE:NAME, got %r" % text)
    try:
        addr = int(parts[0], 0)
    except ValueError:
        raise argparse.ArgumentTypeError("bad address in %r" % text)
    mode = parts[1].lower()
    if mode not in ("arm", "thumb"):
        raise argparse.ArgumentTypeError("mode must be arm or thumb, got %r" % parts[1])
    return addr, mode, parts[2]


def read_task_symbols(path):
    """Yield (addr, 'thumb', name) for each `thumb_func <addr> <name>` line."""
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) >= 3 and parts[0] == "thumb_func":
                yield int(parts[1], 16), "thumb", parts[2]


def build_symbols(seeds, task_syms_path):
    symbols = {addr: (mode, name) for addr, mode, name in seeds}
    if task_syms_path and os.path.exists(task_syms_path):
        for addr, mode, name in read_task_symbols(task_syms_path):
            symbols[addr] = (mode, name)
    return symbols


def format_tsv(symbols):
    return "".join("0x%08X\t%s\t%s\n" % (addr, symbols[addr][0], symbols[addr][1])
                   for addr in sorted(symbols))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", default=".", help="project root (default: current directory)")
    ap.add_argument("--out", help="output TSV (default: <root>/symbols/imported_symbols.tsv)")
    ap.add_argument("--task-syms", help="gen_symbols.py output (default: <root>/symbols/task_mode_symbols.txt)")
    ap.add_argument("--seed", action="append", default=[], type=parse_seed, metavar="ADDR:MODE:NAME")
    args = ap.parse_args(argv)

    out = args.out or os.path.join(args.root, "symbols", "imported_symbols.tsv")
    task = args.task_syms or os.path.join(args.root, "symbols", "task_mode_symbols.txt")
    symbols = build_symbols(args.seed, task)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(format_tsv(symbols))
    print("Wrote %d symbols to %s" % (len(symbols), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
