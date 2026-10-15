#!/usr/bin/env python3
"""Dump guest memory of a running recompiled game through gbarecomp's observe-mode TCP port.

Start the game with the observe port enabled (environment variable GBARECOMP_TCP_OBSERVE=<port>, or
--tcp-observe <port>), get it to the scene you want to look at, then run this script. It asks the
game to pause (the windowed observe mode accepts the command but keeps running; the metadata records whether
the game really stopped), reads IWRAM and EWRAM (optionally VRAM, OAM, palette, IO and the PPU state), writes them to one
file with metadata (frame, regions, label) and lets the game continue. Snapshots of different scenes
are compared with ram_diff.py to find mode flags.

Protocol (gbarecomp TCP.md): one JSON object per line, one JSON answer per line. Commands used:
  {"cmd":"pause"} / {"cmd":"continue"}      free-run control
  {"cmd":"frame"}                           -> {"ok":true,"frame":<PPU frame>}
  {"cmd":"read_iwram|read_ewram|read_vram|read_oam|read_pal|read_io","addr":A,"len":N}
                                            -> {"ok":true,"base":A,"len":N,"data":"<hex>"}
  {"cmd":"ppu_state"}                       -> {"ok":true,"frame":...,"dispcnt":...,"bg0cnt":...,...}

File format ("ramsnap1"): the first line is a JSON header terminated by a line feed, followed by the
raw bytes of every region back to back, in the order of header["regions"] (each entry has "name",
"base", "size" and "offset" relative to the start of the data). Use load_snapshot() / save_snapshot()
from Python.

Usage:
  ram_snapshot.py --port 19842 -o menu.ramsnap --label menu
  ram_snapshot.py --port 19842 -o map.ramsnap --regions iwram,ewram,oam --frame 5000
"""
import argparse
import json
import socket
import sys
import time

FORMAT = "ramsnap1"

# name -> (base address, size, TCP command). Sizes are the regions of the GBA memory map.
REGIONS = {
    "iwram": (0x03000000, 0x8000, "read_iwram"),
    "ewram": (0x02000000, 0x40000, "read_ewram"),
    "vram": (0x06000000, 0x18000, "read_vram"),
    "oam": (0x07000000, 0x400, "read_oam"),
    "pal": (0x05000000, 0x400, "read_pal"),
    "io": (0x04000000, 0x400, "read_io"),
}
DEFAULT_REGIONS = ("iwram", "ewram")
CHUNK = 0x8000  # bytes per read command (keeps every answer line below 70 KB)


class TcpError(RuntimeError):
    pass


class TcpClient:
    """Line based JSON client of the observe-mode server."""

    def __init__(self, host="127.0.0.1", port=19842, timeout=30.0, retries=1, retry_delay=0.5):
        last = None
        self.sock = None
        for _ in range(max(1, retries)):
            try:
                self.sock = socket.create_connection((host, port), timeout=timeout)
                break
            except OSError as e:  # connection refused while the game boots
                last = e
                time.sleep(retry_delay)
        if self.sock is None:
            raise TcpError("cannot connect to %s:%d (%s); start the game with GBARECOMP_TCP_OBSERVE=%d"
                           % (host, port, last, port))
        self.file = self.sock.makefile("rwb")

    def rpc(self, cmd, **fields):
        req = dict(fields)
        req["cmd"] = cmd
        self.file.write((json.dumps(req) + "\n").encode("ascii"))
        self.file.flush()
        line = self.file.readline()
        if not line:
            raise TcpError("connection closed while waiting for the answer to %r" % cmd)
        ans = json.loads(line.decode("utf-8", "replace"))
        if not ans.get("ok", False):
            raise TcpError("%s failed: %s" % (cmd, ans.get("error", ans)))
        return ans

    def frame(self):
        return int(self.rpc("frame")["frame"])

    def close(self):
        try:
            self.file.close()
        finally:
            self.sock.close()


