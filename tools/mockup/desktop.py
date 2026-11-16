"""Draws the target look of the desktop (docs/look/target-98.png) in the board's
real 256 colours. A reference for the window manager, not part of the OS.

  python3 tools/mockup/desktop.py 98 docs/look/target-98.png   (needs Pillow)
"""
import sys
from PIL import Image, ImageDraw
from kit import *

import os
F = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "third_party", "fonts", "spleen") + "/"
REG = BdfFont(F + "spleen-6x12.bdf")
BOLD = REG.bold()
STYLE = sys.argv[1]            # "95" or "98"
W, H = 640, 480
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def dith(rgb, x, y):
    """Nearest board colours, mixed in a fixed pattern to reach in-between ones."""
    t = (BAYER[y & 3][x & 3] + 0.5) / 16
    out = []
    for v, levels in zip(rgb, (L3, L3, LB)):
        lo = max(l for l in levels if l <= v)
        hi = min(l for l in levels if l >= v)
        out.append(hi if hi != lo and (v - lo) / (hi - lo) > t else lo)
    return tuple(out)


img = Image.new("RGB", (W, H))
px = img.load()
d = ImageDraw.Draw(img)


def checker(x, y, w, h):          # the classic white/grey weave
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            px[xx, yy] = WHITE if (xx + yy) & 1 else GREY


def fill(x, y, w, h, fn):
    for yy in range(max(0, y), min(H, y + h)):
        for xx in range(max(0, x), min(W, x + w)):
            px[xx, yy] = dith(fn(xx - x, yy - y) if callable(fn) else fn, xx, yy)


def rect(x, y, w, h, col):
    d.rectangle([x, y, x + w - 1, y + h - 1], col)


def edge(x, y, w, h, tl, br):
    d.line([(x, y), (x + w - 2, y)], tl); d.line([(x, y), (x, y + h - 2)], tl)
    d.line([(x, y + h - 1), (x + w - 1, y + h - 1)], br); d.line([(x + w - 1, y), (x + w - 1, y + h - 1)], br)


def raised(x, y, w, h):          # window / button edge, two pixels
    edge(x, y, w, h, GREY, BLACK); edge(x + 1, y + 1, w - 2, h - 2, WHITE, DARK)


def sunken(x, y, w, h):          # text fields, lists
    edge(x, y, w, h, DARK, WHITE); edge(x + 1, y + 1, w - 2, h - 2, BLACK, GREY)


