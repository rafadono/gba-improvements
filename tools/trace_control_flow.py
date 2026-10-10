"""Trace the control flow of a GBA ROM to discover reachable functions.

Starting from the entry point (and optional seed symbols) the script follows
Thumb/ARM branches and calls inside the code region, then scans literal pools
for function pointers, and writes a TSV: ``addr<TAB>mode<TAB>name``.
"""

import argparse
import os
import sys


def hex_int(text):
    """argparse type: integer accepting 0x... hex or decimal."""
    return int(text, 0)


def sign_extend(val, bits):
    sign_bit = 1 << (bits - 1)
    return (val & (sign_bit - 1)) - (val & sign_bit)

def trace_cfg(rom_path, rom_base, code_limit, entry_point, seeds_tsv):
    if not os.path.exists(rom_path):
        print(f"Error: ROM not found: {rom_path}")
        sys.exit(1)

    with open(rom_path, "rb") as f:
        rom = f.read()

    rom_len = len(rom)

    def read_u16(addr):
        off = addr - rom_base
        if 0 <= off <= rom_len - 2:
            return rom[off] | (rom[off + 1] << 8)
        return None

    def read_u32(addr):
        off = addr - rom_base
        if 0 <= off <= rom_len - 4:
            return int.from_bytes(rom[off:off + 4], "little")
        return None

    # Load existing symbols as initial ground-truth seeds
    known_funcs = {}
    tsv_path = seeds_tsv
    if tsv_path and os.path.exists(tsv_path):
        with open(tsv_path, "r", encoding="utf-8") as f:
            for line in f:
                parts = line.strip().split("\t")
                if len(parts) >= 2:
                    try:
                        addr = int(parts[0], 16)
                        mode = parts[1]
                        known_funcs[addr] = mode
                    except ValueError:
                        pass

    # The entry point is traced as ARM code
    discovered_funcs = {entry_point: "arm"}
    for addr, mode in known_funcs.items():
        if addr < code_limit:
            discovered_funcs[addr] = mode

    visited_instructions = set()
    queue = [(entry_point, "arm")]
    for addr, mode in list(discovered_funcs.items()):
        queue.append((addr, mode))

    print(f"Initial seed functions: {len(discovered_funcs)}")

    # CFG Disassembly loop
    while queue:
        addr, mode = queue.pop()
        if addr in visited_instructions or addr < rom_base or addr >= code_limit:
            continue

        if mode == "thumb":
            # Disassemble Thumb basic block
            curr = addr & ~1
            while curr < code_limit and curr not in visited_instructions:
                visited_instructions.add(curr)
                h0 = read_u16(curr)
                if h0 is None:
                    break

                # Check BL (long branch with link: 2 halfwords)
                if (h0 & 0xF800) == 0xF000:
                    h1 = read_u16(curr + 2)
                    if h1 is not None and (h1 & 0xF800) == 0xF800:
                        visited_instructions.add(curr + 2)
                        imm11_h = h0 & 0x07FF
                        imm11_l = h1 & 0x07FF
                        offset = sign_extend((imm11_h << 12) | (imm11_l << 1), 23)
                        target = (curr + 4 + offset) & 0xFFFFFFFE
                        if rom_base <= target < code_limit:
                            if target not in discovered_funcs:
                                discovered_funcs[target] = "thumb"
                                queue.append((target, "thumb"))
                        curr += 4
                        continue

                # Check unconditional branch: B <imm11>
                if (h0 & 0xF800) == 0xE000:
                    imm11 = h0 & 0x07FF
                    offset = sign_extend(imm11 << 1, 12)
                    target = (curr + 4 + offset) & 0xFFFFFFFE
                    if rom_base <= target < code_limit and target not in visited_instructions:
                        queue.append((target, "thumb"))
                    break # End of basic block

                # Check conditional branch: B<cond> <imm8>
                if (h0 & 0xF000) == 0xD000:
                    cond = (h0 >> 8) & 0x0F
                    if cond < 0x0E: # 0x0E is undefined, 0x0F is SWI
                        imm8 = h0 & 0x00FF
                        offset = sign_extend(imm8 << 1, 9)
                        target = (curr + 4 + offset) & 0xFFFFFFFE
                        if rom_base <= target < code_limit and target not in visited_instructions:
                            queue.append((target, "thumb"))
                    elif cond == 0x0F:
                        # SWI instruction
                        pass

                # Check BX / BLX
                if (h0 & 0xFF00) == 0x4700:
                    # BX Rm: if Rm == LR (0x4770), return
                    if h0 == 0x4770:
                        break
                    # Otherwise indirect branch

                # Check POP {..., PC}
                if (h0 & 0xFF00) == 0xBD00:
                    break

                curr += 2

        elif mode == "arm":
            # Disassemble ARM basic block
            curr = addr & ~3
            while curr < code_limit and curr not in visited_instructions:
                visited_instructions.add(curr)
                word = read_u32(curr)
                if word is None:
                    break

                cond = (word >> 28) & 0x0F
                op = (word >> 24) & 0x0F

                # B or BL
                if op == 0x0A or op == 0x0B: # 1010 or 1011
                    imm24 = word & 0x00FFFFFF
                    offset = sign_extend(imm24 << 2, 26)
                    target = curr + 8 + offset
                    is_link = (op == 0x0B)
                    if rom_base <= target < code_limit:
                        if is_link:
                            if target not in discovered_funcs:
                                discovered_funcs[target] = "arm"
                                queue.append((target, "arm"))
                        else:
                            if target not in visited_instructions:
                                queue.append((target, "arm"))
                    if not is_link and cond == 0x0E: # Unconditional B
                        break

                # BX Rm
                if (word & 0x0FFFFFF0) == 0x012FFF10:
                    if (word & 0xF) == 0x0E: # BX LR
                        break

                # LDMFD SP!, {..., PC}
                if (word & 0x0E108000) == 0x08108000:
                    if cond == 0x0E:
                        break

                curr += 4

    # Literal pool scan: identify jump tables and function pointer arrays referenced by code
    print(f"Tracing literal pools for function pointer tables...")
    for offset in range(0, min(rom_len - 4, code_limit - rom_base), 4):
        val = int.from_bytes(rom[offset:offset + 4], "little")
        if rom_base <= val < code_limit:
            target_addr = val & ~1
            mode = "thumb" if (val & 1) else "arm"
            # Verify if target looks like a valid function prologue
            t_off = target_addr - rom_base
            if 0 <= t_off <= rom_len - 4:
                b0 = rom[t_off]
                b1 = rom[t_off + 1]
                if mode == "thumb" and b1 in (0xB5, 0xB4): # PUSH {..., lr} or PUSH {r4-r7}
                    if target_addr not in discovered_funcs:
                        discovered_funcs[target_addr] = mode
                elif mode == "arm":
                    word = int.from_bytes(rom[t_off:t_off + 4], "little")
                    if (word >> 20) & 0xFF == 0x92 and (word & (1 << 14)): # STMFD sp!, {..., lr}
                        if target_addr not in discovered_funcs:
                            discovered_funcs[target_addr] = mode

    print(f"Total CFG-validated reachable functions: {len(discovered_funcs)}")
    return discovered_funcs

