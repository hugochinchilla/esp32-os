#!/usr/bin/env python3
"""Makes pictures for Chinchilla, all generated here (no outside artwork):
a test card, colour bars, gradients, the mascot enlarged and a synthetic
landscape that stands in for a photo. Standard library only; includes small
PNG, BMP and baseline JPEG writers.

  python3 tools/gen_pictures.py tests DIR     unit-test pictures + expected pixels + manifest
  python3 tools/gen_pictures.py bench DIR     640x480 pictures in each format, for the board
  python3 tools/gen_pictures.py embed FILE    the RAM disk samples as a C++ source file
"""
import math
import os
import struct
import sys
import zlib

# ---------------------------------------------------------------------------
# Pictures: functions of (w, h) giving rows of (r, g, b).


def gradient(w, h):
    return [[(x * 255 // max(1, w - 1), y * 255 // max(1, h - 1), (x + y) * 255 // max(1, w + h - 2))
             for x in range(w)] for y in range(h)]


def colour_bars(w, h):
    bars = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0), (255, 0, 255), (255, 0, 0),
            (0, 0, 255), (0, 0, 0)]
    rows = []
    for y in range(h):
        if y < h * 3 // 4:
            rows.append([bars[x * 8 // w] for x in range(w)])
        else:  # a grey ramp along the bottom
            rows.append([(x * 255 // max(1, w - 1),) * 3 for x in range(w)])
    return rows


def test_card(w, h):
    """Grid, circle, colour patches, grey steps and fine lines."""
    rows = []
    cx, cy, rad = w / 2, h / 2, min(w, h) * 0.4
    patches = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255)]
    for y in range(h):
        row = []
        for x in range(w):
            c = (96, 96, 96)
            if x % (w // 10 or 1) == 0 or y % (h // 8 or 1) == 0:
                c = (255, 255, 255)
            d = math.hypot(x - cx, y - cy)
            if abs(d - rad) < 1.2:
                c = (255, 255, 255)
            if h * 0.35 < y < h * 0.5 and w * 0.2 < x < w * 0.8:
                c = patches[int((x - w * 0.2) * 6 / (w * 0.6))]
            if h * 0.5 <= y < h * 0.65 and w * 0.2 < x < w * 0.8:
                c = (int((x - w * 0.2) * 8 / (w * 0.6)) * 255 // 7,) * 3
            if h * 0.7 < y < h * 0.78 and w * 0.3 < x < w * 0.7:
                c = (255, 255, 255) if (x // max(1, (int(y * 3 / h) % 3) + 1)) % 2 else (0, 0, 0)
            row.append(c)
        rows.append(row)
    return rows


MASCOT = [
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG", "GPPPGGLLLLGGPPPG",
    ".GGGLLLLLLLLGGG.", "..GLLLLLLLLLLG..", "..GLKLLLLLLKLG..", "..GLKLLLLLLKLG..", "..GLLLLNNLLLLG..",
    "D.GLLLLNNLLLLG.D", ".DDGLLLLLLLLGDD.", "D..GGLLLLLLGG..D", "....GGGGGGGG....", "................",
    "................"]
MASCOT_INK = {"G": (73, 73, 73), "L": (255, 255, 255), "P": (255, 146, 182), "K": (0, 0, 0),
              "N": (255, 109, 182), "D": (73, 73, 73), ".": (58, 110, 165)}


def mascot(w, h):
    return [[MASCOT_INK[MASCOT[y * 16 // h][x * 16 // w]] for x in range(w)] for y in range(h)]


def noise(x, y, seed=1):
    n = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def smooth_noise(x, y, scale, seed):
    gx, gy = x / scale, y / scale
    x0, y0 = int(gx), int(gy)
    fx, fy = gx - x0, gy - y0
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = noise(x0, y0, seed), noise(x0 + 1, y0, seed)
    c, d = noise(x0, y0 + 1, seed), noise(x0 + 1, y0 + 1, seed)
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def landscape(w, h):
    """Evening sky, sun, two ranges of hills, a lake with reflections."""
    rows = []
    horizon = int(h * 0.62)
    sun_x, sun_y, sun_r = w * 0.68, h * 0.40, h * 0.07

    def sky(x, y):
        t = y / horizon
        r = int(40 + 215 * t)
        g = int(60 + 120 * t)
        b = int(140 - 40 * t)
        d = math.hypot(x - sun_x, y - sun_y)
        if d < sun_r:
            return (255, 236, 170)
        glow = max(0.0, 1 - d / (h * 0.35))
        cloud = smooth_noise(x, y * 2.5, w / 8, 7)
        if cloud > 0.62 and y < horizon * 0.8:
            k = (cloud - 0.62) * 2.2
            r, g, b = r + (250 - r) * k, g + (200 - g) * k, b + (190 - b) * k
        return (min(255, int(r + 60 * glow)), min(255, int(g + 40 * glow)), min(255, int(b + 10 * glow)))

    far = [horizon - int(h * 0.12 * smooth_noise(x, 0, w / 6, 3)) for x in range(w)]
    near = [horizon - int(h * 0.20 * smooth_noise(x, 0, w / 4, 5) - h * 0.02) for x in range(w)]
    for y in range(h):
        row = []
        for x in range(w):
            if y < horizon:
                if y >= near[x]:
                    t = smooth_noise(x, y, w / 30, 9)
                    c = (int(30 + 40 * t), int(70 + 60 * t), int(30 + 20 * t))
                elif y >= far[x]:
                    c = (90, 80, 120)
                else:
                    c = sky(x, y)
            else:
                my = 2 * horizon - y  # mirrored
                ripple = int(3 * math.sin(y * 0.9 + x * 0.05))
                sx = min(w - 1, max(0, x + ripple))
                if my >= near[sx]:
                    t = smooth_noise(sx, my, w / 30, 9)
                    base = (int(30 + 40 * t), int(70 + 60 * t), int(30 + 20 * t))
                elif my >= far[sx]:
                    base = (90, 80, 120)
                else:
                    base = sky(sx, my)
                c = tuple(int(v * 0.7 + 20) for v in base)
            row.append(c)
        rows.append(row)
    return rows


# ---------------------------------------------------------------------------
# Writers.


def png_chunk(tag, data):
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))


def write_png(path, rows, kind="rgb8", palette=None, trns=None, alpha=None, interlace=0):
    """kind: rgb8, rgb16, grey1/2/4/8/16, pal1/2/4/8, rgba8, rgba16, greya8, greya16."""
    h, w = len(rows), len(rows[0])
    raw = bytearray()
    if kind.startswith("pal"):
        depth, ctype = int(kind[3:]), 3
    elif kind.startswith("greya"):
        depth, ctype = int(kind[5:]), 4
    elif kind.startswith("grey"):
        depth, ctype = int(kind[4:]), 0
    elif kind.startswith("rgba"):
        depth, ctype = int(kind[4:]), 6
    else:
        depth, ctype = int(kind[3:]), 2
    for y, row in enumerate(rows):
        raw.append(y % 5)  # every filter type
        line = bytearray()
        if ctype in (0, 3) and depth < 8:
            acc, n = 0, 0
            for px in row:
                v = px if ctype == 3 else px[0] * ((1 << depth) - 1) // 255
                acc = (acc << depth) | v
                n += depth
                if n == 8:
                    line.append(acc)
                    acc, n = 0, 0
            if n:
                line.append(acc << (8 - n))
        else:
            for x, px in enumerate(row):
                if ctype == 3:
                    vals = [px]
                elif ctype == 0:
                    vals = [px[0]]
                elif ctype == 4:
                    vals = [px[0], alpha[y][x]]
                elif ctype == 6:
                    vals = list(px) + [alpha[y][x]]
                else:
                    vals = list(px)
                for v in vals:
                    if depth == 16:
                        line += struct.pack(">H", v * 257)
                    else:
                        line.append(v)
        raw += filter_row(line, raw_prev(raw, len(line), y), y % 5, max(1, (depth * channels(ctype)) // 8))
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, interlace))
    if palette:
        data += png_chunk(b"PLTE", bytes(c for p in palette for c in p))
    if trns is not None:
        data += png_chunk(b"tRNS", trns)
    comp = zlib.compress(bytes(raw), 9)
    for i in range(0, len(comp), 8192):  # several IDAT chunks
        data += png_chunk(b"IDAT", comp[i:i + 8192])
    data += png_chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


_prev_line = {}


def channels(ctype):
    return {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]


def raw_prev(raw, n, y):
    return _prev_line.get("prev", bytes(n)) if y > 0 else bytes(n)


def filter_row(line, prev, ftype, bpp):
    out = bytearray(len(line))
    for i in range(len(line)):
        a = line[i - bpp] if i >= bpp else 0
        b = prev[i]
        c = prev[i - bpp] if i >= bpp else 0
        if ftype == 0:
            p = 0
        elif ftype == 1:
            p = a
        elif ftype == 2:
            p = b
        elif ftype == 3:
            p = (a + b) // 2
        else:
            pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
            p = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
        out[i] = (line[i] - p) & 255
    _prev_line["prev"] = bytes(line)
    return bytes(out)


def write_bmp(path, rows, bpp=24, top_down=False, palette=None):
    h, w = len(rows), len(rows[0])
    stride = ((w * bpp + 31) // 32) * 4
    ncol = len(palette) if palette else 0
    pal = b"".join(bytes((p[2], p[1], p[0], 0)) for p in (palette or []))
    bitfields = bpp == 16
    extra = 12 if bitfields else 0
    offset = 14 + 40 + extra + len(pal)
    data = bytearray()
    order = range(h) if top_down else range(h - 1, -1, -1)
    for y in order:
        line = bytearray()
        if bpp <= 8:
            acc, n = 0, 0
            for idx in rows[y]:
                acc = (acc << bpp) | idx
                n += bpp
                if n == 8:
                    line.append(acc)
                    acc, n = 0, 0
            if n:
                line.append(acc << (8 - n))
        elif bpp == 16:  # 5-6-5 with bit fields
            for r, g, b in rows[y]:
                line += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        else:
            for r, g, b in rows[y]:
                line += bytes((b, g, r)) + (b"\x00" if bpp == 32 else b"")
        line += bytes(stride - len(line))
        data += line
    header = struct.pack("<2sIHHI", b"BM", offset + len(data), 0, 0, offset)
    info = struct.pack("<IiiHHIIiiII", 40, w, -h if top_down else h, 1, bpp, 3 if bitfields else 0,
                       len(data), 2835, 2835, ncol, 0)
    masks = struct.pack("<III", 0xF800, 0x07E0, 0x001F) if bitfields else b""
    with open(path, "wb") as f:
        f.write(header + info + masks + pal + data)


# --- Baseline JPEG ---------------------------------------------------------

ZIGZAG = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7,
          14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39,
          46, 53, 60, 61, 54, 47, 55, 62, 63]
Q_LUMA = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22,
          29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103,
          121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99]
Q_CHROMA = [17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66,
            99, 99, 99, 99, 99, 99] + [99] * 32
DC_LUMA = ([0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0], list(range(12)))
DC_CHROMA = ([0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0], list(range(12)))
AC_LUMA = ([0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d], bytes.fromhex(
    "01020300041105122131410613516107227114328191a1082342b1c11552d1f02433627282090a161718191a25262728292a3435363738"
    "393a434445464748494a535455565758595a636465666768696a737475767778797a838485868788898a92939495969798999aa2a3a4a5"
    "a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae1e2e3e4e5e6e7e8e9eaf1f2f3f4f5f6f7f8f9fa"))
AC_CHROMA = ([0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77], bytes.fromhex(
    "000102031104052131061241510761711322328108144291a1b1c109233352f0156272d10a162434e125f11718191a262728292a3536"
    "3738393a434445464748494a535455565758595a636465666768696a737475767778797a82838485868788898a92939495969798999aa2"
    "a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae2e3e4e5e6e7e8e9eaf2f3f4f5f6f7f8f9fa"))


def huff_codes(table):
    bits, vals = table
    codes, code, k = {}, 0, 0
    for length in range(1, 17):
        for _ in range(bits[length - 1]):
            codes[vals[k]] = (code, length)
            code += 1
            k += 1
        code <<= 1
    return codes


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, value, length):
        for i in range(length - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                if self.acc == 0xFF:
                    self.out.append(0)
                self.acc = self.n = 0

    def flush(self):
        while self.n:
            self.put(1, 1)


COS = [[math.cos((2 * x + 1) * u * math.pi / 16) for x in range(8)] for u in range(8)]


def fdct(block):
    tmp = [[sum(block[y][x] * COS[u][x] for x in range(8)) for u in range(8)] for y in range(8)]
    out = [0.0] * 64
    for v in range(8):
        for u in range(8):
            s = sum(tmp[y][u] * COS[v][y] for y in range(8))
            cu = 1 / math.sqrt(2) if u == 0 else 1
            cv = 1 / math.sqrt(2) if v == 0 else 1
            out[v * 8 + u] = 0.25 * cu * cv * s
    return out


def write_jpeg(path, rows, quality=80, sampling="420", restart=0, progressive_marker=False):
    h, w = len(rows), len(rows[0])
    scale = 5000 / quality if quality < 50 else 200 - quality * 2
    ql = [max(1, min(255, (q * scale + 50) // 100)) for q in Q_LUMA]
    qc = [max(1, min(255, (q * scale + 50) // 100)) for q in Q_CHROMA]
    grey = sampling == "grey"
    hs = 2 if sampling in ("420", "422") else 1
    vs = 2 if sampling == "420" else 1
    if grey:
        hs = vs = 1
    Y = [[0.299 * r + 0.587 * g + 0.114 * b for r, g, b in row] for row in rows]
    Cb = [[-0.168736 * r - 0.331264 * g + 0.5 * b + 128 for r, g, b in row] for row in rows]
    Cr = [[0.5 * r - 0.418688 * g - 0.081312 * b + 128 for r, g, b in row] for row in rows]

    def px(plane, x, y):
        return plane[min(y, h - 1)][min(x, w - 1)]

    def chroma(plane, x, y):  # average of the samples a chroma pixel covers
        s = 0
        for dy in range(vs):
            for dx in range(hs):
                s += px(plane, x * hs + dx, y * vs + dy)
        return s / (hs * vs)

    out = bytearray(b"\xff\xd8")
    out += b"\xff\xe0" + struct.pack(">H", 16) + b"JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
    for tid, q in ((0, ql), (1, qc)):
        out += b"\xff\xdb" + struct.pack(">HB", 67, tid) + bytes(q[ZIGZAG[k]] for k in range(64))
    sof = b"\xff\xc2" if progressive_marker else b"\xff\xc0"
    ncomp = 1 if grey else 3
    out += sof + struct.pack(">HBHHB", 8 + 3 * ncomp, 8, h, w, ncomp)
    out += bytes((1, (hs << 4) | vs, 0))
    if not grey:
        out += bytes((2, 0x11, 1, 3, 0x11, 1))
    if progressive_marker:
        out += b"\xff\xd9"
        with open(path, "wb") as f:
            f.write(out)
        return
    for cls, tid, table in ((0, 0, DC_LUMA), (1, 0, AC_LUMA), (0, 1, DC_CHROMA), (1, 1, AC_CHROMA)):
        bits, vals = table
        out += b"\xff\xc4" + struct.pack(">HB", 3 + 16 + len(vals), (cls << 4) | tid) + bytes(bits) + bytes(vals)
    if restart:
        out += b"\xff\xdd" + struct.pack(">HH", 4, restart)
    out += b"\xff\xda" + struct.pack(">HB", 6 + 2 * ncomp, ncomp) + bytes((1, 0x00))
    if not grey:
        out += bytes((2, 0x11, 3, 0x11))
    out += bytes((0, 63, 0))
    dcl, acl, dcc, acc = (huff_codes(t) for t in (DC_LUMA, AC_LUMA, DC_CHROMA, AC_CHROMA))
    bw = BitWriter()
    preds = [0, 0, 0]

    def encode(block, q, dc_codes, ac_codes, ci):
        coef = fdct([[v - 128 for v in row] for row in block])
        zz = [int(round(coef[ZIGZAG[k]] / q[ZIGZAG[k]])) for k in range(64)]
        diff = zz[0] - preds[ci]
        preds[ci] = zz[0]

        def bits_of(v):
            s = abs(v).bit_length()
            return s, (v if v >= 0 else v + (1 << s) - 1)

        s, v = bits_of(diff)
        bw.put(*dc_codes[s])
        if s:
            bw.put(v, s)
        run = 0
        last = max([k for k in range(1, 64) if zz[k] != 0], default=0)
        for k in range(1, last + 1):
            if zz[k] == 0:
                run += 1
                continue
            while run > 15:
                bw.put(*ac_codes[0xF0])
                run -= 16
            s, v = bits_of(zz[k])
            bw.put(*ac_codes[(run << 4) | s])
            bw.put(v, s)
            run = 0
        if last < 63:
            bw.put(*ac_codes[0x00])

    mcu_w, mcu_h = 8 * hs, 8 * vs
    count, rst = 0, 0
    for my in range(0, h, mcu_h):
        for mx in range(0, w, mcu_w):
            if restart and count and count % restart == 0:
                bw.flush()
                bw.out += bytes((0xFF, 0xD0 + rst))
                rst = (rst + 1) % 8
                preds[:] = [0, 0, 0]
            count += 1
            for by in range(vs):
                for bx in range(hs):
                    block = [[px(Y, mx + bx * 8 + x, my + by * 8 + y) for x in range(8)] for y in range(8)]
                    encode(block, ql, dcl, acl, 0)
            if not grey:
                for ci, plane in ((1, Cb), (2, Cr)):
                    block = [[chroma(plane, mx // hs + x, my // vs + y) for x in range(8)] for y in range(8)]
                    encode(block, qc, dcc, acc, ci)
    bw.flush()
    out += bw.out + b"\xff\xd9"
    with open(path, "wb") as f:
        f.write(out)


# ---------------------------------------------------------------------------


def raw_rgb(rows):
    return bytes(c for row in rows for px in row for c in px)


def blend(c, a, bg=(255, 255, 255)):
    return tuple((c[i] * a + bg[i] * (255 - a) + 127) // 255 for i in range(3))


def make_tests(folder):
    """Small pictures of every kind, the pixels each should decode to, and a
    manifest: name, then 'exact', 'lossy' or the expected error."""
    os.makedirs(folder, exist_ok=True)
    manifest = []
    w, h = 37, 29  # odd sizes catch row padding and partial MCUs
    pic = gradient(w, h)

    def exact(name, rows):
        with open(os.path.join(folder, name + ".rgb"), "wb") as f:
            f.write(raw_rgb(rows))
        manifest.append("%s exact %d %d" % (name, len(rows[0]), len(rows)))

    # PNG: every colour type and depth.
    write_png(os.path.join(folder, "rgb8.png"), pic, "rgb8"); exact("rgb8.png", pic)
    write_png(os.path.join(folder, "rgb16.png"), pic, "rgb16"); exact("rgb16.png", pic)
    for d in (1, 2, 4, 8, 16):
        levels = (1 << min(d, 8)) - 1
        g = [[((x * 7 + y * 3) % (levels + 1) * 255 // levels,) * 3 for x in range(w)] for y in range(h)]
        write_png(os.path.join(folder, "grey%d.png" % d), g, "grey%d" % d)
        exact("grey%d.png" % d, g)
    pal = [(i * 37 % 256, i * 91 % 256, i * 53 % 256) for i in range(256)]
    for d in (1, 2, 4, 8):
        n = 1 << d
        idx = [[(x + y * 3) % n for x in range(w)] for y in range(h)]
        write_png(os.path.join(folder, "pal%d.png" % d), idx, "pal%d" % d, palette=pal[:n])
        exact("pal%d.png" % d, [[pal[i] for i in row] for row in idx])
    # Palette with transparency, RGBA, grey + alpha (over white).
    idx = [[(x + y) % 4 for x in range(w)] for y in range(h)]
    tr = bytes((255, 0, 128, 64))
    write_png(os.path.join(folder, "pal-trns.png"), idx, "pal2", palette=pal[:4], trns=tr)
    exact("pal-trns.png", [[blend(pal[i], tr[i]) for i in row] for row in idx])
    alpha = [[(x * 255 // (w - 1)) for x in range(w)] for y in range(h)]
    write_png(os.path.join(folder, "rgba8.png"), pic, "rgba8", alpha=alpha)
    exact("rgba8.png", [[blend(pic[y][x], alpha[y][x]) for x in range(w)] for y in range(h)])
    write_png(os.path.join(folder, "rgba16.png"), pic, "rgba16", alpha=alpha)
    exact("rgba16.png", [[blend(pic[y][x], alpha[y][x]) for x in range(w)] for y in range(h)])
    grey = [[(v[0],) * 3 for v in row] for row in pic]
    write_png(os.path.join(folder, "greya8.png"), grey, "greya8", alpha=alpha)
    exact("greya8.png", [[blend(grey[y][x], alpha[y][x]) for x in range(w)] for y in range(h)])
    write_png(os.path.join(folder, "interlaced.png"), pic, "rgb8", interlace=1)
    manifest.append("interlaced.png error Unsupported")

    # BMP.
    write_bmp(os.path.join(folder, "bmp24.bmp"), pic, 24); exact("bmp24.bmp", pic)
    write_bmp(os.path.join(folder, "bmp24-top.bmp"), pic, 24, top_down=True); exact("bmp24-top.bmp", pic)
    write_bmp(os.path.join(folder, "bmp32.bmp"), pic, 32); exact("bmp32.bmp", pic)
    q565 = [[(((r >> 3) * 255 + 15) // 31, ((g >> 2) * 255 + 31) // 63, ((b >> 3) * 255 + 15) // 31) for r, g, b in row] for row in pic]
    write_bmp(os.path.join(folder, "bmp16.bmp"), pic, 16); exact("bmp16.bmp", q565)
    for d in (1, 4, 8):
        n = 1 << d
        idx = [[(x * 3 + y) % n for x in range(w)] for y in range(h)]
        write_bmp(os.path.join(folder, "bmp%d.bmp" % d), idx, d, palette=pal[:n])
        exact("bmp%d.bmp" % d, [[pal[i] for i in row] for row in idx])

    # JPEG: compared loosely with the picture it was made from.
    photo = landscape(64, 48)
    for name, kw in (("j420.jpg", {"sampling": "420"}), ("j444.jpg", {"sampling": "444", "quality": 90}),
                     ("j422.jpg", {"sampling": "422"}), ("jgrey.jpg", {"sampling": "grey"}),
                     ("jrestart.jpg", {"sampling": "420", "restart": 3}),
                     ("jodd.jpg", {"sampling": "420", "rows": gradient(37, 29)})):
        rows = kw.pop("rows", photo)
        write_jpeg(os.path.join(folder, name), rows, **kw)
        ref = rows if kw.get("sampling") != "grey" else \
            [[(round(0.299 * r + 0.587 * g + 0.114 * b),) * 3 for r, g, b in row] for row in rows]
        with open(os.path.join(folder, name + ".rgb"), "wb") as f:
            f.write(raw_rgb(ref))
        # Chroma at half size loses colour detail at sharp edges: a lower bar.
        floor = 26 if kw.get("sampling") == "420" else 30
        manifest.append("%s lossy %d %d %d" % (name, len(rows[0]), len(rows), floor))
    write_jpeg(os.path.join(folder, "progressive.jpg"), photo, progressive_marker=True)
    manifest.append("progressive.jpg error Unsupported")

    with open(os.path.join(folder, "not-a-picture.png"), "wb") as f:
        f.write(b"hello, this is text\n")
    manifest.append("not-a-picture.png error Unsupported")
    with open(os.path.join(folder, "manifest.txt"), "w") as f:
        f.write("\n".join(manifest) + "\n")


def samples():
    """The RAM disk's pictures: small, so the firmware stays small."""
    out = {}
    tmp = "/tmp/chinchilla-samples"
    os.makedirs(tmp, exist_ok=True)
    write_png(tmp + "/test card.png", test_card(320, 240), "rgb8")
    write_jpeg(tmp + "/landscape.jpg", landscape(320, 240), quality=75)
    idx_rows = mascot(128, 128)
    pal = sorted(set(c for row in idx_rows for c in row))
    write_bmp(tmp + "/mascot.bmp", [[pal.index(c) for c in row] for row in idx_rows], 4,
              palette=pal + [(0, 0, 0)] * (16 - len(pal)))
    for name in ("test card.png", "landscape.jpg", "mascot.bmp"):
        with open(tmp + "/" + name, "rb") as f:
            out[name] = f.read()
    return out


def make_embed(path):
    files = samples()
    with open(path, "w") as f:
        f.write("// Generated by tools/gen_pictures.py embed; do not edit. The pictures are\n")
        f.write("// made by that script (no outside artwork).\n")
        f.write('#include "core/fs/samples.hpp"\n\nnamespace fs {\n\nnamespace {\n\n')
        for i, (name, data) in enumerate(files.items()):
            f.write("const uint8_t kFile%d[%d] = {\n" % (i, len(data)))
            for j in range(0, len(data), 20):
                f.write("    " + ", ".join("%d" % b for b in data[j:j + 20]) + ",\n")
            f.write("};\n\n")
        f.write("} // namespace\n\nconst SampleFile kSamplePictures[] = {\n")
        for i, (name, data) in enumerate(files.items()):
            f.write('    {"%s", kFile%d, sizeof(kFile%d)},\n' % (name, i, i))
        f.write("};\nconst int kSamplePictureCount = %d;\n\n} // namespace fs\n" % len(files))
    print("%s: %s" % (path, ", ".join("%s %d bytes" % (n, len(d)) for n, d in files.items())))


def make_bench(folder):
    os.makedirs(folder, exist_ok=True)
    photo = landscape(640, 480)
    write_jpeg(os.path.join(folder, "landscape640.jpg"), photo, quality=80)
    write_png(os.path.join(folder, "landscape640.png"), photo, "rgb8")
    write_bmp(os.path.join(folder, "landscape640.bmp"), photo, 24)
    write_png(os.path.join(folder, "testcard640.png"), test_card(640, 480), "rgb8")
    write_png(os.path.join(folder, "bars640.png"), colour_bars(640, 480), "rgb8")
    write_jpeg(os.path.join(folder, "gradient640.jpg"), gradient(640, 480), quality=90, sampling="444")
    with open(os.path.join(folder, "landscape640.rgb"), "wb") as f:
        f.write(raw_rgb(photo))
    print("bench pictures in", folder)


if __name__ == "__main__":
    what, target = sys.argv[1], sys.argv[2]
    {"tests": make_tests, "bench": make_bench, "embed": make_embed}[what](target)
