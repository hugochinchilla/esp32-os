#!/usr/bin/env python3
"""Reference implementation of the picture dithers in core/src/image/convert.cpp,
written separately so the tests can check the C++ against it. Same integer
rules: board levels, thresholds (v - lo) / (hi - lo) > (t + 0.5) / n, error
shares rounded towards zero, errors lost past the picture's edges, errors
kept in 16ths (whole values for the original Floyd-Steinberg).

  dither(rgb_rows, name) -> rows of board colour bytes (RRRGGGBB)
"""
import os
import re

LEVELS3 = [0, 36, 73, 109, 146, 182, 219, 255]
LEVELSB = [0, 73, 182, 255]
NAMES = ["none", "bayer2", "bayer4", "bayer8", "blue-noise", "floyd-steinberg", "floyd-serpentine",
         "atkinson", "jarvis-judice-ninke", "stucki", "sierra-lite"]

KERNELS = {
    "floyd-steinberg": ([(1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1)], 16, False),
    "floyd-serpentine": ([(1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1)], 16, True),
    "atkinson": ([(1, 0, 1), (2, 0, 1), (-1, 1, 1), (0, 1, 1), (1, 1, 1), (0, 2, 1)], 8, False),
    "jarvis-judice-ninke": ([(1, 0, 7), (2, 0, 5), (-2, 1, 3), (-1, 1, 5), (0, 1, 7), (1, 1, 5), (2, 1, 3),
                             (-2, 2, 1), (-1, 2, 3), (0, 2, 5), (1, 2, 3), (2, 2, 1)], 48, False),
    "stucki": ([(1, 0, 8), (2, 0, 4), (-2, 1, 2), (-1, 1, 4), (0, 1, 8), (1, 1, 4), (2, 1, 2),
                (-2, 2, 1), (-1, 2, 2), (0, 2, 4), (1, 2, 2), (2, 2, 1)], 42, False),
    "sierra-lite": ([(1, 0, 2), (-1, 1, 1), (0, 1, 1)], 4, False),
}


def bayer(n, x, y):
    if n == 1:
        return 0
    h = n // 2
    return 4 * bayer(h, x % h, y % h) + [[0, 2], [3, 1]][y // h][x // h]


def blue_noise():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core", "src", "image",
                        "blue_noise.cpp")
    body = open(path).read().split("= {", 1)[1].split("}")[0]
    vals = [int(v) for v in re.findall(r"\d+", body)]
    assert len(vals) == 4096
    return vals


def nearest_level(v, levels):
    best = 0
    for i in range(1, len(levels)):
        if abs(v - levels[i]) < abs(v - levels[best]):
            best = i
    return best


def channel(v, levels, t, n):
    lo = 0
    while lo + 1 < len(levels) and levels[lo + 1] <= v:
        lo += 1
    if levels[lo] == v or lo + 1 == len(levels):
        return lo
    span = levels[lo + 1] - levels[lo]
    return lo + 1 if 2 * n * (v - levels[lo]) > (2 * t + 1) * span else lo


def colour(r, g, b):
    return (r << 5) | (g << 2) | b


def trunc_div(a, b):
    q = abs(a) // b
    return q if a >= 0 else -q


def dither(rows, name):
    """rows: list of rows of (r, g, b). Returns rows of colour bytes."""
    h, w = len(rows), len(rows[0])
    out = [[0] * w for _ in range(h)]
    if name in KERNELS:
        taps, div, serp = KERNELS[name]
        # Errors in 16ths, except the original Floyd-Steinberg (whole values).
        sh = 0 if name == "floyd-steinberg" else 4
        half = (1 << sh) >> 1
        err = {}  # (x, y, c) -> error
        for y in range(h):
            back = serp and y % 2 == 1
            xs = range(w - 1, -1, -1) if back else range(w)
            for x in xs:
                t = [max(0, min(255 << sh, (rows[y][x][c] << sh) + err.get((x, y, c), 0))) for c in range(3)]
                v = [(tc + half) >> sh for tc in t]
                ri, gi, bi = nearest_level(v[0], LEVELS3), nearest_level(v[1], LEVELS3), nearest_level(v[2], LEVELSB)
                out[y][x] = colour(ri, gi, bi)
                got = (LEVELS3[ri], LEVELS3[gi], LEVELSB[bi])
                for dx, dy, wt in taps:
                    tx = x - dx if back else x + dx
                    if tx < -2 or tx >= w + 2:
                        continue
                    for c in range(3):
                        e = t[c] - (got[c] << sh)
                        err[(tx, y + dy, c)] = err.get((tx, y + dy, c), 0) + trunc_div(e * wt, div)
        return out
    mask = blue_noise() if name == "blue-noise" else None
    for y in range(h):
        for x in range(w):
            r, g, b = rows[y][x]
            if name == "none":
                out[y][x] = colour(nearest_level(r, LEVELS3), nearest_level(g, LEVELS3), nearest_level(b, LEVELSB))
                continue
            if name == "blue-noise":
                t, n = mask[(y % 64) * 64 + x % 64], 4096
            else:
                size = {"bayer2": 2, "bayer4": 4, "bayer8": 8}[name]
                t, n = bayer(size, x % size, y % size), size * size
            out[y][x] = colour(channel(r, LEVELS3, t, n), channel(g, LEVELS3, t, n), channel(b, LEVELSB, t, n))
    return out
