#!/usr/bin/env python3
"""Unit tests of the helper scripts in tools/ (merge_cache_misses, update_symbols, import_decomp_symbols,
png_to_ppm, gen_font_data). Standard library only; run with `python -I tests/test_tools.py`."""
import contextlib
import importlib.util
import io
import os
import struct
import sys
import tempfile
import unittest
import zlib

TOOLS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools")


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(TOOLS, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def quiet(fn, *a, **k):
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return fn(*a, **k)


def read(path, binary=False):
    with open(path, "rb" if binary else "r", **({} if binary else {"encoding": "utf-8"})) as f:
        return f.read()


def write(path, text, binary=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb" if binary else "w", **({} if binary else {"encoding": "utf-8", "newline": "\n"})) as f:
        f.write(text)


class MergeCacheMisses(unittest.TestCase):
    def setUp(self):
        self.m = load("merge_cache_misses")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_parse_cache_name(self):
        self.assertEqual(self.m.parse_cache_name("08001234_20_t.c"), (0x08001234, "thumb"))
        self.assertEqual(self.m.parse_cache_name("/x/y/03000000_8_a.c"), (0x03000000, "arm"))
        self.assertIsNone(self.m.parse_cache_name("notes.c"))
        self.assertIsNone(self.m.parse_cache_name("zz_1_t.c"))
        self.assertIsNone(self.m.parse_cache_name("08001234_20_t.h"))

    def test_find_new_functions_filters_and_dedups(self):
        toml = '[[extra_func]]\naddr = 0x08000100\nmode = "arm"\n'
        files = ["08000100_4_a.c", "08000200_4_t.c", "sub/08000200_4_t.c", "03000000_4_a.c", "08000050_4_a.c"]
        got = self.m.find_new_functions(toml, files, 0x08000000)
        self.assertEqual(got, [(0x08000050, "arm"), (0x08000200, "thumb")])
        self.assertEqual(self.m.find_new_functions(toml, files, 0)[0], (0x03000000, "arm"))

    def test_main_appends_and_is_idempotent(self):
        toml = os.path.join(self.root, "config", "game.toml")
        write(toml, 'name = "x"\n')
        write(os.path.join(self.root, "recomp_cache", "a", "08000200_4_t.c"), "")
        write(os.path.join(self.root, "recomp_cache", "08000300_4_a.c"), "")
        self.assertEqual(quiet(self.m.main, ["--root", self.root]), 0)
        text = read(toml)
        self.assertIn('addr = 0x08000200\nmode = "thumb"\nname = "func_08000200"', text)
        self.assertIn('addr = 0x08000300\nmode = "arm"', text)
        self.assertEqual(quiet(self.m.main, ["--root", self.root]), 0)
        self.assertEqual(read(toml), text)

    def test_dry_run_and_ambiguous_toml(self):
        write(os.path.join(self.root, "config", "a.toml"), "")
        write(os.path.join(self.root, "recomp_cache", "08000200_4_t.c"), "")
        self.assertEqual(quiet(self.m.main, ["--root", self.root, "--dry-run"]), 0)
        self.assertEqual(read(os.path.join(self.root, "config", "a.toml")), "")
        write(os.path.join(self.root, "config", "b.toml"), "")
        self.assertEqual(quiet(self.m.main, ["--root", self.root]), 1)
        self.assertEqual(quiet(self.m.main, ["--root", self.root, "--toml", os.path.join(self.root, "config", "a.toml")]), 0)


class UpdateSymbols(unittest.TestCase):
    def setUp(self):
        self.m = load("update_symbols")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_parse_seed(self):
        self.assertEqual(self.m.parse_seed("0x080000C0:ARM:crt0_entry"), (0x080000C0, "arm", "crt0_entry"))
        for bad in ("0x1:arm", "0x1:foo:n", "zz:arm:n", "0x1:arm:"):
            with self.assertRaises(Exception):
                self.m.parse_seed(bad)

    def test_merge_sorted_and_task_overrides_seed(self):
        write(os.path.join(self.root, "symbols", "task_mode_symbols.txt"),
              "# comment\nthumb_func 0x08000200 task_a_0\nthumb_func 08000100 task_b_0\nother 1 2\n")
        rc = quiet(self.m.main, ["--root", self.root, "--seed", "0x08000000:arm:start",
                                 "--seed", "0x08000100:arm:seed_name"])
        self.assertEqual(rc, 0)
        out = read(os.path.join(self.root, "symbols", "imported_symbols.tsv"))
        self.assertEqual(out, "0x08000000\tarm\tstart\n0x08000100\tthumb\ttask_b_0\n0x08000200\tthumb\ttask_a_0\n")

    def test_missing_task_file_is_fine(self):
        os.makedirs(os.path.join(self.root, "symbols"))
        self.assertEqual(quiet(self.m.main, ["--root", self.root, "--seed", "0x1:thumb:f"]), 0)
        self.assertEqual(read(os.path.join(self.root, "symbols", "imported_symbols.tsv")), "0x00000001\tthumb\tf\n")


class ImportDecompSymbols(unittest.TestCase):
    def setUp(self):
        self.m = load("import_decomp_symbols")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_forwards_arguments_to_the_importer(self):
        # A fake importer that records its argv.
        imp = os.path.join(self.root, "gbarecomp", "tools", "symbol_import", "import_decomp_symbols.py")
        log = os.path.join(self.root, "argv.txt")
        write(imp, "import sys\nopen(%r, 'w').write('\\n'.join(sys.argv[1:]))\n" % log)
        write(os.path.join(self.root, "symbols", "g_syms.txt"), "")
        write(os.path.join(self.root, "roms", "g.gba"), b"\0", binary=True)
        rc = quiet(self.m.main, ["--root", self.root, "--id", "ABCD", "--name", "Game", "--syms", "symbols/g_syms.txt",
                                 "--rom", "roms/g.gba", "--", "--dry-run"])
        self.assertEqual(rc, 0)
        argv = read(log).split("\n")
        self.assertEqual(argv[:4], ["--id", "ABCD", "--name", "Game"])
        self.assertEqual(argv[argv.index("--syms") + 1], os.path.join(self.root, "symbols/g_syms.txt"))
        self.assertEqual(argv[argv.index("--out") + 1], os.path.join(self.root, "symbols"))
        self.assertEqual(argv[-1], "--dry-run")
        self.assertNotIn("--sections", argv)

    def test_reports_missing_importer_and_inputs(self):
        write(os.path.join(self.root, "symbols", "s.txt"), "")
        self.assertEqual(quiet(self.m.main, ["--root", self.root, "--id", "X", "--syms", "symbols/s.txt",
                                             "--rom", "nope.gba"]), 1)
        write(os.path.join(self.root, "gbarecomp", "tools", "symbol_import", "import_decomp_symbols.py"), "")
        self.assertEqual(quiet(self.m.main, ["--root", self.root, "--id", "X", "--syms", "symbols/s.txt",
                                             "--rom", "nope.gba"]), 1)


def make_png(width, height, rows, ctype=2, filters=None):
    """rows: list of bytes (unfiltered, width*bpp each). Applies per-row PNG filters."""
    bpp = 3 if ctype == 2 else 4
    raw = bytearray()
    prev = bytes(width * bpp)
    for y, row in enumerate(rows):
        f = (filters or [0] * height)[y]
        out = bytearray()
        for i, v in enumerate(row):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 0:
                p = 0
            elif f == 1:
                p = a
            elif f == 2:
                p = b
            elif f == 3:
                p = (a + b) >> 1
            else:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                p = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            out.append((v - p) & 255)
        raw.append(f)
        raw += out
        prev = row

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, ctype, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


class PngToPpm(unittest.TestCase):
    def setUp(self):
        self.m = load("png_to_ppm")

    def test_all_filters_rgb(self):
        w, h = 5, 5
        rows = [bytes(((x * 37 + y * 91 + c * 13) & 255) for x in range(w) for c in range(3)) for y in range(h)]
        png = make_png(w, h, rows, ctype=2, filters=[0, 1, 2, 3, 4])
        self.assertEqual(self.m.png_to_ppm(png), b"P6 5 5 255\n" + b"".join(rows))

    def test_rgba_drops_alpha(self):
        rows = [bytes([10, 20, 30, 255, 40, 50, 60, 128]), bytes([1, 2, 3, 0, 4, 5, 6, 7])]
        png = make_png(2, 2, rows, ctype=6, filters=[4, 1])
        self.assertEqual(self.m.png_to_ppm(png), b"P6 2 2 255\n" + bytes([10, 20, 30, 40, 50, 60, 1, 2, 3, 4, 5, 6]))

    def test_rejects_other_formats(self):
        with self.assertRaises(ValueError):
            self.m.png_to_ppm(b"not a png at all")
        bad = bytearray(make_png(1, 1, [bytes(3)]))
        bad[24] = 16  # bit depth
        with self.assertRaises(ValueError):
            self.m.png_to_ppm(bytes(bad))

    def test_cli(self):
        with tempfile.TemporaryDirectory() as d:
            src, dst = os.path.join(d, "a.png"), os.path.join(d, "a.ppm")
            write(src, make_png(1, 1, [bytes([9, 8, 7])]), binary=True)
            self.assertEqual(self.m.main([src, dst]), 0)
            self.assertEqual(read(dst, binary=True), b"P6 1 1 255\n\x09\x08\x07")
            self.assertEqual(quiet(self.m.main, [src]), 2)


class GenFontData(unittest.TestCase):
    def setUp(self):
        self.m = load("gen_font_data")

    def test_default_output(self):
        src = self.m.generate([("dir/Sans-Regular.sub.ttf", bytes(range(30))), ("B.ttf", b"\x01\x02")])
        self.assertIn("namespace embedded_fonts {", src)
        self.assertIn("static const unsigned char kDataSansRegular[30] = {", src)
        self.assertIn("0x00,0x01,0x02", src)
        self.assertIn("        case 1: return kDataB;", src)
        self.assertIn("size_t face_size(int face) {", src)
        self.assertIn("        case 0: return 30;", src)
        self.assertIn("        case 1: return 2;", src)
        self.assertNotIn("count", src)
        self.assertTrue(src.rstrip().endswith("} // namespace embedded_fonts"))
        # 30 bytes = one row of 24 plus one of 6
        self.assertEqual(src.count("0x00,"), 1)

    def test_options(self):
        src = self.m.generate([("A.ttf", b"\x07")], namespace="app::fonts", include="fonts.h",
                              data_fn="extra_data", size_fn="extra_size", count_fn="extra_count", banner="X")
        self.assertTrue(src.startswith("// X\n#include <cstddef>\n#include \"fonts.h\"\n"))
        self.assertIn("namespace app { namespace fonts {", src)
        self.assertIn("const unsigned char* extra_data(int face)", src)
        self.assertIn("int extra_count() { return 1; }", src)
        self.assertTrue(src.rstrip().endswith("}} // namespace app::fonts"))

    def test_duplicate_identifier_rejected(self):
        with self.assertRaises(ValueError):
            self.m.generate([("a/F.ttf", b"1"), ("b/F.sub.ttf", b"2")])


if __name__ == "__main__":
    unittest.main()
