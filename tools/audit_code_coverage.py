"""Audit how much of a GBA ROM's code is covered by recompiled functions.

Scans the ROM for plausible ARM/Thumb function prologues, compares them with
the known symbol set (recompiler TOML + imported symbols TSV) and writes a
Markdown report listing orphaned (uncompiled) candidates.
"""

import argparse
import os
import re
import sys


def hex_int(text):
    """argparse type: integer accepting 0x... hex or decimal."""
    return int(text, 0)


def is_thumb_prologue(b0, b1):
    # PUSH {..., LR}: 1011 0101 xxxx xxxx (0xB5xx)
    return b1 == 0xB5

def is_thumb_epilogue(b0, b1):
    # POP {..., PC}: 1011 1101 xxxx xxxx (0xBDxx)
    # BX LR: 0100 0111 0111 0000 (0x4770)
    if b1 == 0xBD:
        return True
    if b1 == 0x47 and b0 == 0x70:
        return True
    return False

def is_arm_prologue(word):
    # STMFD sp!, {..., lr} -> 0xE92Dxxxx with bit 14 set (LR)
    cond = (word >> 28) & 0xF
    if cond in (0xE, 0x0, 0x1): # AL, EQ, NE
        op = (word >> 20) & 0xFF
        if op == 0x92 and (word & (1 << 14)):
            return True
    return False

def is_arm_epilogue(word):
    # LDMFD sp!, {..., pc} -> 0xE8BDxxxx with bit 15 set (PC)
    # BX lr -> 0xE12FFF1E
    if word == 0xE12FFF1E:
        return True
    cond = (word >> 28) & 0xF
    if cond in (0xE, 0x0, 0x1):
        op = (word >> 20) & 0xFF
        if op == 0x8B and (word & (1 << 15)):
            return True
    return False

