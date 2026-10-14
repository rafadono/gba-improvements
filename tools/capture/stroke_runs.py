#!/usr/bin/env python3
"""Histogram of horizontal run lengths of dark text ink in a crop of a capture (stdlib only).

  stroke_runs.py IMG.png X,Y,W,H [threshold=110]

With an integer output scale k every 1-source-pixel stroke is exactly k px wide (runs are multiples of k);
at a non-integer scale the same stroke is sometimes floor(k) and sometimes ceil(k) px wide.
"""
import sys, collections
sys.path.insert(0, __file__.rsplit("/", 1)[0] if "/" in __file__ else ".")
from montage import read_png

path, crop = sys.argv[1], [int(v) for v in sys.argv[2].split(",")]
thr = int(sys.argv[3]) if len(sys.argv) > 3 else 110
w, h, rows = read_png(path)
x0, y0, cw, ch = crop
hist = collections.Counter()
for y in range(y0, min(h, y0 + ch)):
    row = rows[y]
    run = 0
    for x in range(x0, min(w, x0 + cw)):
        r, g, b = row[3 * x], row[3 * x + 1], row[3 * x + 2]
        if max(r, g, b) < thr:
            run += 1
        else:
            if run:
                hist[run] += 1
            run = 0
    if run:
        hist[run] += 1
tot = sum(hist.values())
print("%s: %d runs; run-length histogram (len:count) %s" % (path.split("/")[-1], tot, sorted(hist.items())))
