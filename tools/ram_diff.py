#!/usr/bin/env python3
"""Compare memory snapshots taken with ram_snapshot.py.

Two sub-commands:

  changes  A B [C ...]    ranges of bytes that are not equal in every snapshot, merged when close and
                          shown with a little context. Use it to see what a scene change touched.
  flags    --group NAME=file[,file...] --group ...
                          "mode flag finder": locations whose value is the same in every snapshot of a
                          group (one game context, several snapshots taken at different times) and
                          different between the groups. Counters and timers change inside a group and
                          therefore never show up.

Noise filters for both: --noise A B (repeatable) ignores every byte that differs between two snapshots
of the same context; --ignore 0xSTART-0xEND ignores an address range (repeatable);
--region iwram,ewram limits the regions.

Output is text by default and JSON with --json. Addresses are guest addresses (region base + offset).

Examples:
  ram_diff.py changes title.ramsnap map.ramsnap --gap 16 --context 4
  ram_diff.py flags --group menu=m1.ramsnap,m2.ramsnap --group map=p1.ramsnap,p2.ramsnap --max-value 1
  ram_diff.py flags --group a=a1.ramsnap,a2.ramsnap --group b=b1.ramsnap --group c=c1.ramsnap --distinct
"""
import argparse
import importlib.util
import json
import os
import sys