def main():
    parser = argparse.ArgumentParser(
        description="Trace reachable ARM/Thumb functions in a GBA ROM by "
                    "following branches from the entry point and scanning "
                    "literal pools for function pointers.")
    parser.add_argument("--rom", required=True, help="path to the .gba ROM image")
    parser.add_argument("--root", default=".",
                        help="project root used to resolve default paths (default: current directory)")
    parser.add_argument("--rom-base", type=hex_int, default=0x08000000,
                        help="ROM load address (default: 0x08000000)")
    parser.add_argument("--code-limit", type=hex_int, default=0x08140000,
                        help="exclusive upper address of the executable code region (default: 0x08140000)")
    parser.add_argument("--entry-point", type=hex_int, default=0x080000C0,
                        help="entry point address, traced as ARM code (default: 0x080000C0)")
    parser.add_argument("--seeds", default=None,
                        help="optional TSV of known functions (addr<TAB>mode[<TAB>name]) used as seeds "
                             "(default: <root>/symbols/imported_symbols.tsv if it exists)")
    parser.add_argument("-o", "--output", default=None,
                        help="output TSV (default: <root>/symbols/reachable_cfg_symbols.tsv)")
    args = parser.parse_args()

    seeds = args.seeds or os.path.join(args.root, "symbols", "imported_symbols.tsv")
    out_file = args.output or os.path.join(args.root, "symbols", "reachable_cfg_symbols.tsv")
    funcs = trace_cfg(args.rom, args.rom_base, args.code_limit, args.entry_point, seeds)
    os.makedirs(os.path.dirname(os.path.abspath(out_file)), exist_ok=True)
    with open(out_file, "w", encoding="utf-8") as f:
        for addr in sorted(funcs.keys()):
            f.write(f"0x{addr:08X}\t{funcs[addr]}\tfunc_{addr:08X}\n")
    print(f"Exported to {out_file}")


if __name__ == "__main__":
    main()
