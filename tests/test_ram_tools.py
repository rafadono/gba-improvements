#!/usr/bin/env python3
"""Tests of tools/ram_snapshot.py and tools/ram_diff.py against a fake observe-mode TCP server that
speaks the gbarecomp protocol (one JSON object per line: pause, continue, frame, read_iwram, read_ewram,
read_vram, read_oam, read_pal, read_io, ppu_state, quit). Standard library only; run with
`python -I tests/test_ram_tools.py`."""
import contextlib
import importlib.util
import io
import json
import os
import socket
import socketserver
import tempfile
import threading
import unittest

TOOLS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools")


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(TOOLS, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def quiet(fn, *a, **k):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = fn(*a, **k)
    return rc, out.getvalue(), err.getvalue()


BASES = {"iwram": 0x03000000, "ewram": 0x02000000, "vram": 0x06000000, "oam": 0x07000000,
         "pal": 0x05000000, "io": 0x04000000}
SIZES = {"iwram": 0x8000, "ewram": 0x40000, "vram": 0x18000, "oam": 0x400, "pal": 0x400, "io": 0x400}


class FakeGame:
    """Memory and state behind the fake server."""

    def __init__(self):
        self.mem = {r: bytearray((i * 7 + len(r)) & 0xFF for i in range(n)) for r, n in SIZES.items()}
        self.frame = 1000
        self.paused = False
        self.advance_when_running = 0  # frames added by every "frame" query while running
        self.advance_every = 1         # ... but only on every Nth query
        self.ignore_pause = False      # windowed observe mode: pause is accepted but the game keeps running
        self.queries = 0
        self.log = []
        self.max_len = 0


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        game = self.server.game
        for line in self.rfile:
            line = line.strip()
            if not line:
                continue
            try:
                req = json.loads(line)
            except ValueError:
                self.reply({"ok": False, "error": "bad json"})
                continue
            cmd = req.get("cmd")
            game.log.append(cmd)
            if cmd == "pause":
                game.paused = not game.ignore_pause
                self.reply({"ok": True, "run": "windowed" if game.ignore_pause else "paused",
                            "parked": not game.ignore_pause})
            elif cmd == "continue":
                game.paused = False
                self.reply({"ok": True, "run": "running"})
            elif cmd == "frame":
                game.queries += 1
                if not game.paused and game.queries % game.advance_every == 0:
                    game.frame += game.advance_when_running
                self.reply({"ok": True, "frame": game.frame})
            elif cmd == "ppu_state":
                self.reply({"ok": True, "frame": game.frame, "dispcnt": 0x1F00, "bg0cnt": 0x4405})
            elif cmd and cmd.startswith("read_") and cmd[5:] in SIZES:
                name = cmd[5:]
                addr, ln = req.get("addr"), req.get("len")
                if addr is None or ln is None:
                    self.reply({"ok": False, "error": "missing addr/len"})
                    continue
                off = addr - BASES[name] if addr >= BASES[name] else addr
                if off < 0 or off + ln > SIZES[name]:
                    self.reply({"ok": False, "error": "out of range"})
                    continue
                game.max_len = max(game.max_len, ln)
                self.reply({"ok": True, "base": BASES[name] + off, "len": ln,
                            "data": bytes(game.mem[name][off:off + ln]).hex()})
            elif cmd == "quit":
                self.reply({"ok": True, "bye": True})
                return
            else:
                self.reply({"ok": False, "error": "unknown command"})

    def reply(self, obj):
        self.wfile.write((json.dumps(obj) + "\n").encode())
        self.wfile.flush()


class FakeServer:
    def __enter__(self):
        self.srv = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Handler)
        self.srv.daemon_threads = True
        self.srv.game = FakeGame()
        self.thread = threading.Thread(target=self.srv.serve_forever, daemon=True)
        self.thread.start()
        self.game = self.srv.game
        self.port = self.srv.server_address[1]
        return self

    def __exit__(self, *a):
        self.srv.shutdown()
        self.srv.server_close()