def button(x, y, w, h, text=None, font=REG, pressed=False, flat=False):
    rect(x, y, w, h, GREY)
    if pressed:
        edge(x, y, w, h, BLACK, WHITE); edge(x + 1, y + 1, w - 2, h - 2, DARK, GREY)
        checker(x + 2, y + 2, w - 4, h - 4)
    elif not flat:
        edge(x, y, w, h, WHITE, BLACK); edge(x + 1, y + 1, w - 2, h - 2, GREY, DARK)
    if text:
        font.draw(img, x + (w - font.width(text)) // 2 + pressed, y + (h - font.height) // 2 + pressed, text, BLACK)


def glyph(x, y, rows, col=BLACK):
    for r, row in enumerate(rows):
        for q, ch in enumerate(row):
            if ch == "X":
                px[x + q, y + r] = col


CLOSE = ["XX    XX", " XX  XX ", "  XXXX  ", "   XX   ", "  XXXX  ", " XX  XX ", "XX    XX"]
MAXI = ["XXXXXXXXX", "XXXXXXXXX", "X       X", "X       X", "X       X", "X       X", "X       X", "X       X", "XXXXXXXXX"]
MINI = ["      ", "      ", "      ", "      ", "      ", "      ", "XXXXXX", "XXXXXX"]
UP = ["   X   ", "  XXX  ", " XXXXX ", "XXXXXXX"]
DOWN = UP[::-1]


def titlebar(x, y, w, text, active):
    h = 18
    if STYLE == "98":
        a, b = ((0, 0, 128), (16, 132, 208)) if active else ((128, 128, 128), (192, 192, 192))
        fill(x, y, w, h, lambda i, j: tuple(a[k] + (b[k] - a[k]) * i // max(1, w - 1) for k in range(3)))
    else:
        fill(x, y, w, h, (0, 0, 128) if active else (128, 128, 128))
    BOLD.draw(img, x + 22, y + 3, text, WHITE if active else GREY)
    bx = x + w - 18
    for g in (CLOSE, MAXI, MINI):
        button(bx, y + 2, 16, 14)
        glyph(bx + 4, y + 5, g)
        bx -= 16 if g is not CLOSE else 18
    return h


def window(x, y, w, h, title, active, menus=("File", "Edit", "View", "Help")):
    rect(x, y, w, h, GREY)
    raised(x, y, w, h)
    titlebar(x + 4, y + 4, w - 8, title, active)
    mx = x + 10
    for m in menus:
        REG.draw(img, mx, y + 26, m, BLACK)
        d.line([(mx, y + 37), (mx + REG.width(m[0]) - 1, y + 37)], BLACK)
        mx += REG.width(m) + 12
    return x + 4, y + 42, w - 8, h - 46      # client area


def scrollbar(x, y, h):
    checker(x, y, 16, h)
    button(x, y, 16, 16); glyph(x + 4, y + 6, UP)
    button(x, y + h - 16, 16, 16); glyph(x + 4, y + h - 10, DOWN)
    button(x, y + 16, 16, 40)


# ---- Icons: original, drawn from simple shapes ------------------------------
def icon(kind, x, y, s=32):
    k = s / 32
    def R(a, b, c2, e, col, outline=BLACK):
        d.rectangle([x + round(a * k), y + round(b * k), x + round(c2 * k), y + round(e * k)], col, outline)
    if kind == "folder":
        R(3, 7, 14, 12, c(6, 5, 0)); R(2, 10, 29, 26, c(7, 6, 1))
        d.line([(x + round(3 * k), y + round(11 * k)), (x + round(28 * k), y + round(11 * k))], c(7, 7, 2))
    elif kind == "text":
        R(7, 3, 25, 28, WHITE)
        for i in range(4 if s < 20 else 6):
            yy = y + round((8 + i * (3.4 if s >= 20 else 5)) * k)
            d.line([(x + round(10 * k), yy), (x + round(22 * k), yy)], c(0, 0, 2))
    elif kind == "picture":
        R(4, 5, 28, 26, WHITE); R(6, 7, 26, 24, c(3, 5, 3), None)
        d.polygon([(x + round(6 * k), y + round(24 * k)), (x + round(13 * k), y + round(14 * k)), (x + round(20 * k), y + round(24 * k))], c(0, 4, 0))
        d.ellipse([x + round(19 * k), y + round(9 * k), x + round(23 * k), y + round(13 * k)], c(7, 6, 0))
    elif kind == "board":           # the ESP32 board itself
        R(5, 3, 26, 28, c(0, 3, 1)); R(9, 6, 22, 15, GREY); R(12, 19, 19, 25, BLACK)
        for i in range(6):
            yy = y + round((6 + i * 4) * k)
            d.line([(x + round(2 * k), yy), (x + round(5 * k), yy)], c(6, 5, 0))
            d.line([(x + round(26 * k), yy), (x + round(29 * k), yy)], c(6, 5, 0))
    elif kind == "bin":
        R(8, 8, 23, 28, GREY); R(6, 5, 25, 8, GREY); R(13, 3, 18, 5, GREY)
        for i in range(3):
            xx = x + round((12 + i * 4) * k)
            d.line([(xx, y + round(11 * k)), (xx, y + round(25 * k))], DARK)
    elif kind == "net":
        R(4, 6, 18, 17, c(1, 5, 3)); R(8, 18, 14, 20, GREY); R(14, 14, 28, 25, c(1, 5, 3)); R(18, 26, 24, 28, GREY)


def desk_icon(kind, x, y, text):
    icon(kind, x + 16, y)
    tw = REG.width(text)
    REG.draw(img, x + 32 - tw // 2 + 1, y + 35, text, BLACK)
    REG.draw(img, x + 32 - tw // 2, y + 34, text, WHITE)


# ---- Desktop ----------------------------------------------------------------
fill(0, 0, W, H, (0, 128, 128) if STYLE == "95" else (58, 110, 165))
for i, (k, t) in enumerate((("board", "My Board"), ("folder", "Documents"), ("net", "Network"), ("bin", "Recycle Bin"))):
    desk_icon(k, 6, 8 + i * 70, t)

# File manager window (inactive, behind)
cx, cy, cw, ch = window(96, 28, 400, 250 if STYLE == "95" else 300, "Flash (C:)", False)
icon("folder", 101, 34, 16)
if STYLE == "98":
    bx = cx + 4
    for t in ("Back", "Up", "Cut", "Copy", "Paste"):
        button(bx, cy, 44, 34, flat=True)
        icon("folder" if t in ("Back", "Up") else "text", bx + 14, cy + 2, 16)
        REG.draw(img, bx + (44 - REG.width(t)) // 2, cy + 20, t, BLACK)
        bx += 46
    d.line([(cx, cy + 36), (cx + cw - 1, cy + 36)], DARK); d.line([(cx, cy + 37), (cx + cw - 1, cy + 37)], WHITE)
    REG.draw(img, cx + 4, cy + 44, "Address", BLACK)
    sunken(cx + 50, cy + 40, cw - 54, 20); rect(cx + 52, cy + 42, cw - 58, 16, WHITE)
    icon("folder", cx + 54, cy + 42, 16); REG.draw(img, cx + 74, cy + 44, "C:\\", BLACK)
    top = cy + 64
else:
    top = cy
lh = ch - (top - cy) - 22
sunken(cx, top, cw, lh); rect(cx + 2, top + 2, cw - 4, lh - 4, WHITE)
files = (("folder", "docs"), ("folder", "pictures"), ("folder", "system"), ("text", "readme.txt"),
         ("text", "notes.txt"), ("picture", "sunset.png"), ("picture", "board.jpg"), ("text", "todo.txt"))
for i, (k, t) in enumerate(files):
    ix = cx + 14 + (i % 5) * 76
    iy = top + 10 + (i // 5) * 66
    icon(k, ix + 16, iy)
    REG.draw(img, ix + 32 - REG.width(t) // 2, iy + 36, t, BLACK)
sunken(cx, cy + ch - 20, cw - 120, 20); REG.draw(img, cx + 6, cy + ch - 16, "8 objects", BLACK)
sunken(cx + cw - 118, cy + ch - 20, 118, 20); REG.draw(img, cx + cw - 112, cy + ch - 16, "1.2 MB free", BLACK)

# Text editor window (active, in front)
cx, cy, cw, ch = window(250, 150, 360, 270, "notes.txt - Editor", True, ("File", "Edit", "Search", "Help"))
icon("text", 255, 156, 16)
sunken(cx, cy, cw, ch); rect(cx + 2, cy + 2, cw - 4, ch - 4, WHITE)
scrollbar(cx + cw - 18, cy + 2, ch - 4)
text = ["Shopping list for the VGA board", "", "  9 colour resistors: 510, 1k, 2k",
        "  2 sync resistors: 120", "  1 cut VGA cable (mind the shield!)", "",
        "640 x 480, 256 colours, 60 Hz.", "Screenshots travel over the serial port.", "",
        "TODO: window manager, fonts, a name..."]
for i, line in enumerate(text):
    REG.draw(img, cx + 6, cy + 5 + i * 13, line, BLACK)
d.line([(cx + 6 + REG.width(text[-1]) + 1, cy + 5 + 9 * 13), (cx + 6 + REG.width(text[-1]) + 1, cy + 16 + 9 * 13)], BLACK)

# Taskbar
ty = H - 28
rect(0, ty, W, 28, GREY); d.line([(0, ty), (W, ty)], GREY); d.line([(0, ty + 1), (W, ty + 1)], WHITE)
button(2, ty + 4, 58, 22)
MASCOT = [
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG", "GPPPGGLLLLGGPPPG",
    ".GGGLLLLLLLLGGG.", "..GLLLLLLLLLLG..", "..GLKLLLLLLKLG..", "..GLKLLLLLLKLG..", "..GLLLLNNLLLLG..",
    "D.GLLLLNNLLLLG.D", ".DDGLLLLLLLLGDD.", "D..GGLLLLLLGG..D", "....GGGGGGGG....", "................", "................"]
MK = {"G": DARK, "L": WHITE, "P": c(7, 4, 2), "K": BLACK, "N": c(7, 3, 2), "D": DARK}
for r, row in enumerate(MASCOT):
    for q, chh in enumerate(row):
        if chh in MK:
            px[5 + q, ty + 7 + r] = MK[chh]
BOLD.draw(img, 25, ty + 9, "Start", BLACK)
tx = 66
if STYLE == "98":
    d.line([(tx, ty + 5), (tx, ty + 24)], DARK); d.line([(tx + 1, ty + 5), (tx + 1, ty + 24)], WHITE)
    for i, k in enumerate(("folder", "text", "net")):
        icon(k, tx + 8 + i * 22, ty + 7, 16)
    tx += 78
    d.line([(tx - 4, ty + 5), (tx - 4, ty + 24)], DARK); d.line([(tx - 3, ty + 5), (tx - 3, ty + 24)], WHITE)
for t, k, act in (("Flash (C:)", "folder", False), ("notes.txt - Editor", "text", True)):
    button(tx, ty + 4, 150, 22, pressed=act)
    icon(k, tx + 4 + act, ty + 7 + act, 16)
    (BOLD if act else REG).draw(img, tx + 24 + act, ty + 9 + act, t, BLACK)
    tx += 153
edge(W - 62, ty + 4, 60, 22, DARK, WHITE)
REG.draw(img, W - 62 + (60 - REG.width("12:46 PM")) // 2, ty + 9, "12:46 PM", BLACK)

# Mouse pointer (the one already in the firmware)
PTR = ["X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ", "X.....X     ",
       "X......X    ", "X.......X   ", "X........X  ", "X.........X ", "X......XXXXX", "X...X..X    ", "X..X X..X   ",
       "X.X  X..X   ", "XX    X..X  ", "X     X..X  ", "       X..X ", "       XXX  "]
for r, row in enumerate(PTR):
    for q, chh in enumerate(row):
        if chh != " ":
            px[470 + q, 300 + r] = BLACK if chh == "X" else WHITE

assert set(img.get_flattened_data()) <= set(PALETTE), "a colour outside the board's 256"
img.save(sys.argv[2])
