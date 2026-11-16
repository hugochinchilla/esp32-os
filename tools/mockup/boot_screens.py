"""Boot screen options for Chinchilla, drawn in the board's real 256 colours.
Run from the repo's tools/mockup folder:  python3 boot_screens.py OUTDIR
Everything is procedural (gradients, scaled bitmaps, scaled Spleen 6x12), so
no 300 KB picture has to be stored in flash."""
import sys, os, math
from PIL import Image
sys.path.insert(0, os.environ.get("MOCKUP", "."))
from kit import *

F = os.environ.get("SPLEEN", "../../third_party/fonts/spleen/spleen-6x12.bdf")
REG = BdfFont(F)
W, H = 640, 480
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
OUT = sys.argv[1]


def dith(rgb, x, y):
    t = (BAYER[y & 3][x & 3] + 0.5) / 16
    out = []
    for v, levels in zip(rgb, (L3, L3, LB)):
        v = max(0, min(255, v))
        lo = max(l for l in levels if l <= v)
        hi = min(l for l in levels if l >= v)
        out.append(hi if hi != lo and (v - lo) / (hi - lo) > t else lo)
    return tuple(out)


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def ramp(stops, t):
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        if t <= t1:
            return lerp(c0, c1, (t - t0) / (t1 - t0) if t1 > t0 else 0)
    return stops[-1][1]


# ---- the mascot, 32x32, grown from the 16x16 Start-button face ----
G, L, S, P, K, N, Hh = DARK, WHITE, c(5, 5, 2), c(7, 4, 2), BLACK, c(7, 3, 2), WHITE


