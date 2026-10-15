#!/usr/bin/env python3
"""Run gbarecomp's decompilation symbol importer for a project with the usual layout.

A thin wrapper: it checks that the importer and the input files exist, then runs

  python <importer> --id ID [--name NAME] --syms SYMS [--sections F] [--map F] --rom ROM --out DIR [extra...]

Usage:
  import_decomp_symbols.py --id ID --syms FILE --rom ROM [--name NAME] [--sections FILE] [--map FILE]
                           [--out DIR] [--importer PATH] [--root DIR] [-- extra importer arguments]

Defaults: --root is the current directory; --importer is
<root>/gbarecomp/tools/symbol_import/import_decomp_symbols.py; --out is <root>/symbols.
Relative --syms/--sections/--map/--rom/--importer/--out paths are resolved against --root.
The exit status is the importer's.
"""

import argparse
import os
import subprocess
import sys


def build_command(args, extra):
    """Return (importer_path, argv list) for the given parsed arguments."""
    def path(p):
        return p if os.path.isabs(p) else os.path.join(args.root, p)

    importer = path(args.importer) if args.importer else os.path.join(
        args.root, "gbarecomp", "tools", "symbol_import", "import_decomp_symbols.py")
    out = path(args.out) if args.out else os.path.join(args.root, "symbols")
    cmd = [sys.executable, importer, "--id", args.id]
    if args.name:
        cmd += ["--name", args.name]
    cmd += ["--syms", path(args.syms)]
    if args.sections:
        cmd += ["--sections", path(args.sections)]
    if args.map:
        cmd += ["--map", path(args.map)]
    cmd += ["--rom", path(args.rom), "--out", out]
    return importer, cmd + list(extra)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", default=".")
    ap.add_argument("--importer")
    ap.add_argument("--id", required=True, help="ROM id (for example the game code)")
    ap.add_argument("--name", default="", help="human readable game name")
    ap.add_argument("--syms", required=True)
    ap.add_argument("--sections")
    ap.add_argument("--map")
    ap.add_argument("--rom", required=True)
    ap.add_argument("--out")
    args, extra = ap.parse_known_args(argv)
    if extra and extra[0] == "--":
        extra = extra[1:]

    importer, cmd = build_command(args, extra)
    if not os.path.exists(importer):
        print("Error: importer not found at %s" % importer, file=sys.stderr)
        return 1
    for label, p in (("symbols", args.syms), ("ROM", args.rom)):
        full = p if os.path.isabs(p) else os.path.join(args.root, p)
        if not os.path.exists(full):
            print("Error: %s file not found: %s" % (label, full), file=sys.stderr)
            return 1
    print("Running symbol import for %s..." % cmd[cmd.index("--rom") + 1])
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