def make_snap(snapshot_mod, iwram=None, ewram=None, label="", frame=0):
    regions = {}
    if iwram is not None:
        regions["iwram"] = bytes(iwram)
    if ewram is not None:
        regions["ewram"] = bytes(ewram)
    return {"meta": {"label": label, "frame": frame}, "regions": regions}


class SnapshotTool(unittest.TestCase):
    def setUp(self):
        self.s = load("ram_snapshot")
        self.tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.tmp.cleanup()

    def test_take_and_roundtrip(self):
        with FakeServer() as fake:
            fake.game.mem["iwram"][0x1234] = 1
            c = self.s.TcpClient("127.0.0.1", fake.port)
            snap = self.s.take_snapshot(c, ("iwram", "ewram", "oam"), label="map", with_ppu=True)
            c.close()
            self.assertEqual(snap["meta"]["frame"], 1000)
            self.assertEqual(snap["meta"]["label"], "map")
            self.assertTrue(snap["meta"]["consistent"])
            self.assertEqual(snap["meta"]["ppu"]["dispcnt"], 0x1F00)
            self.assertEqual(snap["regions"]["iwram"], bytes(fake.game.mem["iwram"]))
            self.assertEqual(len(snap["regions"]["ewram"]), 0x40000)
            self.assertEqual(snap["regions"]["iwram"][0x1234], 1)
            self.assertLessEqual(fake.game.max_len, self.s.CHUNK)     # big regions are read in pieces
            self.assertFalse(fake.game.paused)                       # resumed
            self.assertEqual(fake.game.log[0], "pause")
            self.assertEqual(fake.game.log[-1], "continue")
            path = os.path.join(self.tmp.name, "a.ramsnap")
            self.s.save_snapshot(path, snap)
            back = self.s.load_snapshot(path)
            self.assertEqual(back["regions"], snap["regions"])
            self.assertEqual(back["bases"]["iwram"], 0x03000000)
            self.assertEqual(back["bases"]["oam"], 0x07000000)
            self.assertEqual(back["meta"]["frame"], 1000)
            self.assertEqual(back["meta"]["label"], "map")

    def test_wait_for_frame_and_keep_paused(self):
        with FakeServer() as fake:
            fake.game.advance_when_running = 40
            c = self.s.TcpClient("127.0.0.1", fake.port)
            snap = self.s.take_snapshot(c, ("oam",), wait_frame=1100, resume=False, poll=0)
            c.close()
            self.assertGreaterEqual(snap["meta"]["frame"], 1100)
            self.assertTrue(fake.game.paused)

    def test_no_pause_is_recorded(self):
        with FakeServer() as fake:
            fake.game.advance_when_running = 3
            c = self.s.TcpClient("127.0.0.1", fake.port)
            snap = self.s.take_snapshot(c, ("pal",), pause=False)
            c.close()
            self.assertNotIn("pause", fake.game.log)
            self.assertFalse(snap["meta"]["pause_requested"])
            self.assertFalse(snap["meta"]["stopped"])
            self.assertFalse(snap["meta"]["consistent"])      # the frame advanced while reading

    def test_pause_without_effect_retries_until_one_frame(self):
        # windowed observe mode: the game keeps running; the frame moves on every 3rd query only
        with FakeServer() as fake:
            fake.game.ignore_pause = True
            fake.game.advance_when_running = 1
            fake.game.advance_every = 3
            c = self.s.TcpClient("127.0.0.1", fake.port)
            snap = self.s.take_snapshot(c, ("oam",), attempts=6)
            c.close()
            self.assertFalse(snap["meta"]["stopped"])
            self.assertTrue(snap["meta"]["consistent"])
            self.assertEqual(snap["meta"]["frame"], snap["meta"]["frame_after"])
            self.assertEqual(fake.game.log[-1], "continue")
        # the frame moves on every query: attempts are used up and the result says so
        with FakeServer() as fake:
            fake.game.ignore_pause = True
            fake.game.advance_when_running = 1
            c = self.s.TcpClient("127.0.0.1", fake.port)
            snap = self.s.take_snapshot(c, ("oam",), attempts=3)
            c.close()
            self.assertFalse(snap["meta"]["stopped"])
            self.assertFalse(snap["meta"]["consistent"])
            self.assertEqual(fake.game.log.count("read_oam"), 3)
            rc, text, err = quiet(self.s.main, ["--port", str(fake.port), "-o", os.path.join(self.tmp.name, "w.ramsnap"),
                                                "--regions", "oam", "--connect-retries", "1", "--attempts", "2"])
            self.assertEqual(rc, 0, err)
            self.assertIn("not stopped by pause", text)

    def test_errors(self):
        with FakeServer() as fake:
            c = self.s.TcpClient("127.0.0.1", fake.port)
            with self.assertRaises(self.s.TcpError):
                c.rpc("read_iwram", addr=0x03000000, len=0x9000)      # out of range
            with self.assertRaises(self.s.TcpError):
                c.rpc("no_such_command")
            with self.assertRaises(ValueError):
                self.s.take_snapshot(c, ("nope",))
            c.close()
        # nobody listening
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
        sock.close()
        with self.assertRaises(self.s.TcpError) as cm:
            self.s.TcpClient("127.0.0.1", port, timeout=1, retries=1, retry_delay=0)
        self.assertIn("GBARECOMP_TCP_OBSERVE", str(cm.exception))

    def test_bad_files(self):
        p = os.path.join(self.tmp.name, "bad")
        with open(p, "wb") as f:
            f.write(b"not json\nxxxx")
        with self.assertRaises(ValueError):
            self.s.load_snapshot(p)
        good = make_snap(self.s, iwram=[1, 2, 3, 4])
        gp = os.path.join(self.tmp.name, "good")
        self.s.save_snapshot(gp, good)
        with open(gp, "rb") as f:
            raw = f.read()
        with open(gp, "wb") as f:
            f.write(raw[:-2])                                     # truncated data
        with self.assertRaises(ValueError):
            self.s.load_snapshot(gp)

    def test_cli(self):
        with FakeServer() as fake:
            out = os.path.join(self.tmp.name, "cli.ramsnap")
            rc, text, err = quiet(self.s.main, ["--port", str(fake.port), "-o", out, "--regions", "iwram,io",
                                                "--label", "menu", "--ppu", "--connect-retries", "1"])
            self.assertEqual(rc, 0, err)
            self.assertIn("frame 1000", text)
            snap = self.s.load_snapshot(out)
            self.assertEqual(sorted(snap["regions"]), ["io", "iwram"])
            self.assertEqual(snap["meta"]["label"], "menu")
            rc, _, err = quiet(self.s.main, ["--port", str(fake.port), "-o", out, "--regions", "bogus"])
            self.assertEqual(rc, 2)
            self.assertIn("bogus", err)


