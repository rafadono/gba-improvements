#!/usr/bin/env python3
"""Append functions found in the recompiler's miss cache to a recompiler TOML.

The recompiler caches code it had to translate at run time as files named
`<addr>_<size>_<t|a>.c` (hex address, then a mode letter: `t` = Thumb, anything else = ARM)
somewhere below a cache directory. This script adds every address that is not yet
declared (`addr = 0x...`) to the TOML as an `[[extra_func]]` entry named `func_<ADDR>`.

Usage:
  merge_cache_misses.py [--root DIR] [--toml FILE] [--cache-dir DIR] [--min-addr ADDR] [--dry-run]

Defaults (relative to --root, default: current directory): the single `config/*.toml`
(an error when there is none or several), cache directory `recomp_cache`,
--min-addr 0x08000000 (ROM only; use 0 to accept every address).
"""

import argparse
import glob
import os
import re
import sys


def hex_int(text):
    """argparse type: integer accepting 0x... hex or decimal."""
    return int(text, 0)


def parse_cache_name(filename):
    """`<addr>_<size>_<mode>.c` -> (addr, 'thumb' | 'arm'), or None when it is not a cache file."""
    base = os.path.basename(filename)
    if not base.endswith(".c"):
        return None
    parts = base[:-2].split("_")
    if len(parts) < 3:
        return None
    try:
        addr = int(parts[0], 16)
    except ValueError:
        return None
    return addr, ("thumb" if parts[2] == "t" else "arm")


def declared_addresses(toml_text):
    return {int(m.group(1), 16) for m in re.finditer(r"addr\s*=\s*(0x[0-9a-fA-F]+)", toml_text)}


def find_new_functions(toml_text, cache_files, min_addr):
    """Sorted [(addr, mode)] of cached functions at or above `min_addr` and not yet in the TOML."""
    existing = declared_addresses(toml_text)
    found = []
    for path in cache_files:
        parsed = parse_cache_name(path)
        if parsed is None:
            continue
        addr, mode = parsed
        if addr >= min_addr and addr not in existing:
            existing.add(addr)
            found.append((addr, mode))
    return sorted(found)


def format_entries(funcs):
    return "".join(
        '\n[[extra_func]]\naddr = 0x{0:08X}\nmode = "{1}"\nname = "func_{0:08X}"\n'.format(a, m)
        for a, m in funcs)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", default=".", help="project root (default: current directory)")
    ap.add_argument("--toml", help="recompiler TOML to extend (default: the only <root>/config/*.toml)")
    ap.add_argument("--cache-dir", help="miss cache directory (default: <root>/recomp_cache)")
    ap.add_argument("--min-addr", type=hex_int, default=0x08000000,
                    help="ignore cached functions below this address (default: 0x08000000)")
    ap.add_argument("--dry-run", action="store_true", help="report only, do not modify the TOML")
    args = ap.parse_args(argv)

    toml_path = args.toml
    if not toml_path:
        candidates = sorted(glob.glob(os.path.join(args.root, "config", "*.toml")))
        if len(candidates) != 1:
            print("Expected exactly one config/*.toml (found %d); pass --toml." % len(candidates),
                  file=sys.stderr)
            return 1
        toml_path = candidates[0]
    cache_dir = args.cache_dir or os.path.join(args.root, "recomp_cache")
    print("Using config TOML: %s" % toml_path)

    cache_files = glob.glob(os.path.join(cache_dir, "**", "*_*.c"), recursive=True)
    cache_files = [p for p in cache_files if parse_cache_name(p) is not None]
    print("Found %d cached miss files" % len(cache_files))

    with open(toml_path, "r", encoding="utf-8") as f:
        content = f.read()
    funcs = find_new_functions(content, cache_files, args.min_addr)
    print("New functions to add: %d" % len(funcs))
    if funcs and not args.dry_run:
        with open(toml_path, "a", encoding="utf-8", newline="\n") as f:
            f.write(format_entries(funcs))
        print("Updated %s with cached miss functions." % toml_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
