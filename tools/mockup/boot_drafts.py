import sys
from PIL import Image, ImageDraw
from kit import *

F = "../../third_party/fonts/spleen/"
S12, S16, S24, S32, S64 = (BdfFont(F + "spleen-%s.bdf" % n) for n in ("6x12", "8x16", "12x24", "16x32", "32x64"))
W, H = 640, 480
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]

def dith(rgb, x, y):
    t = (BAYER[y & 3][x & 3] + 0.5) / 16
    out = []
    for v, levels in zip(rgb, (L3, L3, LB)):
        lo = max(l for l in levels if l <= v); hi = min(l for l in levels if l >= v)
        out.append(hi if hi != lo and (v - lo) / (hi - lo) > t else lo)
    return tuple(out)

MASCOT = [
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG", "GPPPGGLLLLGGPPPG",
    ".GGGLLLLLLLLGGG.", "..GLLLLLLLLLLG..", "..GLKLLLLLLKLG..", "..GLKLLLLLLKLG..", "..GLLLLNNLLLLG..",
    "D.GLLLLNNLLLLG.D", ".DDGLLLLLLLLGDD.", "D..GGLLLLLLGG..D", "....GGGGGGGG....", "................", "................"]

def mascot(img, x, y, s, outline=DARK, fur=WHITE, whisk=None):
    key = {"G": outline, "L": fur, "P": c(7, 4, 2), "K": BLACK, "N": c(7, 3, 2), "D": whisk or outline}
    d = ImageDraw.Draw(img)
    for r, row in enumerate(MASCOT):
        for q, ch in enumerate(row):
            if ch in key:
                d.rectangle([x + q * s, y + r * s, x + q * s + s - 1, y + r * s + s - 1], key[ch])

def centre(img, font, y, text, col):
    font.draw(img, (W - font.width(text)) // 2, y, text, col)

def nine(img, x, y, size, gap, lit=9):
    """The nine resistors as a progress row: three reds, three greens, three blues."""
    cols = [c(7, 0, 0), c(5, 0, 0), c(3, 0, 0), c(0, 7, 0), c(0, 5, 0), c(0, 3, 0), c(0, 0, 3), c(0, 0, 2), c(0, 0, 1)]
    d = ImageDraw.Draw(img)
    for i in range(9):
        col = cols[i] if i < lit else c(1, 1, 0)
        d.rectangle([x + i * (size + gap), y, x + i * (size + gap) + size - 1, y + size - 1], col, WHITE if i < lit else DARK)

def option_a():
    """Dusk sky fading to deep blue, big mascot, wordmark, nine-dot progress."""
    img = Image.new("RGB", (W, H))
    px = img.load()
    top, bot = (8, 16, 72), (40, 120, 190)
    for y in range(H):
        t = y / (H - 1)
        col = tuple(round(top[k] + (bot[k] - top[k]) * t) for k in range(3))
        for x in range(W):
            px[x, y] = dith(col, x, y)
    # a few stars in the upper half
    import random
    rnd = random.Random(7)
    for _ in range(60):
        px[rnd.randrange(W), rnd.randrange(200)] = WHITE
    mascot(img, (W - 16 * 9) // 2, 70, 9)
    centre(img, S64, 232, "Chinchilla", WHITE)
    centre(img, S16, 304, "for the ESP32-S3", c(5, 6, 3))
    nine(img, (W - (9 * 14 + 8 * 8)) // 2, 390, 14, 8, lit=6)
    centre(img, S12, 452, "640 x 480  -  256 colours  -  nine resistors", c(4, 5, 3))
    return img

def option_b():
    """Power-on self-test in text, the way PCs started."""
    img = Image.new("RGB", (W, H), BLACK)
    mascot(img, W - 16 * 5 - 24, 16, 5, outline=GREY, fur=WHITE)
    lines = [
        (WHITE, "Chinchilla BIOS v0.1"), (GREY, "Copyleft 2026, built on nine resistors"), (GREY, ""),
        (GREY, "Main Processor : ESP32-S3 dual core, 240 MHz"),
        (GREY, "Memory Test   :   8192K OK   (external)"),
        (GREY, "               :    512K OK   (internal)"), (GREY, ""),
        (GREY, "Detecting video ....... VGA 640x480, 256 colours"),
        (GREY, "Detecting drive C: .... 2 MB RAM disk"),
        (GREY, "Detecting mouse ....... serial debug link"),
        (GREY, "Detecting sound ....... none"), (GREY, ""),
        (c(7, 7, 0), "Starting Chinchilla..."),
    ]
    for i, (col, text) in enumerate(lines):
        S16.draw(img, 24, 24 + i * 18, text, col)
    S16.draw(img, 24, H - 40, "Press DEL to enter SETUP (there is no setup)", DARK)
    return img

def option_c():
    """Light and friendly: mascot beside the name, the whole palette as a ribbon."""
    img = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(img)
    mascot(img, 70, 130, 11)
    S64.draw(img, 270, 150, "Chinchilla", BLACK)
    S24.draw(img, 274, 222, "ninety-six", c(0, 0, 2))
    S12.draw(img, 274, 262, "A small desktop for a small computer", DARK)
    # ribbon of all 256 colours, sorted into 16 rows of 16
    x0, y0, cw, ch = 0, 380, 40, 6
    for v in range(256):
        r, g, b = v >> 5, (v >> 2) & 7, v & 3
        col_i = g * 2 + (b >> 1)          # 16 columns
        row_i = r * 2 + (b & 1)           # 16 rows
        d.rectangle([x0 + col_i * cw, y0 + row_i * ch, x0 + col_i * cw + cw - 1, y0 + row_i * ch + ch - 1], PALETTE[v])
    nine(img, 274, 300, 12, 6, lit=4)
    return img

for name, fn in (("a", option_a), ("b", option_b), ("c", option_c)):
    im = fn()
    assert set(im.get_flattened_data()) <= set(PALETTE), name
    im.save("boot-%s.png" % name)
sheet = Image.new("RGB", (W * 3 + 40, H + 20), (40, 40, 40))
for i, n in enumerate("abc"):
    sheet.paste(Image.open("boot-%s.png" % n), (10 + i * (W + 10), 10))
sheet.save("boot-options.png")