def big_mascot():
    n = 32
    grid = [[None] * n for _ in range(n)]

    def inside_circle(x, y, cx, cy, r):
        return (x + .5 - cx) ** 2 + (y + .5 - cy) ** 2 <= r * r

    def head(x, y):
        return ((x + .5 - 16) / 11.5) ** 2 + ((y + .5 - 19.5) / 9.8) ** 2 <= 1

    ears = [(7.5, 8), (24.5, 8)]
    # ears: outline, grey rim, pink inside
    for y in range(n):
        for x in range(n):
            for cx, cy in ears:
                if inside_circle(x, y, cx, cy, 6.6):
                    grid[y][x] = G
                if inside_circle(x, y, cx, cy, 5.6):
                    grid[y][x] = S
                if inside_circle(x, y, cx, cy - .3, 3.6):
                    grid[y][x] = P
    # head with outline
    for y in range(n):
        for x in range(n):
            if head(x, y):
                edge = any(not head(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                if edge:
                    grid[y][x] = G
                else:
                    # soft grey fur at the top of the head and the cheeks' lower edge
                    ny = (y + .5 - 19.5) / 9.8
                    grid[y][x] = S if ny < -0.72 else L
    # eyes 2x3 with highlight
    for ex in (10, 20):
        for dy in range(3):
            for dx in range(2):
                grid[17 + dy][ex + dx] = K
        grid[17][ex + 1] = Hh
    # nose and mouth
    for y, x in ((22, 15), (22, 16), (23, 15), (23, 16)):
        grid[y][x] = N
    grid[24][14] = grid[24][17] = G
    grid[25][15] = grid[25][16] = G
    # whiskers
    for x, y in ((0, 21), (1, 21), (2, 22), (3, 22), (0, 25), (1, 24), (2, 24), (3, 24),
                 (31, 21), (30, 21), (29, 22), (28, 22), (31, 25), (30, 24), (29, 24), (28, 24)):
        grid[y][x] = G
    return grid


MASCOT32 = big_mascot()
MASCOT16 = ["................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG", "GPPPGGLLLLGGPPPG",
            ".GGGLLLLLLLLGGG.", "..GLLLLLLLLLLG..", "..GLKLLLLLLKLG..", "..GLKLLLLLLKLG..", "..GLLLLNNLLLLG..",
            "D.GLLLLNNLLLLG.D", ".DDGLLLLLLLLGDD.", "D..GGLLLLLLGG..D", "....GGGGGGGG....", "................", "................"]
MK = {"G": DARK, "L": WHITE, "P": c(7, 4, 2), "K": BLACK, "N": c(7, 3, 2), "D": DARK}


def blit(px, grid, x0, y0, s, recolour=None, shadow=None):
    rows = len(grid)
    for r in range(rows):
        for q in range(len(grid[r])):
            v = grid[r][q]
            if isinstance(v, str):
                v = MK.get(v)
            if v is None:
                continue
            if recolour:
                v = recolour(v)
            if shadow is not None:
                for yy in range(s):
                    for xx in range(s):
                        px[x0 + q * s + xx + shadow[0], y0 + r * s + yy + shadow[1]] = shadow[2]
    for r in range(rows):
        for q in range(len(grid[r])):
            v = grid[r][q]
            if isinstance(v, str):
                v = MK.get(v)
            if v is None:
                continue
            if recolour:
                v = recolour(v)
            for yy in range(s):
                for xx in range(s):
                    px[x0 + q * s + xx, y0 + r * s + yy] = v


def text_mask(t, bold=False):
    f = REG.bold() if bold else REG
    w = f.width(t)
    m = Image.new("RGB", (w, REG.height), (1, 2, 3))
    f.draw(m, 0, 0, t, (255, 255, 255))
    mp = m.load()
    return [[mp[x, y] == (255, 255, 255) for x in range(w)] for y in range(REG.height)], w


def big_text(px, t, x0, y0, s, colour, bold=False, shadow=None):
    """colour: board colour or fn(x, y) -> board colour (for dithered fills)."""
    m, w = text_mask(t, bold)
    if shadow:
        dx, dy, sc = shadow
        for r, row in enumerate(m):
            for q, on in enumerate(row):
                if on:
                    for yy in range(s):
                        for xx in range(s):
                            px[x0 + q * s + xx + dx, y0 + r * s + yy + dy] = sc
    for r, row in enumerate(m):
        for q, on in enumerate(row):
            if on:
                for yy in range(s):
                    for xx in range(s):
                        X, Y = x0 + q * s + xx, y0 + r * s + yy
                        px[X, Y] = colour(X, Y) if callable(colour) else colour
    return w * s


def text(img, t, x, y, colour, bold=False):
    (REG.bold() if bold else REG).draw(img, x, y, t, colour)


def width(t, bold=False, s=1):
    return (REG.bold() if bold else REG).width(t) * s


def save(img, name):
    assert set(img.get_flattened_data()) <= set(PALETTE), name + ": a colour outside the board's 256"
    img.save(os.path.join(OUT, name))


# ================= A: Andes at dusk =================
def andes():
    img = Image.new("RGB", (W, H)); px = img.load()
    sky = [(0, (8, 10, 60)), (0.45, (60, 40, 120)), (0.75, (200, 90, 110)), (1.0, (255, 180, 90))]
    hz = 360
    for y in range(H):
        for x in range(W):
            px[x, y] = dith(ramp(sky, min(1, y / hz)), x, y)
    # a few stars in the dark part
    import random
    rnd = random.Random(7)
    for _ in range(70):
        x, y = rnd.randrange(W), rnd.randrange(150)
        px[x, y] = WHITE if rnd.random() < .4 else c(5, 5, 2)
    # far range (dithered purple) and near range (black), drawn from sums of sines
    for x in range(W):
        far = 300 + 26 * math.sin(x / 47) + 14 * math.sin(x / 13 + 1) + 8 * math.sin(x / 5.3)
        far -= 50 * math.exp(-((x - 470) / 60) ** 2)
        near = 352 + 18 * math.sin(x / 71 + 2) + 9 * math.sin(x / 19) + 4 * math.sin(x / 7)
        near -= 40 * math.exp(-((x - 150) / 70) ** 2)
        for y in range(int(far), H):
            px[x, y] = dith((70, 30, 90), x, y)
        for y in range(int(near), H):
            px[x, y] = BLACK
    s = 4
    mx = (W - 32 * s) // 2
    blit(px, MASCOT32, mx, 70, s, shadow=(4, 4, c(1, 0, 1)))
    tw = width("Chinchilla", True, 5)
    big_text(px, "Chinchilla", (W - tw) // 2, 220, 5, WHITE, bold=True, shadow=(4, 4, c(1, 0, 1)))
    # thin progress strip in the black foreground
    bx, by, bw = 220, 430, 200
    for i in range(10):
        on = i < 6
        for y in range(by, by + 6):
            for x in range(bx + i * 20 + 2, bx + i * 20 + 18):
                px[x, y] = c(7, 5, 1) if on else c(1, 1, 0)
    t = "Starting up"
    text(img, t, (W - width(t)) // 2, 412, c(5, 5, 2))
    save(img, "boot-a-andes.png")


# ================= B: power-on self test =================
def post():
    img = Image.new("RGB", (W, H), BLACK); px = img.load()
    fg, hi, dim = c(5, 5, 2), WHITE, c(3, 3, 1)
    blit(px, MASCOT32, W - 32 * 3 - 16, 16, 3)
    lines = [
        (hi, True, "Chinchilla 96 Boot Loader  v0.1"),
        (fg, False, "Copyleft 2026, built on nine resistors"),
        (fg, False, ""),
        (fg, False, "ESP32-S3 dual core at 240 MHz"),
        (fg, False, "Flash   16384 KB .......... OK"),
        (fg, False, "PSRAM    8192 KB octal ..... OK"),
        (fg, False, "Video   640x480  256 colours  60 Hz"),
        (fg, False, "Debug   UART0  921600 baud"),
        (fg, False, "Wi-Fi   ready"),
        (fg, False, "USB     no keyboard, no mouse"),
        (fg, False, ""),
        (hi, False, "Starting Chinchilla..."),
    ]
    y = 24
    for col, b, t in lines:
        text(img, t, 16, y, col, b)
        y += 14
    # cursor block
    for yy in range(y - 14 + 10, y - 14 + 12):
        for xx in range(16 + width("Starting Chinchilla..."), 22 + width("Starting Chinchilla...")):
            px[xx, yy] = hi
    t = "Press DEL to enter setup"
    text(img, t, 16, H - 24, dim)
    t = "Original logo, original text"
    save(img, "boot-b-self-test.png")


# ================= C: splash panel on the desktop =================
def panel():
    img = Image.new("RGB", (W, H)); px = img.load()
    for y in range(H):
        for x in range(W):
            px[x, y] = dith((58, 110, 165), x, y)
    pw, ph = 400, 230
    x0, y0 = (W - pw) // 2, (H - ph) // 2
    for y in range(y0, y0 + ph):
        for x in range(x0, x0 + pw):
            px[x, y] = GREY
    # raised 3D edge
    for x in range(x0, x0 + pw):
        px[x, y0] = WHITE; px[x, y0 + ph - 1] = BLACK; px[x, y0 + ph - 2] = DARK
    for y in range(y0, y0 + ph):
        px[x0, y] = WHITE; px[x0 + pw - 1, y] = BLACK; px[x0 + pw - 2, y] = DARK
    # gradient banner across the top like a 98 title bar, wider
    bh = 96
    for y in range(y0 + 3, y0 + 3 + bh):
        for x in range(x0 + 3, x0 + pw - 3):
            t = (x - x0) / pw
            px[x, y] = dith(lerp((0, 0, 128), (16, 132, 208), t), x, y)
    blit(px, MASCOT32, x0 + 20, y0 + 3 + (bh - 64) // 2, 2)
    big_text(px, "Chinchilla", x0 + 100, y0 + 3 + 14, 3, WHITE, bold=True, shadow=(2, 2, BLACK))
    text(img, "96", x0 + 100 + width("Chinchilla", True, 3) + 6, y0 + 3 + 14, c(7, 7, 2), True)
    text(img, "A small desktop for the ESP32-S3", x0 + 102, y0 + 3 + 60, WHITE)
    text(img, "Starting Chinchilla...", x0 + 20, y0 + 120, BLACK)
    # sunken progress bar with blue blocks
    bx, by, bw, bh2 = x0 + 20, y0 + 140, pw - 40, 20
    for y in range(by, by + bh2):
        for x in range(bx, bx + bw):
            px[x, y] = WHITE
    for x in range(bx, bx + bw):
        px[x, by] = DARK; px[x, by + bh2 - 1] = WHITE
    for y in range(by, by + bh2):
        px[bx, y] = DARK; px[bx + bw - 1, y] = WHITE
    for i in range(22):
        xs = bx + 3 + i * 10
        for y in range(by + 3, by + bh2 - 3):
            for x in range(xs, xs + 8):
                px[x, y] = c(0, 0, 2)
    text(img, "Copyleft 2026", x0 + 20, y0 + ph - 50, DARK)
    text(img, "640 x 480, 256 colours", x0 + 20, y0 + ph - 34, DARK)
    save(img, "boot-c-splash-panel.png")


# ================= D: big face on black =================
def bigface():
    img = Image.new("RGB", (W, H), BLACK); px = img.load()
    s = 14
    mx, my = (W - 16 * s) // 2, 20
    blit(px, MASCOT16, mx, my, s)
    # name filled with a dithered sunset gradient, letters 5x
    sc = 6
    tw = width("Chinchilla", True, sc)
    ty = 300
    grad = [(0, (255, 230, 120)), (0.5, (255, 130, 60)), (1, (200, 40, 120))]
    big_text(px, "Chinchilla", (W - tw) // 2, ty, sc,
             lambda X, Y: dith(ramp(grad, (Y - ty) / (12 * sc)), X, Y), bold=True,
             shadow=(3, 3, c(1, 1, 1)))
    t = "for the ESP32-S3"
    text(img, t, (W - width(t)) // 2, ty + 12 * sc + 8, c(5, 5, 2))
    # little moving dots strip
    for i in range(5):
        col = WHITE if i == 2 else c(3, 3, 1)
        x = W // 2 - 40 + i * 18
        for yy in range(450, 456):
            for xx in range(x, x + 6):
                px[xx, yy] = col
    save(img, "boot-d-big-face.png")


andes(); post(); panel(); bigface()