def load_recompiled_symbols(toml_path, tsv_path):
    compiled = {}
    if os.path.exists(toml_path):
        with open(toml_path, "r", encoding="utf-8", errors="ignore") as f:
            for m in re.finditer(r'\[\[extra_func\]\]\s*addr\s*=\s*(0x[0-9a-fA-F]+)\s*mode\s*=\s*\"([^\"]+)\"(?:\s*name\s*=\s*\"([^\"]+)\")?', f.read()):
                addr = int(m.group(1), 16)
                compiled[addr] = {
                    "mode": m.group(2),
                    "name": m.group(3) or f"func_{addr:08X}",
                    "source": "toml"
                }

    if os.path.exists(tsv_path):
        with open(tsv_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                parts = line.split("\t")
                addr = int(parts[0], 16)
                if addr not in compiled:
                    compiled[addr] = {
                        "mode": parts[1],
                        "name": parts[2] if len(parts) > 2 else f"func_{addr:08X}",
                        "source": "tsv"
                    }
    return compiled

def audit(args):
    root = args.root
    rom_path = args.rom
    if not rom_path or not os.path.exists(rom_path):
        roms_dir = os.path.join(root, "roms")
        candidates = []
        if os.path.isdir(roms_dir):
            candidates = [os.path.join(roms_dir, f) for f in sorted(os.listdir(roms_dir)) if f.endswith(".gba")]
        if candidates:
            rom_path = candidates[0]
        else:
            print("Error: No ROM found (use --rom).")
            sys.exit(1)

    toml_path = args.toml or os.path.join(root, "config", "recomp.toml")
    tsv_path = args.symbols_tsv or os.path.join(root, "symbols", "imported_symbols.tsv")
    report_path = args.report or os.path.join(root, "docs", "CODE_COVERAGE_AUDIT.md")
    scan_limit = args.scan_limit

    print("================================================================")
    print("            Code Coverage & Orphan Function Audit               ")
    print("================================================================")
    print(f"ROM Image: {rom_path}")
    print(f"Scanning the first 0x{scan_limit:X} bytes of the ROM (base 0x{args.rom_base:08X})...")
    print()

    with open(rom_path, "rb") as f:
        rom_data = f.read()

    rom_len = len(rom_data)
    rom_base = args.rom_base

    compiled_symbols = load_recompiled_symbols(toml_path, tsv_path)
    print(f"Total Symbols in Recompilation Database: {len(compiled_symbols)}")

    # Detect code region by scanning for valid instructions and function prologues
    detected_prologues = {}

    # Scan for Thumb prologues (2-byte aligned)
    for offset in range(0, min(rom_len - 4, scan_limit), 2):
        addr = rom_base + offset
        b0 = rom_data[offset]
        b1 = rom_data[offset + 1]

        if is_thumb_prologue(b0, b1):
            # Check if likely preceded by another function's epilogue or alignment padding
            is_plausible = False
            if offset >= 2:
                prev_b0 = rom_data[offset - 2]
                prev_b1 = rom_data[offset - 1]
                if is_thumb_epilogue(prev_b0, prev_b1):
                    is_plausible = True
                elif prev_b0 == 0x00 and prev_b1 == 0x00:
                    is_plausible = True
                elif prev_b0 == 0xC0 and prev_b1 == 0x46: # nop
                    is_plausible = True
            if offset == 0:
                is_plausible = True

            # If it's already an acknowledged symbol, it's 100% genuine
            if addr in compiled_symbols:
                is_plausible = True

            if is_plausible:
                detected_prologues[addr] = "thumb"

    # Scan for ARM prologues (4-byte aligned)
    for offset in range(0, min(rom_len - 8, scan_limit), 4):
        addr = rom_base + offset
        word = int.from_bytes(rom_data[offset:offset+4], "little")
        if is_arm_prologue(word):
            detected_prologues[addr] = "arm"

    # Merge all explicitly compiled symbols as ground truth
    for addr, sym in compiled_symbols.items():
        if addr < rom_base + rom_len:
            detected_prologues[addr] = sym["mode"]

    total_detected = len(detected_prologues)
    covered_funcs = []
    orphaned_funcs = []

    for addr in sorted(detected_prologues.keys()):
        mode = detected_prologues[addr]
        if addr in compiled_symbols:
            covered_funcs.append((addr, mode, compiled_symbols[addr]["name"]))
        else:
            orphaned_funcs.append((addr, mode))

    covered_count = len(covered_funcs)
    orphan_count = len(orphaned_funcs)
    coverage_pct = (covered_count / total_detected * 100.0) if total_detected > 0 else 0.0

    print("----------------------------------------------------------------")
    print("                     Coverage Summary                           ")
    print("----------------------------------------------------------------")
    print(f"Total Identified Code Functions:   {total_detected}")
    print(f"Statically Recompiled Functions:   {covered_count}")
    print(f"Orphaned (Uncompiled) Candidates:  {orphan_count}")
    print(f"Overall Code Coverage Percentage:  {coverage_pct:.2f}%")
    print("----------------------------------------------------------------")
    print()

    # Write detailed markdown audit report
    os.makedirs(os.path.dirname(os.path.abspath(report_path)), exist_ok=True)

    with open(report_path, "w", encoding="utf-8") as f:
        f.write("# Code Coverage and Static Recompilation Audit\n\n")
        f.write("## Executive Summary\n\n")
        f.write(f"- **Target ROM:** {args.game_name}\n")
        f.write(f"- **Total Identified Functions:** {total_detected}\n")
        f.write(f"- **Statically Recompiled Functions:** {covered_count}\n")
        f.write(f"- **Orphaned Candidates:** {orphan_count}\n")
        f.write(f"- **Coverage Metric:** {coverage_pct:.2f}%\n\n")

        f.write("## Top Identified Orphan Candidates\n\n")
        f.write("| Address | Mode | Prologue Bytes | Notes |\n")
        f.write("|---|---|---|---|\n")

        limit = min(50, len(orphaned_funcs))
        for addr, mode in orphaned_funcs[:limit]:
            offset = addr - rom_base
            raw_bytes = rom_data[offset:offset+4].hex()
            f.write(f"| `0x{addr:08X}` | {mode} | `{raw_bytes}` | Potential unindexed helper |\n")

        f.write("\n## Recompiled Functions Distribution\n\n")
        f.write(f"All {covered_count} recompiled functions are listed in `{os.path.basename(toml_path)}` or the imported symbols TSV.\n")

    print(f"Full audit report written to: {report_path}")
    print()
    if orphan_count > 0:
        print(f"First 10 Orphan Candidates for Ingestion:")
        for addr, mode in orphaned_funcs[:10]:
            offset = addr - rom_base
            raw_bytes = rom_data[offset:offset+4].hex()
            print(f"  0x{addr:08X} ({mode}): bytes={raw_bytes}")

def main():
    parser = argparse.ArgumentParser(
        description="Audit recompilation coverage of a GBA ROM: detect function "
                    "prologues, compare against known symbols and write a Markdown report.")
    parser.add_argument("--rom", default=None,
                        help="path to the .gba ROM (default: first *.gba found in <root>/roms)")
    parser.add_argument("--root", default=".",
                        help="project root used to resolve default paths (default: current directory)")
    parser.add_argument("--toml", default=None,
                        help="recompiler TOML with [[extra_func]] entries (default: <root>/config/recomp.toml)")
    parser.add_argument("--symbols-tsv", default=None,
                        help="imported symbols TSV, addr<TAB>mode[<TAB>name] (default: <root>/symbols/imported_symbols.tsv)")
    parser.add_argument("--report", default=None,
                        help="Markdown report to write (default: <root>/docs/CODE_COVERAGE_AUDIT.md)")
    parser.add_argument("--game-name", default="GBA game",
                        help="game name written in the report (default: 'GBA game')")
    parser.add_argument("--rom-base", type=hex_int, default=0x08000000,
                        help="ROM load address (default: 0x08000000)")
    parser.add_argument("--scan-limit", type=hex_int, default=0x00200000,
                        help="number of ROM bytes (from the start) scanned for prologues (default: 0x200000)")
    audit(parser.parse_args())


if __name__ == "__main__":
    main()
