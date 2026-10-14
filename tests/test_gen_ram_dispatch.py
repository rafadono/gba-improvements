"""Tests for tools/gen_ram_dispatch.py (no recompiler, no gbarecomp needed).

Run: python tests/test_gen_ram_dispatch.py
"""
import importlib.util
import io
import os
import re
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE_CACHE = os.path.join(HERE, "fixtures", "ram_dispatch", "cache")

spec = importlib.util.spec_from_file_location("gen_ram_dispatch", os.path.join(HERE, "..", "tools", "gen_ram_dispatch.py"))
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)

OLD_STYLE = '''// AUTO-GENERATED RAM overlay native dispatcher for Some Game
// Directly compiles and dispatches all 2 IWRAM/EWRAM routines natively.

#include "ram_overlay_dispatch.h"
#include "runtime_arm.h"
#include <algorithm>
#include <cstdio>

namespace {

static void ram_func_03000000(void) {
}

struct RamDispatchEntry {
    uint32_t pc;
    int thumb;
    void (*fn)(void);
};

static const RamDispatchEntry kRamDispatchEntries[] = {
    { 0x03000000u, 0, ram_func_03000000 },
};

constexpr size_t kRamDispatchCount = sizeof(kRamDispatchEntries) / sizeof(kRamDispatchEntries[0]);

} // namespace

extern "C" int sg_ram_dispatch_hook(uint32_t pc, int thumb) {
    return 0;
}

void sg_install_ram_dispatch() {
    static bool s_installed = false;
    if (!s_installed) {
        g_runtime_ram_dispatch_hook = sg_ram_dispatch_hook;
        s_installed = true;
        std::printf("[Some 100%% \\"Game\\"] Native RAM overlay dispatcher installed (%zu functions registered)\\n", kRamDispatchCount);
    }
}
'''


def run_gen(out_dir, *extra):
    argv = ["--cache-dir", FIXTURE_CACHE, "--cache-pattern", os.path.join("**", "abi5-ram3", "*.c"),
            "--out-dir", out_dir, "--symbol-prefix", "khfix", "--game-name", "Fixture Game"] + list(extra)
    with redirect_stdout(io.StringIO()):
        gen.main(argv)
    with open(os.path.join(out_dir, "ram_overlay_dispatch.cpp"), encoding="utf-8", newline="") as f:
        cpp = f.read()
    with open(os.path.join(out_dir, "ram_overlay_dispatch.h"), encoding="utf-8", newline="") as f:
        hdr = f.read()
    return cpp, hdr


class GeneratorOutput(unittest.TestCase):
    def test_table_sorted_ram_only_and_library_hook(self):
        with tempfile.TemporaryDirectory() as d:
            cpp, hdr = run_gen(d)
        rows = re.findall(r"\{ 0x([0-9A-F]{8})u, ([01]), (ram_func_[0-9A-F]{8}(?:_thumb)?) \},", cpp)
        keys = [(int(pc, 16), int(m)) for pc, m, _ in rows]
        self.assertEqual(keys, sorted(keys))
        self.assertEqual(keys, [(0x02000010, 1), (0x03000100, 0), (0x03000100, 1), (0x03007F00, 0)])
        # ARM and Thumb routines at one address get distinct names, and each body is defined once
        names = [n for _, _, n in rows]
        self.assertEqual(len(set(names)), len(names))
        for n in names:
            self.assertEqual(len(re.findall(r"static void " + n + r"\(void\)", cpp)), 1)
        self.assertIn("{ 0x03000100u, 1, ram_func_03000100_thumb }", cpp)
        self.assertNotIn("08000200", cpp)                      # ROM routine skipped
        self.assertIn("gbaimp::RamDispatchEntry", cpp)
        self.assertIn('#include "gbaimp/ram_dispatch_gbarecomp.h"', cpp)
        self.assertTrue(cpp.rstrip().endswith('GBAIMP_DEFINE_RAM_DISPATCH_N(khfix, "Fixture Game", kRamDispatchEntries, 4)'))
        # nothing of the old self-contained hook remains
        self.assertNotIn("while (lo <= hi)", cpp)
        self.assertNotIn("g_runtime_ram_dispatch_hook", cpp)
        self.assertIn("all 4 IWRAM/EWRAM routines", cpp)
        self.assertIn('extern "C" int khfix_ram_dispatch_hook(uint32_t pc, int thumb);', hdr)
        self.assertIn("void khfix_install_ram_dispatch();", hdr)

    def test_deterministic(self):
        with tempfile.TemporaryDirectory() as d1, tempfile.TemporaryDirectory() as d2:
            self.assertEqual(run_gen(d1), run_gen(d2))

    def test_empty_table_uses_placeholder_and_zero_count(self):
        text = gen.render_cpp([], "p", "G", "runtime_arm.h")
        self.assertIn("{ 0u, 0, nullptr }", text)
        self.assertIn('GBAIMP_DEFINE_RAM_DISPATCH_N(p, "G", kRamDispatchEntries, 0)', text)

    def test_log_name_escaping(self):
        text = gen.render_cpp([], "p", 'A "B" \\ 100%', "runtime_arm.h")
        self.assertIn('"A \\"B\\" \\\\ 100%"', text)           # C-escaped, '%' stays (it is an argument now)

    def test_invalid_prefix_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(SystemExit), redirect_stdout(io.StringIO()):
                gen.main(["--cache-dir", FIXTURE_CACHE, "--out-dir", d, "--symbol-prefix", "1bad"])


class Migration(unittest.TestCase):
    def test_migrates_old_tail(self):
        new = gen.migrate_text(OLD_STYLE)
        self.assertIsNotNone(new)
        self.assertNotIn("g_runtime_ram_dispatch_hook", new)
        self.assertIn('#include "gbaimp/ram_dispatch_gbarecomp.h"', new)
        self.assertTrue(new.rstrip().endswith(
            'GBAIMP_DEFINE_RAM_DISPATCH_N(sg, "Some 100% \\"Game\\"", kRamDispatchEntries, kRamDispatchCount)'))
        # the include goes right after the runtime header; the table and the routines are untouched
        self.assertLess(new.index('runtime_arm.h"'), new.index("ram_dispatch_gbarecomp.h"))
        self.assertIn("{ 0x03000000u, 0, ram_func_03000000 },", new)

    def test_idempotent(self):
        once = gen.migrate_text(OLD_STYLE)
        self.assertIsNone(gen.migrate_text(once))

    def test_preserves_crlf(self):
        crlf = OLD_STYLE.replace("\n", "\r\n")
        new = gen.migrate_text(crlf)
        self.assertNotIn("\n", new.replace("\r\n", ""))
        self.assertEqual(new.replace("\r\n", "\n"), gen.migrate_text(OLD_STYLE))

    def test_unrelated_file_untouched(self):
        self.assertIsNone(gen.migrate_text("int main() { return 0; }\n"))

    def test_unexpected_layout_raises(self):
        broken = OLD_STYLE.replace("g_runtime_ram_dispatch_hook = sg_ram_dispatch_hook;", "")
        with self.assertRaises(ValueError):
            gen.migrate_text(broken)

    def test_migrate_files_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "ram_overlay_dispatch.cpp")
            with open(p, "w", encoding="utf-8", newline="") as f:
                f.write(OLD_STYLE)
            with redirect_stdout(io.StringIO()):
                gen.migrate_files([p, p])   # second run reports "nothing to migrate"
            with open(p, encoding="utf-8", newline="") as f:
                self.assertEqual(f.read(), gen.migrate_text(OLD_STYLE))


if __name__ == "__main__":
    unittest.main()
