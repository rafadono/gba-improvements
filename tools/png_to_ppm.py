#!/usr/bin/env python3
"""Convert an 8-bit RGB/RGBA non-interlaced PNG (e.g. a capture-harness frame) to a binary PPM (P6).

Usage: png_to_ppm.py in.png out.ppm

Standard library only. Palette, 16-bit, grayscale and interlaced PNGs are rejected.
"""
import struct
import sys
import zlib


def decode_png_rgb(data):
    """Return (width, height, rgb_bytes) for an 8-bit RGB/RGBA non-interlaced PNG (alpha dropped)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, w, h, bpp = 8, b"", 0, 0, 0
    while pos < len(data):
        n, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if tag == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            if not (depth == 8 and ctype in (2, 6) and inter == 0):
                raise ValueError("only 8-bit RGB/RGBA, non-interlaced")
            bpp = 3 if ctype == 2 else 4
        elif tag == b"IDAT":
            idat += body
        pos += 12 + n
    if not bpp:
        raise ValueError("missing IHDR")
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    out = bytearray()
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
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        prev = line
        for x in range(w):
            out += line[x * bpp:x * bpp + 3]
    return w, h, bytes(out)


def png_to_ppm(data):
    w, h, rgb = decode_png_rgb(data)
    return b"P6 %d %d 255\n" % (w, h) + rgb


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 2:
        print("Usage: png_to_ppm.py in.png out.ppm", file=sys.stderr)
        return 2
    with open(argv[0], "rb") as f:
        ppm = png_to_ppm(f.read())
    with open(argv[1], "wb") as f:
        f.write(ppm)
    return 0


if __name__ == "__main__":
    sys.exit(main())