def _load_snapshot_module():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ram_snapshot.py")
    spec = importlib.util.spec_from_file_location("ram_snapshot", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


_snap = _load_snapshot_module()
load_snapshot = _snap.load_snapshot


# --- helpers -----------------------------------------------------------------------------------

def parse_addr(text):
    return int(text, 0)


def parse_ranges(items):
    """['0x03000000-0x030000FF', ...] -> [(start, end_exclusive)]. The end is inclusive in the text."""
    out = []
    for it in items or []:
        if "-" not in it:
            a = parse_addr(it)
            out.append((a, a + 1))
            continue
        lo, hi = it.split("-", 1)
        a, b = parse_addr(lo), parse_addr(hi)
        if b < a:
            raise ValueError("bad range %r (end before start)" % it)
        out.append((a, b + 1))
    return out


def common_regions(snaps, wanted=None):
    """Names of the regions present in every snapshot (same size), in the first snapshot's order."""
    names = []
    for name, data in snaps[0]["regions"].items():
        if wanted and name not in wanted:
            continue
        if all(name in s["regions"] and len(s["regions"][name]) == len(data) and
               s["bases"].get(name) == snaps[0]["bases"].get(name) for s in snaps[1:]):
            names.append(name)
    return names


def build_noise(noise_pairs, regions, snaps_by_path):
    """region -> bytearray mask (1 = ignore). Bytes that differ inside a pair of same-context snapshots."""
    mask = {r: None for r in regions}
    for a, b in noise_pairs:
        sa, sb = snaps_by_path(a), snaps_by_path(b)
        for r in regions:
            if r not in sa["regions"] or r not in sb["regions"]:
                continue
            da, db = sa["regions"][r], sb["regions"][r]
            n = min(len(da), len(db))
            if mask[r] is None:
                mask[r] = bytearray(len(da))
            m = mask[r]
            for i in range(n):
                if da[i] != db[i]:
                    m[i] = 1
    return mask


def apply_ignores(mask, regions, bases, sizes, ranges):
    for r in regions:
        if mask.get(r) is None:
            mask[r] = bytearray(sizes[r])
        base = bases[r]
        for lo, hi in ranges:
            a, b = max(lo, base) - base, min(hi, base + sizes[r]) - base
            for i in range(a, max(a, b)):
                mask[r][i] = 1
    return mask


def hexbytes(data):
    return " ".join("%02X" % b for b in data)


# --- changes -----------------------------------------------------------------------------------

def find_changes(snaps, regions, mask, gap=8, min_size=1):
    """Returns {region: [range dict]}. A range: start/end (guest address, end exclusive), changed (number
    of differing bytes) and values (one byte string per snapshot, the whole merged range)."""
    result = {}
    for r in regions:
        datas = [s["regions"][r] for s in snaps]
        base = snaps[0]["bases"][r]
        m = mask.get(r)
        size = len(datas[0])
        ref = datas[0]
        differing = bytearray(size)
        for d in datas[1:]:
            if d == ref:
                continue
            for i in range(size):
                if d[i] != ref[i]:
                    differing[i] = 1
        if m is not None:
            for i in range(size):
                if m[i]:
                    differing[i] = 0
        ranges = []
        i = 0
        while i < size:
            if not differing[i]:
                i += 1
                continue
            start = i
            last = i
            j = i + 1
            while j < size and j - last - 1 <= gap:
                if differing[j]:
                    last = j
                j += 1
            end = last + 1
            changed = sum(differing[start:end])
            if end - start >= min_size:
                ranges.append({"start": base + start, "end": base + end, "changed": changed,
                               "offset": start, "values": [bytes(d[start:end]) for d in datas]})
            i = end
        result[r] = ranges
    return result


def format_changes(result, snaps, context, regions_data, max_show=16, limit=0):
    lines = []
    labels = [s["meta"].get("label") or os.path.basename(s.get("path", "")) or "#%d" % k for k, s in enumerate(snaps)]
    for k, s in enumerate(snaps):
        lines.append("snapshot %d: %s (frame %s)" % (k, labels[k], s["meta"].get("frame", "?")))
    for r, ranges in result.items():
        total = sum(x["changed"] for x in ranges)
        lines.append("")
        lines.append("== %s: %d range(s), %d byte(s) differ" % (r, len(ranges), total))
        shown = ranges if not limit else ranges[:limit]
        for x in shown:
            n = x["end"] - x["start"]
            lines.append("0x%08X..0x%08X  %d byte(s), %d differ" % (x["start"], x["end"] - 1, n, x["changed"]))
            off = x["offset"]
            for k, v in enumerate(x["values"]):
                data = regions_data[k][r]
                pre = data[max(0, off - context):off]
                post = data[off + n:off + n + context]
                body = v[:max_show]
                text = hexbytes(body) + (" ..." if n > max_show else "")
                lines.append("  [%d] %s%s%s" % (k, ("(" + hexbytes(pre) + ") ") if pre else "", text,
                                                (" (" + hexbytes(post) + ")") if post else ""))
        if limit and len(ranges) > limit:
            lines.append("... %d more range(s)" % (len(ranges) - limit))
    return "\n".join(lines)


def changes_to_json(result, snaps):
    return {"snapshots": [{"label": s["meta"].get("label", ""), "frame": s["meta"].get("frame"),
                           "path": s.get("path", "")} for s in snaps],
            "regions": {r: [{"start": x["start"], "end": x["end"], "changed": x["changed"],
                             "values": [v.hex() for v in x["values"]]} for x in ranges]
                        for r, ranges in result.items()}}


# --- flag finder ---------------------------------------------------------------------------------

def find_flags(groups, regions, mask, unit="auto", distinct=False, max_value=None, nonzero_group=None):
    """groups: [(name, [snapshot, ...])]. For every position, the value must be identical in all the
    snapshots of a group and differ between groups (at least one pair, or every pair with distinct=True).

    unit: 1, 2 or 4 evaluates aligned units of that many bytes as little-endian values; "auto" evaluates
    bytes and merges adjacent flagged bytes into one entry whose width is the run length.
    max_value: keep entries whose value is <= max_value in every group (for example 1 for booleans).
    Returns {region: [entry]}; entry: {"address", "size", "values": {group: int}, "offset"}."""
    result = {}
    step = 1 if unit == "auto" else int(unit)
    for r in regions:
        base = groups[0][1][0]["bases"][r]
        size = len(groups[0][1][0]["regions"][r])
        m = mask.get(r)
        entries = []
        for off in range(0, size - step + 1, step):
            if m is not None and any(m[off:off + step]):
                continue
            vals = []
            ok = True
            for _, snaps in groups:
                chunk = snaps[0]["regions"][r][off:off + step]
                if any(s["regions"][r][off:off + step] != chunk for s in snaps[1:]):
                    ok = False
                    break
                vals.append(int.from_bytes(chunk, "little"))
            if not ok:
                continue
            if distinct:
                if len(set(vals)) != len(vals):
                    continue
            elif len(set(vals)) == 1:
                continue
            if max_value is not None and any(v > max_value for v in vals):
                continue
            entries.append({"offset": off, "address": base + off, "size": step,
                            "values": {g[0]: v for g, v in zip(groups, vals)}})
        if unit == "auto":
            entries = merge_runs(entries)
        result[r] = entries
    return result


def merge_runs(entries):
    """Join adjacent flagged bytes into wider entries (at most 4 bytes, so the value is a word)."""
    out = []
    for e in entries:
        prev = out[-1] if out else None
        if prev and prev["offset"] + prev["size"] == e["offset"] and prev["address"] // 4 == e["address"] // 4:
            # adjacent bytes of the same aligned word: concatenate the values (little endian)
            shift = 8 * prev["size"]
            for g in prev["values"]:
                prev["values"][g] |= e["values"][g] << shift
            prev["size"] += e["size"]
        else:
            out.append({"offset": e["offset"], "address": e["address"], "size": e["size"],
                        "values": dict(e["values"])})
    return out


def format_flags(result, groups, limit=0):
    lines = []
    for name, snaps in groups:
        lines.append("group %s: %s" % (name, ", ".join("%s (frame %s)" % (os.path.basename(s.get("path", "")) or
                                                                       s["meta"].get("label", ""),
                                                                       s["meta"].get("frame", "?")) for s in snaps)))
    for r, entries in result.items():
        lines.append("")
        lines.append("== %s: %d candidate(s)" % (r, len(entries)))
        shown = entries if not limit else entries[:limit]
        for e in shown:
            width = 2 * e["size"]
            vals = "  ".join("%s=0x%0*X" % (g, width, v) for g, v in e["values"].items())
            lines.append("0x%08X  %s  %s%s" % (e["address"], {1: "u8 ", 2: "u16", 3: "3B ", 4: "u32"}[e["size"]], vals,
                                               "  (boolean)" if all(v in (0, 1) for v in e["values"].values()) else ""))
        if limit and len(entries) > limit:
            lines.append("... %d more" % (len(entries) - limit))
    return "\n".join(lines)


def flags_to_json(result, groups):
    return {"groups": {n: [{"label": s["meta"].get("label", ""), "frame": s["meta"].get("frame"),
                            "path": s.get("path", "")} for s in snaps] for n, snaps in groups},
            "regions": {r: [{"address": e["address"], "size": e["size"], "values": e["values"],
                             "boolean": all(v in (0, 1) for v in e["values"].values())} for e in entries]
                        for r, entries in result.items()}}


# --- command line ----------------------------------------------------------------------------------

def _common(ap):
    ap.add_argument("--region", default="", help="comma list of regions to compare (default: all the snapshots share)")
    ap.add_argument("--noise", nargs=2, action="append", metavar=("A", "B"), default=[],
                    help="two snapshots of the SAME context taken at different times: the bytes that differ "
                         "between them (counters, timers) are ignored; repeatable")
    ap.add_argument("--ignore", action="append", default=[], metavar="0xSTART-0xEND",
                    help="ignore a guest address range (end inclusive); repeatable")
    ap.add_argument("--limit", type=int, default=0, help="print at most this many entries per region (0 = all)")
    ap.add_argument("--json", action="store_true", help="JSON output")


def _prepare(snaps, args):
    wanted = [t.strip().lower() for t in args.region.split(",") if t.strip()] or None
    regions = common_regions(snaps, wanted)
    if not regions:
        raise ValueError("the snapshots share no region" + (" among %s" % wanted if wanted else ""))
    cache = {s["path"]: s for s in snaps}

    def get(path):
        if path not in cache:
            cache[path] = load_snapshot(path)
        return cache[path]

    mask = build_noise(args.noise, regions, get)
    sizes = {r: len(snaps[0]["regions"][r]) for r in regions}
    bases = {r: snaps[0]["bases"][r] for r in regions}
    apply_ignores(mask, regions, bases, sizes, parse_ranges(args.ignore))
    return regions, mask


def parse_groups(items):
    groups = []
    for it in items:
        if "=" not in it:
            raise ValueError("bad --group %r (expected NAME=file[,file...])" % it)
        name, files = it.split("=", 1)
        paths = [f for f in files.split(",") if f]
        if not name or not paths:
            raise ValueError("bad --group %r (expected NAME=file[,file...])" % it)
        if any(n == name for n, _ in groups):
            raise ValueError("group %r given twice" % name)
        groups.append((name, paths))
    return groups


def cmd_changes(args):
    snaps = [load_snapshot(p) for p in args.snapshots]
    regions, mask = _prepare(snaps, args)
    result = find_changes(snaps, regions, mask, gap=args.gap, min_size=args.min_size)
    if args.json:
        print(json.dumps(changes_to_json(result, snaps), indent=2))
    else:
        print(format_changes(result, snaps, args.context, [s["regions"] for s in snaps], limit=args.limit))
    return 0


def cmd_flags(args):
    groups = []
    for name, paths in parse_groups(args.group):
        groups.append((name, [load_snapshot(p) for p in paths]))
    if len(groups) < 2:
        raise ValueError("flags needs at least two --group")
    all_snaps = [s for _, ss in groups for s in ss]
    regions, mask = _prepare(all_snaps, args)
    result = find_flags(groups, regions, mask, unit=args.unit, distinct=args.distinct, max_value=args.max_value)
    if args.json:
        print(json.dumps(flags_to_json(result, groups), indent=2))
    else:
        print(format_flags(result, groups, limit=args.limit))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="Compare ram_snapshot.py files.")
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("changes", help="ranges that differ between two or more snapshots")
    c.add_argument("snapshots", nargs="+")
    c.add_argument("--gap", type=int, default=8, help="merge ranges separated by at most this many unchanged bytes (default 8)")
    c.add_argument("--context", type=int, default=4, help="bytes of unchanged context shown around a range (default 4)")
    c.add_argument("--min-size", type=int, default=1, help="drop ranges shorter than this many bytes")
    _common(c)
    f = sub.add_parser("flags", help="flag finder: constant inside every group, different between groups")
    f.add_argument("--group", action="append", required=True, metavar="NAME=file[,file...]",
                   help="a game context and its snapshots; repeat for each context (two or more)")
    f.add_argument("--unit", default="auto", choices=["auto", "1", "2", "4"],
                   help="auto: bytes, adjacent hits merged (default); 1/2/4: aligned values of that width")
    f.add_argument("--distinct", action="store_true", help="with 3+ groups require every group to differ from every other")
    f.add_argument("--max-value", type=lambda t: int(t, 0), default=None,
                   help="keep only values <= N in every group (1 finds boolean flags)")
    _common(f)
    args = ap.parse_args(argv)
    if args.cmd == "changes" and len(args.snapshots) < 2:
        ap.error("changes needs at least two snapshots")
    try:
        return cmd_changes(args) if args.cmd == "changes" else cmd_flags(args)
    except (ValueError, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
