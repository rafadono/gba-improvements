"""Generate a native RAM-overlay dispatcher (C++ header + source).

Scans cached recompiler output (``.c`` files) for functions located in
IWRAM/EWRAM, extracts them, and emits ``ram_overlay_dispatch.h/.cpp`` with a
binary-searched dispatch table plus an install function. All generated
identifiers are built from ``--symbol-prefix``.
"""

import argparse
import glob
import os
import re


def is_ram_pc(pc):
    return (0x02000000 <= pc < 0x02040000) or (0x03000000 <= pc < 0x03008000)


def main():
    parser = argparse.ArgumentParser(
        description="Generate a native RAM-overlay dispatcher (C++) from cached "
                    "recompiler .c output of IWRAM/EWRAM functions.")
    parser.add_argument("--root", default=".",
                        help="project root used to resolve default paths (default: current directory)")
    parser.add_argument("--cache-dir", default=None,
                        help="recompiler cache directory (default: <root>/recomp_cache)")
    parser.add_argument("--cache-pattern", default=os.path.join("**", "abi5-ram3", "*.c"),
                        help="glob, relative to the cache dir, selecting RAM-function .c files "
                             "(default: **/abi5-ram3/*.c)")
    parser.add_argument("--out-dir", default=None,
                        help="output directory for ram_overlay_dispatch.h/.cpp (default: <root>/src)")
    parser.add_argument("--symbol-prefix", default="recomp",
                        help="prefix for generated C identifiers: <prefix>_ram_dispatch_hook and "
                             "<prefix>_install_ram_dispatch (default: recomp)")
    parser.add_argument("--game-name", default="GBA game",
                        help="game name used in the generated header comment and log string "
                             "(default: 'GBA game')")
    parser.add_argument("--runtime-header", default="runtime_arm.h",
                        help="runtime header declaring g_runtime_ram_dispatch_hook (default: runtime_arm.h)")
    args = parser.parse_args()

    prefix = args.symbol_prefix
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", prefix):
        parser.error("--symbol-prefix must be a valid C identifier")
    game_name = " ".join(args.game_name.split())
    # Escape for use inside a C string literal that is also a printf format.
    log_name = game_name.replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%")

    root = args.root
    cache_dir = args.cache_dir or os.path.join(root, "recomp_cache")
    out_dir = args.out_dir or os.path.join(root, "src")
    cache_glob = os.path.join(cache_dir, args.cache_pattern)
    c_files = glob.glob(cache_glob, recursive=True)

    print(f"Scanning {len(c_files)} total .c files in cache...")

    entries = []
    seen_keys = set()

    for path in c_files:
        filename = os.path.basename(path)
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            content = f.read()

        m = re.search(r"// function 0x([0-9A-Fa-f]+) mode=(arm|thumb) end=0x([0-9A-Fa-f]+)", content)
        if not m:
            continue

        pc_str, mode_str, end_str = m.groups()
        pc = int(pc_str, 16)

        # Only process RAM (IWRAM and EWRAM) routines
        if not is_ram_pc(pc):
            continue

        thumb = 1 if mode_str == "thumb" else 0
        key = (pc, thumb)
        if key in seen_keys:
            continue
        seen_keys.add(key)

        end_pc = int(end_str, 16)

        fn_start = content.find(f"func_{pc_str}")
        if fn_start == -1:
            fn_start = content.find(f"func_{pc:08X}")
        if fn_start == -1:
            print(f"Warning: could not find func_{pc:08X} in {filename}")
            continue

        decl_start = content.rfind("OVL_EXPORT void", 0, fn_start)
        if decl_start == -1:
            decl_start = content.rfind("void", 0, fn_start)

        fn_code = content[decl_start:]
        # Normalize OVL_EXPORT void func_XXXXX to static void ram_func_XXXXX
        fn_code = re.sub(r"OVL_EXPORT\s+void\s+func_([0-9A-Fa-f]+)", r"static void ram_func_\1", fn_code, count=1)

        entries.append({
            "pc": pc,
            "thumb": thumb,
            "end": end_pc,
            "fn_name": f"ram_func_{pc:08X}",
            "code": fn_code.strip()
        })

    # Sort entries by PC then thumb mode for binary search
    entries.sort(key=lambda x: (x["pc"], x["thumb"]))
    print(f"Extracted {len(entries)} unique RAM routines (IWRAM/EWRAM).")

    os.makedirs(out_dir, exist_ok=True)
    out_header_path = os.path.join(out_dir, "ram_overlay_dispatch.h")
    header_content = (
        "#pragma once\n"
        "#include <cstdint>\n"
        "\n"
        f'extern "C" int {prefix}_ram_dispatch_hook(uint32_t pc, int thumb);\n'
        f"void {prefix}_install_ram_dispatch();\n"
    )
    with open(out_header_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header_content)
    print(f"Wrote {out_header_path}")

    out_cpp_path = os.path.join(out_dir, "ram_overlay_dispatch.cpp")
    with open(out_cpp_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(
            f"// AUTO-GENERATED RAM overlay native dispatcher for {game_name}\n"
            f"// Directly compiles and dispatches all {len(entries)} IWRAM/EWRAM routines natively.\n"
            "\n"
            '#include "ram_overlay_dispatch.h"\n'
            f'#include "{args.runtime_header}"\n'
            "#include <algorithm>\n"
            "#include <cstdio>\n"
            "\n"
            "namespace {\n"
            "\n")

        for e in entries:
            f.write(f"// --- Function 0x{e['pc']:08X} ({'thumb' if e['thumb'] else 'arm'}) ---\n")
            f.write(e["code"])
            f.write("\n\n")

        f.write("""struct RamDispatchEntry {
    uint32_t pc;
    int thumb;
    void (*fn)(void);
};

static const RamDispatchEntry kRamDispatchEntries[] = {
""")
        for e in entries:
            f.write(f"    {{ 0x{e['pc']:08X}u, {e['thumb']}, {e['fn_name']} }},\n")

        tail = """};

constexpr size_t kRamDispatchCount = sizeof(kRamDispatchEntries) / sizeof(kRamDispatchEntries[0]);

} // namespace

extern "C" int @PREFIX@_ram_dispatch_hook(uint32_t pc, int thumb) {
    uint32_t target_pc = pc & ~1u;
    int lo = 0;
    int hi = static_cast<int>(kRamDispatchCount) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const auto& e = kRamDispatchEntries[mid];
        if (e.pc < target_pc) {
            lo = mid + 1;
        } else if (e.pc > target_pc) {
            hi = mid - 1;
        } else {
            if (e.thumb == thumb) {
                e.fn();
                return 1;
            }
            if (mid > 0 && kRamDispatchEntries[mid - 1].pc == target_pc && kRamDispatchEntries[mid - 1].thumb == thumb) {
                kRamDispatchEntries[mid - 1].fn();
                return 1;
            }
            if (mid + 1 < static_cast<int>(kRamDispatchCount) && kRamDispatchEntries[mid + 1].pc == target_pc && kRamDispatchEntries[mid + 1].thumb == thumb) {
                kRamDispatchEntries[mid + 1].fn();
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

void @PREFIX@_install_ram_dispatch() {
    static bool s_installed = false;
    if (!s_installed) {
        g_runtime_ram_dispatch_hook = @PREFIX@_ram_dispatch_hook;
        s_installed = true;
        std::printf("[@LOGNAME@] Native RAM overlay dispatcher installed (%zu functions registered)\\n", kRamDispatchCount);
    }
}
"""
        f.write(tail.replace("@PREFIX@", prefix).replace("@LOGNAME@", log_name))
    print(f"Wrote {out_cpp_path} successfully ({len(entries)} functions).")


if __name__ == "__main__":
    main()