def read_region(client, name, chunk=CHUNK):
    """Read a whole region (several commands for the big ones). Returns bytes."""
    base, size, cmd = REGIONS[name]
    out = bytearray()
    off = 0
    while off < size:
        n = min(chunk, size - off)
        ans = client.rpc(cmd, addr=base + off, len=n)
        data = bytes.fromhex(ans["data"])
        if len(data) != n:
            raise TcpError("%s: asked for %d bytes at 0x%08X, got %d" % (cmd, n, base + off, len(data)))
        out += data
        off += n
    return bytes(out)


def wait_until_stopped(client, settle=0.05, timeout=0.6, stable_polls=3):
    """After "pause": poll the frame counter until it has stayed the same for `stable_polls` consecutive polls.
    Returns True when the game is stopped. In the windowed observe mode the command is accepted but the game
    keeps running (the answer shows "run":"windowed","parked":false), so this returns False after `timeout`
    seconds."""
    t0 = time.time()
    last = client.frame()
    same = 0
    while time.time() - t0 <= timeout:
        time.sleep(settle)
        now = client.frame()
        same = same + 1 if now == last else 0
        last = now
        if same >= stable_polls:
            return True
    return False


def take_snapshot(client, regions=DEFAULT_REGIONS, label="", with_ppu=False, pause=True,
                  resume=True, wait_frame=None, poll=0.02, wait_timeout=600.0, chunk=CHUNK, attempts=5):
    """Wait for a frame (optional), pause the game, read the regions, resume. Returns a snapshot dict:
    {"meta": {...}, "regions": {name: bytes}}.

    meta["stopped"] says whether the game really stood still. When it did not (windowed observe mode), the
    regions are read while it runs; the read is repeated up to `attempts` times until the frame counter is
    the same before and after it, and meta["consistent"] tells whether that was achieved. A flag that
    holds for seconds is unaffected by a read that spans a few frames."""
    for r in regions:
        if r not in REGIONS:
            raise ValueError("unknown region %r (valid: %s)" % (r, ", ".join(sorted(REGIONS))))
    if wait_frame is not None:
        t0 = time.time()
        while client.frame() < wait_frame:
            if time.time() - t0 > wait_timeout:
                raise TcpError("frame %d not reached within %.0f s" % (wait_frame, wait_timeout))
            time.sleep(poll)
    stopped = False
    try:
        if pause:
            client.rpc("pause")
            stopped = wait_until_stopped(client)
        tries = 1 if stopped else max(1, attempts)
        for _ in range(tries):
            frame = client.frame()
            data = {r: read_region(client, r, chunk) for r in regions}
            ppu = None
            if with_ppu:
                ppu = {k: v for k, v in client.rpc("ppu_state").items() if k != "ok"}
            frame_after = client.frame()
            if frame == frame_after or stopped:
                break
    finally:
        if pause and resume:
            client.rpc("continue")
    meta = {"label": label, "frame": frame, "frame_after": frame_after, "pause_requested": bool(pause),
            "stopped": stopped, "consistent": frame == frame_after,
            "created": time.strftime("%Y-%m-%dT%H:%M:%S")}
    if ppu is not None:
        meta["ppu"] = ppu
    return {"meta": meta, "regions": data}


def save_snapshot(path, snap):
    regions = []
    blob = bytearray()
    for name, data in snap["regions"].items():
        regions.append({"name": name, "base": REGIONS[name][0] if name in REGIONS else snap.get("bases", {}).get(name, 0),
                        "size": len(data), "offset": len(blob)})
        blob += data
    header = {"format": FORMAT, "meta": snap["meta"], "regions": regions}
    with open(path, "wb") as f:
        f.write((json.dumps(header, sort_keys=True) + "\n").encode("ascii"))
        f.write(bytes(blob))