class DiffTool(unittest.TestCase):
    def setUp(self):
        self.s = load("ram_snapshot")
        self.d = load("ram_diff")
        self.tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.tmp.cleanup()

    def snap(self, name, iwram=None, ewram=None, frame=0):
        base = {"label": name, "frame": frame}
        regions = {}
        if iwram is not None:
            regions["iwram"] = bytes(iwram)
        if ewram is not None:
            regions["ewram"] = bytes(ewram)
        path = os.path.join(self.tmp.name, name + ".ramsnap")
        self.s.save_snapshot(path, {"meta": base, "regions": regions})
        return path

    def test_changes_merge_and_context(self):
        a = bytearray(256)
        b = bytearray(a)
        b[0x10] = 1
        b[0x14] = 2          # 3 bytes after the first: merged with gap 8
        b[0x40] = 3          # far away: its own range
        pa, pb = self.snap("a", a), self.snap("b", b)
        snaps = [self.d.load_snapshot(pa), self.d.load_snapshot(pb)]
        res = self.d.find_changes(snaps, ["iwram"], {}, gap=8)
        r = res["iwram"]
        self.assertEqual([(x["start"], x["end"], x["changed"]) for x in r],
                         [(0x03000010, 0x03000015, 2), (0x03000040, 0x03000041, 1)])
        self.assertEqual(r[0]["values"][1], bytes([1, 0, 0, 0, 2]))
        res = self.d.find_changes(snaps, ["iwram"], {}, gap=1)
        self.assertEqual(len(res["iwram"]), 3)
        text = self.d.format_changes(res, snaps, 2, [s["regions"] for s in snaps])
        self.assertIn("0x03000010..0x03000010", text)
        self.assertIn("(00 00) 01 (00 00)", text)                  # two bytes of context each side
        res = self.d.find_changes(snaps, ["iwram"], {}, gap=8, min_size=2)
        self.assertEqual(len(res["iwram"]), 1)

    def test_changes_three_snapshots_and_cli_json(self):
        a, b, c = bytearray(64), bytearray(64), bytearray(64)
        b[5] = 9
        c[5] = 9
        c[6] = 1
        paths = [self.snap("a", a), self.snap("b", b), self.snap("c", c)]
        rc, out, err = quiet(self.d.main, ["changes", *paths, "--json", "--gap", "0"])
        self.assertEqual(rc, 0, err)
        data = json.loads(out)
        rng = data["regions"]["iwram"]
        self.assertEqual([(x["start"], x["end"]) for x in rng], [(0x03000005, 0x03000007)])
        self.assertEqual(rng[0]["values"], ["0000", "0900", "0901"])
        self.assertEqual(len(data["snapshots"]), 3)

    def test_changes_noise_and_ignore(self):
        a = bytearray(64)
        a2 = bytearray(a)
        a2[3] = 1                        # a counter: differs between two snapshots of the same context
        b = bytearray(a)
        b[3] = 7                         # the counter also differs in the other scene
        b[20] = 5                        # the real change
        b[30] = 6                        # a change inside an ignored range
        pa, pa2, pb = self.snap("a", a), self.snap("a2", a2), self.snap("b", b)
        rc, out, err = quiet(self.d.main, ["changes", pa, pb, "--json", "--gap", "0"])
        self.assertEqual(rc, 0, err)
        self.assertEqual(len(json.loads(out)["regions"]["iwram"]), 3)
        rc, out, err = quiet(self.d.main, ["changes", pa, pb, "--json", "--gap", "0", "--noise", pa, pa2,
                                           "--ignore", "0x0300001E-0x0300001F"])
        self.assertEqual(rc, 0, err)
        rng = json.loads(out)["regions"]["iwram"]
        self.assertEqual([x["start"] for x in rng], [0x03000014])

    def test_changes_regions(self):
        pa = self.snap("a", bytearray(16), bytearray(16))
        e = bytearray(16)
        e[2] = 1
        pb = self.snap("b", bytearray(16), e)
        rc, out, _ = quiet(self.d.main, ["changes", pa, pb, "--json"])
        data = json.loads(out)["regions"]
        self.assertEqual(data["iwram"], [])
        self.assertEqual(data["ewram"][0]["start"], 0x02000002)
        rc, out, _ = quiet(self.d.main, ["changes", pa, pb, "--json", "--region", "iwram"])
        self.assertEqual(list(json.loads(out)["regions"]), ["iwram"])
        # a region that is missing in one snapshot is not compared
        pc = self.snap("c", bytearray(16))
        rc, out, _ = quiet(self.d.main, ["changes", pa, pc, "--json"])
        self.assertEqual(list(json.loads(out)["regions"]), ["iwram"])

    def make_groups(self):
        """Two contexts. 0x10 is a boolean flag (0 / 1), 0x20..0x23 a word (0x1234 vs 0x00FF), 0x30 a counter that
        changes everywhere, 0x40 changes inside one group only, 0x50 is equal in both."""
        def mk(flag, word, counter, other):
            m = bytearray(0x100)
            m[0x10] = flag
            m[0x20:0x24] = word.to_bytes(4, "little")
            m[0x30] = counter
            m[0x40] = other
            m[0x50] = 77
            return m
        menu = [mk(1, 0x1234, 5, 8), mk(1, 0x1234, 9, 8)]
        mapg = [mk(0, 0x00FF, 40, 1), mk(0, 0x00FF, 41, 2)]
        paths = {
            "menu": [self.snap("m1", menu[0], frame=10), self.snap("m2", menu[1], frame=20)],
            "map": [self.snap("p1", mapg[0], frame=30), self.snap("p2", mapg[1], frame=40)],
        }
        return paths

    def groups_loaded(self, paths):
        return [(n, [self.d.load_snapshot(p) for p in ps]) for n, ps in paths.items()]

    def test_flags_basic(self):
        groups = self.groups_loaded(self.make_groups())
        res = self.d.find_flags(groups, ["iwram"], {})["iwram"]
        found = {e["address"]: e for e in res}
        self.assertIn(0x03000010, found)
        self.assertEqual(found[0x03000010]["values"], {"menu": 1, "map": 0})
        self.assertEqual(found[0x03000010]["size"], 1)
        # 0x1234 vs 0x00FF: byte 0 differs (0x34 vs 0xFF), byte 1 differs (0x12 vs 0x00): merged into one u16
        self.assertEqual(found[0x03000020]["size"], 2)
        self.assertEqual(found[0x03000020]["values"], {"menu": 0x1234, "map": 0x00FF})
        self.assertNotIn(0x03000030, found)            # counter changes inside a group
        self.assertNotIn(0x03000040, found)            # changes inside one group
        self.assertNotIn(0x03000050, found)            # equal in both groups
        self.assertNotIn(0x03000021, found)            # merged into the u16 above
        # only booleans
        res = self.d.find_flags(groups, ["iwram"], {}, max_value=1)["iwram"]
        self.assertEqual([e["address"] for e in res], [0x03000010])
        # fixed units
        res = self.d.find_flags(groups, ["iwram"], {}, unit=2)["iwram"]
        self.assertIn(0x03000020, [e["address"] for e in res])
        self.assertIn(0x03000010, [e["address"] for e in res])            # u16 at 0x10: 0x0001 vs 0x0000
        res = self.d.find_flags(groups, ["iwram"], {}, unit=4)["iwram"]
        self.assertEqual({e["address"]: e["values"]["menu"] for e in res if e["address"] == 0x03000020},
                         {0x03000020: 0x1234})

    def test_flags_distinct_three_groups(self):
        def mk(a, b):
            m = bytearray(16)
            m[1] = a
            m[2] = b
            return m
        p = {"a": [self.snap("a1", mk(1, 1)), self.snap("a2", mk(1, 1))],
             "b": [self.snap("b1", mk(2, 1))],
             "c": [self.snap("c1", mk(3, 5))]}
        groups = self.groups_loaded(p)
        res = {e["address"] for e in self.d.find_flags(groups, ["iwram"], {}, unit=1)["iwram"]}
        self.assertEqual(res, {0x03000001, 0x03000002})
        res = {e["address"] for e in self.d.find_flags(groups, ["iwram"], {}, unit=1, distinct=True)["iwram"]}
        self.assertEqual(res, {0x03000001})            # byte 2 is equal in a and b

    def test_flags_cli_noise_ignore_json(self):
        paths = self.make_groups()
        args = ["flags"]
        for name, ps in paths.items():
            args += ["--group", "%s=%s" % (name, ",".join(ps))]
        rc, out, err = quiet(self.d.main, args + ["--json", "--max-value", "1"])
        self.assertEqual(rc, 0, err)
        data = json.loads(out)
        self.assertEqual([e["address"] for e in data["regions"]["iwram"]], [0x03000010])
        self.assertTrue(data["regions"]["iwram"][0]["boolean"])
        self.assertEqual(sorted(data["groups"]), ["map", "menu"])
        # ignoring the address removes it
        rc, out, _ = quiet(self.d.main, args + ["--json", "--max-value", "1", "--ignore", "0x03000010"])
        self.assertEqual(json.loads(out)["regions"]["iwram"], [])
        # text output mentions the address and the group values
        rc, out, _ = quiet(self.d.main, args + ["--max-value", "1"])
        self.assertIn("0x03000010", out)
        self.assertIn("menu=0x01", out)
        self.assertIn("(boolean)", out)
        # --noise: a flag that only looks constant because the two snapshots of its group were taken close together
        mk = lambda v: bytearray([0, 0, v, 0])
        g1 = [self.snap("x1", mk(1)), self.snap("x2", mk(1))]
        g2 = [self.snap("y1", mk(2)), self.snap("y2", mk(2))]
        extra_a, extra_b = self.snap("n1", mk(0)), self.snap("n2", mk(1))     # same context, byte 2 is a counter
        a = ["flags", "--group", "x=%s" % ",".join(g1), "--group", "y=%s" % ",".join(g2), "--json"]
        rc, out, _ = quiet(self.d.main, a)
        self.assertEqual([e["address"] for e in json.loads(out)["regions"]["iwram"]], [0x03000002])
        rc, out, _ = quiet(self.d.main, a + ["--noise", extra_a, extra_b])
        self.assertEqual(json.loads(out)["regions"]["iwram"], [])

    def test_errors(self):
        pa = self.snap("a", bytearray(8))
        rc, _, err = quiet(self.d.main, ["flags", "--group", "only=%s" % pa])
        self.assertEqual(rc, 2)
        self.assertIn("two", err)
        rc, _, err = quiet(self.d.main, ["flags", "--group", "bad", "--group", "x=%s" % pa])
        self.assertEqual(rc, 2)
        rc, _, err = quiet(self.d.main, ["changes", pa, os.path.join(self.tmp.name, "missing.ramsnap")])
        self.assertEqual(rc, 2)
        pb = self.snap("b", None, bytearray(8))
        rc, _, err = quiet(self.d.main, ["changes", pa, pb])
        self.assertEqual(rc, 2)
        self.assertIn("no region", err)
        with self.assertRaises(SystemExit):
            quiet(self.d.main, ["changes", pa])

    def test_end_to_end_with_fake_server(self):
        """Two scenes taken through the TCP protocol, then the flag finder over them."""
        paths = {"menu": [], "map": []}
        for scene, flag in (("menu", 1), ("map", 0)):
            for k in range(2):
                with FakeServer() as fake:
                    fake.game.mem["iwram"][0x1234] = flag
                    fake.game.mem["iwram"][0x0100] = 10 * k + (3 if scene == "map" else 1)    # a counter
                    fake.game.frame = 100 * k
                    out = os.path.join(self.tmp.name, "%s%d.ramsnap" % (scene, k))
                    rc, _, err = quiet(self.s.main, ["--port", str(fake.port), "-o", out, "--label", scene,
                                                     "--connect-retries", "1"])
                    self.assertEqual(rc, 0, err)
                    paths[scene].append(out)
        args = ["flags", "--region", "iwram", "--json", "--max-value", "1"]
        for n, ps in paths.items():
            args += ["--group", "%s=%s" % (n, ",".join(ps))]
        rc, out, err = quiet(self.d.main, args)
        self.assertEqual(rc, 0, err)
        self.assertEqual([e["address"] for e in json.loads(out)["regions"]["iwram"]], [0x03001234])


if __name__ == "__main__":
    unittest.main()
