#!/usr/bin/env python3
"""Contact-sheet / crop-zoom tool for the capture harness (stdlib only).

  montage.py OUT.png [--cols N] [--crop X,Y,W,H] [--zoom K] [--gap PX] IMG1.png IMG2.png ...

Reads 8-bit RGB PNGs (the harness writes filter-0 stored PNGs; any PNG filter is accepted),
optionally crops each image (coordinates are in the image's own pixels, or fractions 0..1 of
width/height when written like 0.0,0.55,1.0,0.45), zooms with nearest-neighbour, and tiles them
into a grid with a small index number in each tile's corner. The index -> file legend is printed.
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, w = 8, b"", 0
    bpp = 3
    while pos < len(data):
        n, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype in (2, 6), "only 8-bit RGB/RGBA supported"
            bpp = 3 if ctype == 2 else 4
        elif tag == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    p = 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append(bytes(line))
        prev = line
    if bpp == 4:
        rows = [bytes(v for i in range(0, len(r), 4) for v in r[i:i + 3]) for r in rows]
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


DIGITS = {
    "0": ["111", "101", "101", "101", "111"], "1": ["010", "110", "010", "010", "111"],
    "2": ["111", "001", "111", "100", "111"], "3": ["111", "001", "111", "001", "111"],
    "4": ["101", "101", "111", "001", "001"], "5": ["111", "100", "111", "001", "111"],
    "6": ["111", "100", "111", "101", "111"], "7": ["111", "001", "010", "010", "010"],
    "8": ["111", "101", "111", "101", "111"], "9": ["111", "101", "111", "001", "111"],
}


def stamp(rows, w, text, x0=2, y0=2, k=3):
    rows = [bytearray(r) for r in rows]
    cx = x0
    for ch in text:
        g = DIGITS[ch]
        for gy in range(5):
            for gx in range(3):
                if g[gy][gx] == "1":
                    for dy in range(k):
                        for dx in range(k):
                            y, x = y0 + gy * k + dy, cx + gx * k + dx
                            if y < len(rows) and x < w:
                                rows[y][x * 3:x * 3 + 3] = b"\xff\x00\xff"
        cx += 4 * k
    return [bytes(r) for r in rows]


def parse_crop(s, w, h):
    v = [float(t) for t in s.split(",")]
    if all(0 <= t <= 1 for t in v) and any(t < 1 for t in v):
        return int(v[0] * w), int(v[1] * h), max(1, int(v[2] * w)), max(1, int(v[3] * h))
    return int(v[0]), int(v[1]), int(v[2]), int(v[3])


def main(argv):
    cols, crop, zoom, gap, files, out = 2, None, 1, 6, [], None
    it = iter(argv)
    for a in it:
        if a == "--cols":
            cols = int(next(it))
        elif a == "--crop":
            crop = next(it)
        elif a == "--zoom":
            zoom = int(next(it))
        elif a == "--gap":
            gap = int(next(it))
        elif out is None:
            out = a
        else:
            files.append(a)
    tiles = []
    for i, f in enumerate(files):
        w, h, rows = read_png(f)
        if crop:
            x, y, cw, ch = parse_crop(crop, w, h)
            x, y = min(x, w - 1), min(y, h - 1)
            cw, ch = min(cw, w - x), min(ch, h - y)
            rows = [r[x * 3:(x + cw) * 3] for r in rows[y:y + ch]]
            w, h = cw, ch
        if zoom > 1:
            rows = [b"".join(r[j:j + 3] * zoom for j in range(0, len(r), 3)) for r in rows for _ in range(zoom)]
            w, h = w * zoom, h * zoom
        rows = stamp(rows, w, str(i + 1))
        tiles.append((w, h, rows))
        print("%d: %s (%dx%d)" % (i + 1, f, w, h))
    n = len(tiles)
    nrows = (n + cols - 1) // cols
    colw = [max(t[0] for t in tiles[c::cols]) for c in range(cols) if tiles[c::cols]]
    rowh = [max(t[1] for t in tiles[r * cols:(r + 1) * cols]) for r in range(nrows)]
    W = sum(colw) + gap * (len(colw) + 1)
    H = sum(rowh) + gap * (nrows + 1)
    canvas = [bytearray(b"\x30\x30\x30" * W) for _ in range(H)]
    for i, (w, h, rows) in enumerate(tiles):
        c, r = i % cols, i // cols
        x = gap + sum(colw[:c]) + gap * c
        y = gap + sum(rowh[:r]) + gap * r
        for j, row in enumerate(rows):
            canvas[y + j][x * 3:(x + w) * 3] = row
    write_png(out, W, H, [bytes(r) for r in canvas])
    print("wrote %s (%dx%d)" % (out, W, H))


if __name__ == "__main__":
    main(sys.argv[1:])