def load_snapshot(path):
    """Returns {"meta": {...}, "regions": {name: bytes}, "bases": {name: base}, "path": path}."""
    with open(path, "rb") as f:
        raw = f.read()
    nl = raw.find(b"\n")
    if nl < 0:
        raise ValueError("%s: not a %s file (no header line)" % (path, FORMAT))
    try:
        header = json.loads(raw[:nl].decode("utf-8"))
    except ValueError:
        raise ValueError("%s: not a %s file (bad header)" % (path, FORMAT))
    if header.get("format") != FORMAT:
        raise ValueError("%s: unsupported format %r" % (path, header.get("format")))
    blob = raw[nl + 1:]
    regions, bases = {}, {}
    for r in header["regions"]:
        chunk = blob[r["offset"]:r["offset"] + r["size"]]
        if len(chunk) != r["size"]:
            raise ValueError("%s: region %s is truncated" % (path, r["name"]))
        regions[r["name"]] = chunk
        bases[r["name"]] = r["base"]
    return {"meta": header.get("meta", {}), "regions": regions, "bases": bases, "path": path}


def parse_regions(text):
    names = [t.strip().lower() for t in text.replace(";", ",").split(",") if t.strip()]
    if not names:
        raise ValueError("empty region list")
    for n in names:
        if n not in REGIONS:
            raise ValueError("unknown region %r (valid: %s)" % (n, ", ".join(sorted(REGIONS))))
    return names


def main(argv=None):
    ap = argparse.ArgumentParser(description="Dump guest memory through the observe-mode TCP port of gbarecomp.")
    ap.add_argument("-o", "--output", required=True, help="snapshot file to write")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19842, help="observe port (GBARECOMP_TCP_OBSERVE); default 19842")
    ap.add_argument("--regions", default=",".join(DEFAULT_REGIONS),
                    help="comma list of: %s (default %s)" % (",".join(REGIONS), ",".join(DEFAULT_REGIONS)))
    ap.add_argument("--label", default="", help="free text stored in the metadata (for example the scene name)")
    ap.add_argument("--frame", type=int, default=None, help="wait until this PPU frame, then snapshot")
    ap.add_argument("--ppu", action="store_true", help="also store the ppu_state answer (DISPCNT, BGxCNT, scrolls...)")
    ap.add_argument("--no-pause", action="store_true",
                    help="do not send the pause command (the regions may belong to different frames; recorded in the metadata)")
    ap.add_argument("--attempts", type=int, default=5,
                    help="reads to try while the game keeps running, until one lies within a single frame (default 5)")
    ap.add_argument("--keep-paused", action="store_true", help="leave the game paused afterwards")
    ap.add_argument("--timeout", type=float, default=30.0, help="socket timeout in seconds")
    ap.add_argument("--connect-retries", type=int, default=20, help="connection attempts, 0.5 s apart (default 20)")
    args = ap.parse_args(argv)
    try:
        regions = parse_regions(args.regions)
        client = TcpClient(args.host, args.port, args.timeout, retries=args.connect_retries)
    except (ValueError, TcpError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 2
    try:
        snap = take_snapshot(client, regions, label=args.label, with_ppu=args.ppu, pause=not args.no_pause,
                             resume=not args.keep_paused, wait_frame=args.frame, attempts=args.attempts)
    except (TcpError, ValueError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    finally:
        client.close()
    save_snapshot(args.output, snap)
    total = sum(len(v) for v in snap["regions"].values())
    note = ""
    if not snap["meta"]["stopped"] and not args.no_pause:
        note = " (the game was not stopped by pause: windowed observe mode; %s)" % (
            "read within one frame" if snap["meta"]["consistent"] else "the read spans frames %d..%d" % (
                snap["meta"]["frame"], snap["meta"]["frame_after"]))
    print("%s: frame %d, %s, %d bytes%s" % (args.output, snap["meta"]["frame"], ",".join(regions), total, note))
    return 0


if __name__ == "__main__":
    sys.exit(main())
