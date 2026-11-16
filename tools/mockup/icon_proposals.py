#!/usr/bin/env python3
"""Proposals for refined icons, for the owner to choose from (backlog 29).

  python3 tools/mockup/icon_proposals.py <folder>

Every icon is drawn twice, at 32x32 and at 16x16, with the 256 colours of the
board (3 bits of red, 3 of green, 2 of blue). All original. The sheets show
each one at its real size, on the desktop and on window grey, and enlarged.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont


def c(r, g, b):
    return (r * 255 // 7, g * 255 // 7, b * 255 // 3, 255)


K = c(0, 0, 0)
W = c(7, 7, 3)
G = c(5, 5, 2)
D = c(2, 2, 1)
L = c(6, 6, 3)  # light grey
NAVY = c(0, 0, 2)
BLUE = c(1, 3, 3)
SKY = c(3, 5, 3)
PALE = c(5, 6, 3)
TEAL = c(1, 5, 3)
GREEN = c(1, 5, 0)
DGREEN = c(0, 3, 0)
LGREEN = c(4, 7, 1)
RED = c(7, 0, 0)
DRED = c(4, 0, 0)
YEL = c(7, 6, 0)
LYEL = c(7, 7, 2)
FOLD = c(7, 6, 1)
OCHRE = c(6, 5, 0)
BROWN = c(4, 2, 0)
WOOD = c(6, 4, 1)
PINK = c(7, 4, 2)
ROSE = c(7, 3, 2)
CREAM = c(7, 7, 2)
ORANGE = c(7, 4, 0)


class Icon:
    def __init__(self, size):
        self.size = size
        self.im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def box(self, x0, y0, x1, y1, fill, edge=K):
        self.d.rectangle([x0, y0, x1, y1], fill=fill, outline=edge)

    def fill(self, x0, y0, x1, y1, col):
        self.d.rectangle([x0, y0, x1, y1], fill=col)

    def line(self, pts, col):
        self.d.line(pts, fill=col)

    def poly(self, pts, fill, edge=K):
        self.d.polygon(pts, fill=fill, outline=edge)

    def dot(self, x, y, col):
        self.im.putpixel((x, y), col)

    def rows(self, x, y, rows, inks):
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch in inks:
                    self.im.putpixel((x + i, y + j), inks[ch])

    def ellipse(self, x0, y0, x1, y1, fill, edge=K):
        self.d.ellipse([x0, y0, x1, y1], fill=fill, outline=edge)


# ---- pieces used by several icons ---------------------------------------------

def page32(i, x0=7, y0=3, x1=24, y1=28, paper=W):
    """A sheet with a folded corner and a shadow down its right and bottom."""
    f = 5
    i.poly([(x0, y0), (x1 - f, y0), (x1, y0 + f), (x1, y1), (x0, y1)], paper)
    i.poly([(x1 - f, y0), (x1 - f, y0 + f), (x1, y0 + f)], L)
    i.line([(x1 + 1, y0 + f + 1), (x1 + 1, y1 + 1), (x0 + 1, y1 + 1)], D)


def page16(i, x0=3, y0=1, x1=12, y1=14, paper=W):
    f = 3
    i.poly([(x0, y0), (x1 - f, y0), (x1, y0 + f), (x1, y1), (x0, y1)], paper)
    i.poly([(x1 - f, y0), (x1 - f, y0 + f), (x1, y0 + f)], L)


def pencil32(i, tip=(11, 27), length=20):
    """A pencil lying at 45 degrees, its point down and to the left."""
    tx, ty = tip
    for k in range(length):
        x, y = tx + 2 + k, ty - 2 - k
        if k < 3:
            body, side = WOOD, WOOD
        elif k >= length - 3:
            body, side = PINK, ROSE
        elif k >= length - 5:
            body, side = G, D
        else:
            body, side = YEL, OCHRE
        for dx, dy, col in ((-2, -2, K), (-1, -1, LYEL if body == YEL else body), (0, 0, body), (1, 1, side), (2, 2, K)):
            px, py = x + dx, y + dy
            if 0 <= px < i.size and 0 <= py < i.size:
                i.dot(px, py, col)
            px += 1  # the diagonal needs two pixels a step, or it has holes
            if 0 <= px < i.size and 0 <= py < i.size:
                i.dot(px, py, col)
    # the point
    for x, y, col in ((tx, ty, K), (tx + 1, ty, K), (tx, ty - 1, K), (tx + 1, ty - 1, K), (tx + 2, ty, K),
                      (tx, ty - 2, K), (tx + 2, ty - 1, WOOD), (tx + 1, ty - 2, WOOD), (tx + 3, ty, K),
                      (tx, ty - 3, K)):
        if 0 <= x < i.size and 0 <= y < i.size:
            i.dot(x, y, col)


def pencil16(i, tip=(4, 14), length=10):
    tx, ty = tip
    for k in range(length):
        x, y = tx + 1 + k, ty - 1 - k
        body = WOOD if k < 2 else PINK if k >= length - 2 else YEL
        for dx, dy, col in ((-1, -1, K), (0, 0, body), (1, 1, K)):
            for e in (0, 1):
                px, py = x + dx + e, y + dy
                if 0 <= px < 16 and 0 <= py < 16:
                    i.dot(px, py, col)
    i.dot(tx, ty, K)
    i.dot(tx + 1, ty, K)
    i.dot(tx, ty - 1, K)


def folder32(i, y0=7):
    i.poly([(3, y0 + 3), (3, y0 + 1), (5, y0 - 1), (12, y0 - 1), (14, y0 + 1), (14, y0 + 3)], OCHRE)
    i.box(2, y0 + 3, 28, 26, FOLD)
    i.line([(3, y0 + 4), (27, y0 + 4)], LYEL)
    i.line([(3, y0 + 4), (3, 25)], LYEL)
    i.line([(27, y0 + 5), (27, 25), (4, 25)], OCHRE)
    i.line([(29, y0 + 5), (29, 27), (4, 27)], D)


def folder16(i):
    i.poly([(1, 5), (1, 4), (2, 3), (6, 3), (7, 4), (7, 5)], OCHRE)
    i.box(1, 5, 14, 13, FOLD)
    i.line([(2, 6), (13, 6)], LYEL)
    i.line([(13, 7), (13, 12), (2, 12)], OCHRE)


def monitor32(i, x0, y0, w, h, screen):
    i.box(x0, y0, x0 + w, y0 + h, G)
    i.line([(x0 + 1, y0 + 1), (x0 + w - 1, y0 + 1)], W)
    i.line([(x0 + 1, y0 + 1), (x0 + 1, y0 + h - 1)], W)
    i.line([(x0 + w - 1, y0 + 2), (x0 + w - 1, y0 + h - 1), (x0 + 2, y0 + h - 1)], D)
    i.box(x0 + 3, y0 + 3, x0 + w - 3, y0 + h - 4, screen)


def stand32(i, cx, y, half=5):
    i.box(cx - 2, y, cx + 2, y + 2, G)
    i.box(cx - half, y + 2, cx + half, y + 4, G)
    i.line([(cx - half + 1, y + 3), (cx + half - 1, y + 3)], W)


# ---- Editor: three ideas -------------------------------------------------------

def editor_a32():
    """A spiral notepad and a pencil."""
    i = Icon(32)
    i.box(5, 5, 22, 29, W)
    i.line([(23, 7), (23, 30), (7, 30)], D)
    i.fill(6, 6, 21, 8, c(1, 3, 3))
    for x in range(7, 22, 3):
        i.fill(x, 3, x, 7, K)
        i.dot(x + 1, 4, G)
    for y in (12, 15, 18, 21, 24):
        i.line([(8, y), (19, y)], SKY)
    i.line([(9, 10), (9, 27)], ROSE)
    pencil32(i, tip=(13, 26), length=17)
    return i


def editor_a16():
    i = Icon(16)
    i.box(2, 3, 11, 14, W)
    i.fill(3, 4, 10, 4, BLUE)
    for x in (3, 5, 7, 9):
        i.fill(x, 1, x, 3, K)
    for y in (7, 9, 11):
        i.line([(4, y), (9, y)], SKY)
    pencil16(i, tip=(7, 12), length=8)
    return i


def editor_b32():
    """A written sheet with a big capital and a pencil across it."""
    i = Icon(32)
    page32(i, 4, 3, 22, 28)
    i.rows(7, 6, ["..NN..", ".NNNN.", ".N..N.", "NN..NN", "NNNNNN", "NN..NN", "NN..NN"], {"N": NAVY})
    for y, x1 in ((16, 19), (19, 19), (22, 17), (25, 13)):
        i.line([(7, y), (x1, y)], D)
    i.line([(14, 9), (16, 9)], D)
    i.line([(14, 12), (19, 12)], D)
    pencil32(i, tip=(15, 25), length=15)
    return i


def editor_b16():
    i = Icon(16)
    page16(i, 2, 1, 11, 14)
    i.rows(4, 3, [".NN.", "N..N", "NNNN", "N..N"], {"N": NAVY})
    for y in (9, 11):
        i.line([(4, y), (9, y)], D)
    pencil16(i, tip=(7, 13), length=8)
    return i


def editor_c32():
    """A quill and an inkwell on a sheet."""
    i = Icon(32)
    page32(i, 3, 6, 21, 29, CREAM)
    for y in (13, 16, 19, 22):
        i.line([(6, y), (16, y)], D)
    # inkwell
    i.box(20, 21, 29, 29, NAVY)
    i.box(22, 18, 27, 21, BLUE)
    i.line([(21, 23), (21, 27)], BLUE)
    # feather: a leaf from the top right down to the nib
    i.poly([(29, 1), (30, 5), (27, 11), (22, 16), (19, 17), (20, 13), (24, 6)], W)
    i.line([(28, 3), (20, 16)], G)
    for k in range(4):
        i.line([(26 - 2 * k, 6 + 2 * k), (28 - 2 * k, 7 + 2 * k)], PALE)
    i.line([(20, 16), (14, 24)], K)
    i.line([(19, 16), (13, 24)], K)
    i.dot(13, 25, K)
    return i


def editor_c16():
    i = Icon(16)
    page16(i, 1, 3, 10, 14, CREAM)
    for y in (8, 10, 12):
        i.line([(3, y), (7, y)], D)
    i.poly([(14, 0), (15, 3), (12, 7), (10, 8), (10, 6)], W)
    i.line([(10, 8), (6, 13)], K)
    i.box(11, 10, 15, 14, NAVY)
    i.fill(12, 9, 14, 9, K)
    return i


# ---- Files and folders ---------------------------------------------------------

def folder_icon32():
    i = Icon(32)
    folder32(i)
    return i


def folder_icon16():
    i = Icon(16)
    folder16(i)
    return i


def files32():
    """File manager: an open folder with papers in it."""
    i = Icon(32)
    i.poly([(3, 10), (3, 8), (5, 6), (12, 6), (14, 8), (14, 10)], OCHRE)
    i.box(2, 10, 26, 26, OCHRE)
    i.box(6, 5, 20, 20, W)
    for y in (8, 11, 14):
        i.line([(8, y), (17, y)], D)
    i.box(10, 8, 24, 20, W)
    for y in (11, 14):
        i.line([(12, y), (21, y)], SKY)
    i.poly([(2, 26), (6, 15), (30, 15), (26, 26)], FOLD)
    i.line([(7, 16), (28, 16)], LYEL)
    i.line([(4, 27), (27, 27)], D)
    return i


def files16():
    i = Icon(16)
    i.poly([(1, 5), (1, 4), (2, 3), (5, 3), (6, 4), (6, 5)], OCHRE)
    i.box(1, 5, 12, 13, OCHRE)
    i.box(4, 2, 11, 9, W)
    i.line([(6, 4), (9, 4)], D)
    i.line([(6, 6), (9, 6)], D)
    i.poly([(1, 13), (3, 8), (15, 8), (13, 13)], FOLD)
    return i


def text32():
    i = Icon(32)
    page32(i)
    for y, x1 in ((9, 16), (12, 21), (15, 21), (18, 21), (21, 21), (24, 15)):
        i.line([(10, y), (x1, y)], D)
    return i


def text16():
    i = Icon(16)
    page16(i)
    for y, x1 in ((5, 7), (7, 10), (9, 10), (11, 8)):
        i.line([(5, y), (x1, y)], D)
    return i


def blank32():
    i = Icon(32)
    page32(i)
    return i


def landscape(i, x0, y0, x1, y1):
    """Sky, a sun, two hills."""
    i.fill(x0, y0, x1, y1, SKY)
    w, h = x1 - x0, y1 - y0
    i.ellipse(x1 - w // 3 - 1, y0 + 1, x1 - 2, y0 + w // 3, YEL, YEL)
    i.d.polygon([(x0, y1), (x0 + w * 2 // 5, y0 + h * 2 // 5), (x0 + w * 4 // 5, y1)], fill=DGREEN)
    i.d.polygon([(x0 + w // 3, y1), (x0 + w * 7 // 10, y0 + h * 3 // 5), (x1, y1 - 1), (x1, y1)], fill=GREEN)


def picture32():
    """A picture file: a snapshot with a white border."""
    i = Icon(32)
    i.box(3, 5, 28, 26, W)
    i.line([(29, 7), (29, 27), (5, 27)], D)
    landscape(i, 6, 8, 25, 23)
    return i


def picture16():
    i = Icon(16)
    i.box(1, 2, 14, 13, W)
    landscape(i, 3, 4, 12, 11)
    return i


def viewer32():
    """Picture viewer: a painting on an easel."""
    i = Icon(32)
    i.line([(9, 24), (5, 30)], BROWN)
    i.line([(10, 24), (6, 30)], K)
    i.line([(22, 24), (26, 30)], BROWN)
    i.line([(21, 24), (25, 30)], K)
    i.fill(15, 1, 16, 4, BROWN)
    i.box(4, 4, 27, 23, WOOD)
    i.line([(5, 5), (26, 5)], LYEL)
    i.line([(26, 6), (26, 22), (5, 22)], BROWN)
    i.box(7, 7, 24, 20, SKY)
    landscape(i, 8, 8, 23, 19)
    i.box(3, 23, 28, 25, BROWN)
    return i


def viewer16():
    i = Icon(16)
    i.line([(4, 12), (2, 15)], K)
    i.line([(11, 12), (13, 15)], K)
    i.box(1, 1, 14, 12, WOOD)
    landscape(i, 3, 3, 12, 10)
    return i


# ---- System programs -----------------------------------------------------------

def window32(i, x0, y0, x1, y1, face=G):
    i.box(x0, y0, x1, y1, face)
    i.fill(x0 + 1, y0 + 1, x1 - 1, y0 + 4, NAVY)
    i.fill(x0 + (x1 - x0) // 2, y0 + 1, x1 - 1, y0 + 4, BLUE)
    i.fill(x1 - 4, y0 + 2, x1 - 2, y0 + 3, G)
    i.line([(x1 + 1, y0 + 2), (x1 + 1, y1 + 1), (x0 + 2, y1 + 1)], D)


def tasks32():
    """Task Manager: a window with a load graph."""
    i = Icon(32)
    window32(i, 2, 4, 28, 26)
    i.box(4, 11, 26, 24, K)
    for x in range(8, 26, 4):
        i.line([(x, 12), (x, 23)], DGREEN)
    for y in (15, 19):
        i.line([(5, y), (25, y)], DGREEN)
    i.line([(5, 21), (8, 21), (10, 17), (12, 20), (15, 13), (17, 19), (20, 16), (22, 21), (25, 18)], LGREEN)
    return i


def tasks16():
    i = Icon(16)
    i.box(1, 2, 14, 13, G)
    i.fill(2, 3, 13, 4, NAVY)
    i.box(2, 6, 13, 12, K)
    i.line([(3, 10), (5, 10), (7, 7), (9, 10), (10, 8), (12, 10)], LGREEN)
    return i


def drive32(i, y0):
    i.box(2, y0, 29, y0 + 10, G)
    i.line([(3, y0 + 1), (28, y0 + 1)], W)
    i.line([(3, y0 + 1), (3, y0 + 9)], W)
    i.line([(28, y0 + 2), (28, y0 + 9), (4, y0 + 9)], D)
    i.line([(6, y0 + 5), (19, y0 + 5)], D)
    i.line([(6, y0 + 6), (19, y0 + 6)], W)
    i.fill(23, y0 + 5, 25, y0 + 6, LGREEN)
    i.line([(4, y0 + 11), (30, y0 + 11), (30, y0 + 2)], D)


def defrag32():
    """Defragmenter: scattered blocks falling into a tidy row on the drive."""
    i = Icon(32)
    drive32(i, 18)
    for x, y, col in ((4, 2, RED), (15, 1, BLUE), (24, 4, RED), (9, 6, BLUE)):
        i.box(x, y, x + 4, y + 4, col)
    for k, col in enumerate((BLUE, BLUE, BLUE, RED, RED)):
        i.box(4 + k * 5, 12, 8 + k * 5, 16, col)
    return i


def defrag16():
    i = Icon(16)
    i.box(1, 10, 14, 14, G)
    i.line([(2, 11), (13, 11)], W)
    i.dot(12, 12, LGREEN)
    for k, col in enumerate((BLUE, BLUE, RED, RED)):
        i.box(1 + k * 3, 6, 4 + k * 3, 9, col)
    i.box(3, 1, 6, 4, RED)
    i.box(10, 0, 13, 3, BLUE)
    return i


def settings32():
    """Settings: a panel with three sliders."""
    i = Icon(32)
    i.box(3, 3, 27, 27, G)
    i.line([(4, 4), (26, 4)], W)
    i.line([(4, 4), (4, 26)], W)
    i.line([(26, 5), (26, 26), (5, 26)], D)
    i.line([(28, 5), (28, 28), (5, 28)], D)
    for x, knob, col in ((9, 9, RED), (15, 17, GREEN), (21, 12, BLUE)):
        i.fill(x, 7, x + 1, 23, K)
        i.dot(x + 1, 7, W)
        i.box(x - 2, knob, x + 3, knob + 4, col)
        i.line([(x - 1, knob + 1), (x + 2, knob + 1)], W)
    return i


def settings16():
    i = Icon(16)
    i.box(1, 1, 14, 14, G)
    i.line([(2, 2), (13, 2)], W)
    for x, knob, col in ((4, 4, RED), (8, 9, GREEN), (11, 6, BLUE)):
        i.line([(x, 3), (x, 12)], K)
        i.fill(x - 1, knob, x + 1, knob + 2, col)
    return i


GEAR32 = [
    "............KKKKKK..............",
    "............KLGGDK..............",
    ".....KKK....KLGGDK....KKK.......",
    "....KLGGK..KKLGGDKK..KGGDK......",
    "...KLGGGGKKLLGGGGDDKKGGGGDK.....",
    "...KGGGGGGGGGGGGGGGGGGGGGGK.....",
    "....KGGGGGGGGGGGGGGGGGGGGK......",
    ".....KGGGGGGGKKKKKKGGGGGK.......",
    "....KLGGGGGKK......KKGGGDK......",
    "...KLGGGGGK..........KGGGDK.....",
    "...KLGGGGK............KGGDK.....",
    "KKKKLGGGK..............KGDKKKK..",
    "KLLLGGGGK..............KGGDDDK..",
    "KGGGGGGK................KGGGGK..",
    "KGGGGGGK................KGGGGK..",
    "KGGGGGGK................KGGGGK..",
    "KDDDGGGGK..............KGGDDDK..",
    "KKKKLGGGK..............KGDKKKK..",
    "...KLGGGGK............KGGDK.....",
    "...KLGGGGGK..........KGGGDK.....",
    "....KLGGGGGKK......KKGGGDK......",
    ".....KGGGGGGGKKKKKKGGGGGK.......",
    "....KGGGGGGGGGGGGGGGGGGGGK......",
    "...KGGGGGGGGGGGGGGGGGGGGGGK.....",
    "...KDGGGGKKDDGGGGDDKKGGGGDK.....",
    "....KDGGK..KKLGGDKK..KGDDK......",
    ".....KKK....KLGGDK....KKK.......",
    "............KDDDDK..............",
    "............KKKKKK..............",
]


def service32():
    """A process with no window (desktop, netd, vncd, httpd, dbgd): a cog."""
    i = Icon(32)
    i.rows(1, 1, GEAR32, {"K": K, "G": G, "L": W, "D": D})
    return i


GEAR16 = [
    "......KKKK......",
    "..KK..KGGK..KK..",
    ".KGGKKKGGKKKGGK.",
    ".KGGGGGGGGGGGGK.",
    "..KGGGKKKKGGGK..",
    "..KGGK....KGGK..",
    "KKKGK......KGKKK",
    "KGGGK......KGGGK",
    "KGGGK......KGGGK",
    "KKKGK......KGKKK",
    "..KGGK....KGGK..",
    "..KGGGKKKKGGGK..",
    ".KGGGGGGGGGGGGK.",
    ".KGGKKKGGKKKGGK.",
    "..KK..KGGK..KK..",
    "......KKKK......",
]


def service16():
    i = Icon(16)
    i.rows(0, 0, GEAR16, {"K": K, "G": G})
    return i


def network32():
    """Network: two machines joined by a cable."""
    i = Icon(32)
    monitor32(i, 1, 2, 16, 13, TEAL)
    stand32(i, 9, 15, 4)
    monitor32(i, 14, 12, 16, 13, TEAL)
    stand32(i, 22, 25, 4)
    i.line([(3, 21), (3, 29), (12, 29)], K)
    i.line([(4, 21), (4, 28), (12, 28)], YEL)
    i.line([(5, 6), (9, 6)], W)
    i.line([(18, 16), (22, 16)], W)
    return i


def network16():
    i = Icon(16)
    i.box(0, 1, 8, 7, G)
    i.fill(2, 3, 6, 5, TEAL)
    i.fill(3, 8, 5, 8, K)
    i.box(7, 7, 15, 13, G)
    i.fill(9, 9, 13, 11, TEAL)
    i.fill(9, 14, 13, 14, K)
    i.line([(2, 9), (2, 12), (6, 12)], K)
    return i


def saver32():
    """Screen Saver: a screen showing the night sky."""
    i = Icon(32)
    monitor32(i, 2, 2, 27, 20, K)
    stand32(i, 15, 22, 7)
    for x, y in ((8, 8), (20, 7), (12, 15), (23, 14), (16, 10), (7, 13)):
        i.dot(x, y, W)
    i.rows(17, 12, [".Y.", "YYY", ".Y."], {"Y": YEL})
    i.ellipse(8, 7, 13, 12, CREAM, CREAM)
    i.ellipse(10, 6, 15, 11, K, K)
    return i


def saver16():
    i = Icon(16)
    i.box(1, 1, 14, 11, G)
    i.fill(3, 3, 12, 9, K)
    i.fill(6, 12, 9, 12, K)
    i.box(4, 13, 11, 14, G)
    for x, y in ((5, 4), (10, 5), (7, 8), (11, 8)):
        i.dot(x, y, W)
    i.rows(7, 4, [".Y.", "YYY", ".Y."], {"Y": YEL})
    return i


def display32():
    """Display: a screen showing a desktop with a patterned background."""
    i = Icon(32)
    monitor32(i, 2, 2, 27, 20, BLUE)
    stand32(i, 15, 22, 7)
    for y in range(6, 18, 2):
        for x in range(6 + (y // 2) % 2 * 2, 26, 4):
            i.dot(x, y, PALE)
    i.box(9, 8, 19, 15, W)
    i.fill(10, 9, 18, 10, NAVY)
    i.fill(6, 17, 25, 18, G)
    return i


def display16():
    i = Icon(16)
    i.box(1, 1, 14, 11, G)
    i.fill(3, 3, 12, 9, BLUE)
    for x, y in ((4, 4), (8, 4), (11, 5), (6, 6), (10, 7), (4, 8), (8, 8)):
        i.dot(x, y, PALE)
    i.fill(6, 12, 9, 12, K)
    i.box(4, 13, 11, 14, G)
    return i


def power32():
    """Shut Down: the power sign on a red key."""
    i = Icon(32)
    i.ellipse(3, 3, 28, 28, RED)
    i.d.arc([5, 5, 26, 26], 180, 300, fill=PINK)
    i.d.arc([5, 5, 26, 26], 0, 110, fill=DRED)
    i.d.arc([9, 9, 22, 22], -50, 230, fill=W, width=2)
    i.fill(15, 7, 16, 15, W)
    return i


def power16():
    i = Icon(16)
    i.ellipse(1, 1, 14, 14, RED)
    i.d.arc([4, 4, 11, 11], -45, 225, fill=W)
    i.fill(7, 3, 8, 7, W)
    return i


def gallery32():
    """Widget gallery: a window with a button, a tick box and a slider."""
    i = Icon(32)
    window32(i, 2, 3, 28, 27)
    i.box(5, 11, 11, 17, W)
    i.rows(6, 12, ["....K", "...KK", "K.KK.", "KKK..", ".K..."], {"K": K})
    i.line([(14, 13), (25, 13)], D)
    i.line([(14, 15), (22, 15)], D)
    i.box(5, 20, 16, 25, G)
    i.line([(6, 21), (15, 21)], W)
    i.line([(15, 22), (15, 24), (7, 24)], D)
    i.ellipse(19, 20, 24, 25, W)
    i.fill(21, 22, 22, 23, K)
    return i


def gallery16():
    i = Icon(16)
    i.box(1, 1, 14, 14, G)
    i.fill(2, 2, 13, 3, NAVY)
    i.box(3, 5, 7, 9, W)
    i.rows(4, 6, ["..K", "K.K", ".K."], {"K": K})
    i.line([(9, 6), (12, 6)], D)
    i.line([(9, 8), (12, 8)], D)
    i.box(3, 10, 12, 13, G)
    i.line([(4, 11), (11, 11)], W)
    return i


def welcome32():
    """Welcome: an open book."""
    i = Icon(32)
    i.poly([(1, 8), (15, 10), (15, 27), (1, 25)], BLUE)
    i.poly([(30, 8), (16, 10), (16, 27), (30, 25)], BLUE)
    i.poly([(3, 6), (15, 9), (15, 25), (3, 22)], W)
    i.poly([(28, 6), (16, 9), (16, 25), (28, 22)], W)
    for k in range(4):
        i.line([(5, 10 + k * 3), (13, 12 + k * 3)], D)
        i.line([(26, 10 + k * 3), (18, 12 + k * 3)], D)
    i.fill(20, 3, 22, 12, RED)
    i.dot(21, 12, W)
    return i


def welcome16():
    i = Icon(16)
    i.poly([(0, 4), (7, 5), (7, 14), (0, 13)], BLUE)
    i.poly([(15, 4), (8, 5), (8, 14), (15, 13)], BLUE)
    i.poly([(1, 3), (7, 4), (7, 12), (1, 11)], W)
    i.poly([(14, 3), (8, 4), (8, 12), (14, 11)], W)
    for y in (6, 8):
        i.line([(3, y), (5, y)], D)
        i.line([(10, y), (12, y)], D)
    return i


def run32():
    """Run...: a window with a prompt."""
    i = Icon(32)
    window32(i, 2, 5, 28, 25)
    i.box(4, 12, 26, 23, K)
    i.rows(6, 14, ["W....", ".W...", "..W..", ".W...", "W...."], {"W": W})
    i.line([(13, 19), (18, 19)], W)
    return i


def run16():
    i = Icon(16)
    i.box(1, 2, 14, 13, G)
    i.fill(2, 3, 13, 4, NAVY)
    i.box(2, 6, 13, 12, K)
    i.rows(4, 7, ["W..", ".W.", "W.."], {"W": W})
    i.line([(8, 10), (11, 10)], W)
    return i


# ---- Games and the mascot ------------------------------------------------------

def snake32():
    """Culebra: the snake coiled, with the apple."""
    i = Icon(32)
    body = [(6, 26), (26, 26), (26, 18), (8, 18), (8, 10), (20, 10)]
    for a, b in zip(body, body[1:]):
        x0, x1 = sorted((a[0], b[0]))
        y0, y1 = sorted((a[1], b[1]))
        i.box(x0 - 2, y0 - 2, x1 + 2, y1 + 2, GREEN, DGREEN)
    for a, b in zip(body, body[1:]):
        x0, x1 = sorted((a[0], b[0]))
        y0, y1 = sorted((a[1], b[1]))
        i.fill(x0 - 1, y0 - 1, x1 + 1, y1 + 1, GREEN)
        if y0 == y1:
            i.line([(x0, y0 - 1), (x1, y0 - 1)], LGREEN)
            for x in range(x0 + 1, x1, 4):
                i.fill(x, y0, x + 1, y0 + 1, DGREEN)
        else:
            i.line([(x0 - 1, y0), (x0 - 1, y1)], LGREEN)
    i.box(18, 7, 25, 13, GREEN, DGREEN)
    i.line([(19, 8), (24, 8)], LGREEN)
    i.fill(22, 9, 23, 10, W)
    i.dot(23, 10, K)
    i.line([(26, 11), (28, 11)], RED)
    i.dot(29, 10, RED)
    i.dot(29, 12, RED)
    i.ellipse(24, 1, 29, 6, RED, DRED)
    i.dot(26, 0, DGREEN)
    i.dot(25, 2, PINK)
    return i


def snake16():
    i = Icon(16)
    for x0, y0, x1, y1 in ((2, 12, 13, 14), (11, 8, 13, 13), (3, 8, 12, 10), (3, 4, 5, 9), (4, 4, 10, 6)):
        i.box(x0, y0, x1, y1, GREEN, DGREEN)
    for x0, y0, x1, y1 in ((3, 13, 12, 13), (12, 9, 12, 13), (4, 9, 12, 9), (4, 5, 4, 9), (4, 5, 9, 5)):
        i.fill(x0, y0, x1, y1, GREEN)
    i.dot(9, 5, W)
    i.line([(11, 5), (12, 5)], RED)
    i.box(12, 0, 15, 3, RED, DRED)
    return i


CACTUS32 = [
    "..............KKKK..............",
    ".............KLGGGK.............",
    "............KLGGGGDK............",
    "............KLGGGGDK............",
    "............KLGGWGDK............",
    "............KLGGGGDK....KKK.....",
    "....KKK.....KLGGGGDK...KLGDK....",
    "...KLGDK....KLGWGGDK...KLGDK....",
    "...KLGDK....KLGGGGDK...KLGDK....",
    "...KLGDK....KLGGGGDK...KLGDK....",
    "...KLGDK....KLGGGGDKKKKKGGDK....",
    "...KLGDK....KLGGWGGGGGGGGGDK....",
    "...KLGGKKKKKKLGGGGGGGGGGGDK.....",
    "...KLGGGGGGGGGGGGGDKKKKKKK......",
    "....KLGGGGGGGGWGGGDK............",
    ".....KKKKKKKKLGGGGDK............",
    "............KLGGGGDK............",
    "............KLGGGGDK............",
    "............KLGWGGDK............",
    "............KLGGGGDK............",
    ".......KKKKKKKKKKKKKKKKKK.......",
    ".......KOOOOOOOOOOOOOOOOK.......",
    ".......KKKKKKKKKKKKKKKKKK.......",
    "........KPOOOOOOOOOOOOBK........",
    "........KPOOOOOOOOOOOOBK........",
    ".........KPOOOOOOOOOOBK.........",
    ".........KPOOOOOOOOOOBK.........",
    "..........KPOOOOOOOOBK..........",
    "..........KKKKKKKKKKKK..........",
]


def cactus32():
    """Cactus Patch: a cactus in its pot."""
    i = Icon(32)
    i.rows(0, 2, CACTUS32, {"K": K, "G": GREEN, "L": LGREEN, "D": DGREEN, "W": W, "O": ORANGE, "P": WOOD, "B": BROWN})
    i.ellipse(13, 0, 17, 4, ROSE, DRED)
    return i


CACTUS16 = [
    "......KKK.......",
    ".....KRRRK......",
    "......KKKK......",
    "......KLGK......",
    "......KLGK.KK...",
    "..KK..KLGK.KGK..",
    ".KLGK.KLGK.KGK..",
    ".KLGK.KLGKKKGK..",
    ".KLGKKKLGGGGK...",
    ".KLGGGGLGKKK....",
    "..KKKKKLGK......",
    "....KKKKKKKK....",
    "....KOOOOOOK....",
    ".....KOOOOK.....",
    ".....KOOOOK.....",
    ".....KKKKKK.....",
]


def cactus16():
    i = Icon(16)
    i.rows(0, 0, CACTUS16, {"K": K, "G": GREEN, "L": LGREEN, "R": ROSE, "O": ORANGE})
    return i


MASCOT32 = [
    "................................",
    "...KKKKK................KKKKK...",
    "..KGGGGGK..............KGGGGGK..",
    ".KGPPPPPGK............KGPPPPPGK.",
    ".KGPPPPPPGK..KKKKKK..KGPPPPPPGK.",
    ".KGPPPPPPGKKKWWWWWWKKKGPPPPPPGK.",
    ".KGPPPPPGKWWWWWWWWWWWWKGPPPPPGK.",
    ".KGGPPPGKWWWWWWWWWWWWWWKGPPPGGK.",
    "..KGGGGKWWWWWWWWWWWWWWWWKGGGGK..",
    "...KKKKWWWWWWWWWWWWWWWWWWKKKK...",
    "....KLWWWWWWWWWWWWWWWWWWWWLK....",
    "....KLWWWWWWWWWWWWWWWWWWWWLK....",
    "....KLWWKKKWWWWWWWWWWKKKWWLK....",
    "....KLWKKWKKWWWWWWWWKKWKKWLK....",
    "....KLWKKKKKWWWWWWWWKKKKKWLK....",
    "....KLWWKKKWWWWWWWWWWKKKWWLK....",
    "....KLWWWWWWWWWNNWWWWWWWWWLK....",
    "DD..KLWWWWWWWWNNNNWWWWWWWWLK..DD",
    "..DDKLLWWWWWWWWNNWWWWWWWWLLKDD..",
    "....DKLWWWWWWWWKKWWWWWWWWLKD....",
    "..DDDKLLWWWWWKKWWKKWWWWWLLKDDD..",
    "DD....KLLWWWWWWWWWWWWWWLLK....DD",
    ".......KLLLWWWWWWWWWWLLLK.......",
    "........KKLLLLLLLLLLLLKK........",
    "..........KKKKKKKKKKKK..........",
]


def mascot32():
    """About Chinchilla: the mascot drawn for 32x32, not the small one doubled."""
    i = Icon(32)
    i.rows(0, 3, MASCOT32, {"K": K, "G": G, "P": PINK, "W": W, "L": L, "N": ROSE, "D": D})
    return i


# ---- The sheets ---------------------------------------------------------------

DESK = (36, 109, 170, 255)
FACE = G
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def cell(big, small, label, note=""):
    """One proposal: 32 and 16 at real size and enlarged, with its name."""
    w, h = 300, 190
    im = Image.new("RGBA", (w, h), (250, 250, 247, 255))
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, w - 1, h - 1], outline=(200, 200, 195, 255))
    d.text((10, 6), label, font=ImageFont.truetype(BOLD, 14), fill=(20, 20, 20))
    if note:
        d.text((10, 25), note, font=ImageFont.truetype(FONT, 11), fill=(90, 90, 90))
    # 32: enlarged four times on the desktop colour, then real size
    d.rectangle([10, 44, 10 + 135, 44 + 135], fill=DESK)
    if big is not None:
        im.alpha_composite(big.im.resize((128, 128), Image.NEAREST), (14, 48))
        d.rectangle([154, 44, 154 + 39, 44 + 39], fill=DESK)
        im.alpha_composite(big.im, (158, 48))
        d.rectangle([154, 88, 154 + 39, 88 + 39], fill=(255, 255, 255, 255))
        im.alpha_composite(big.im, (158, 92))
    else:
        d.text((40, 100), "(no 32x32)", font=ImageFont.truetype(FONT, 11), fill=(230, 230, 230))
    if small is not None:
        d.rectangle([202, 44, 202 + 87, 44 + 87], fill=FACE)
        im.alpha_composite(small.im.resize((80, 80), Image.NEAREST), (206, 48))
        d.rectangle([202, 136, 202 + 23, 136 + 23], fill=FACE)
        im.alpha_composite(small.im, (206, 140))
        d.rectangle([230, 136, 230 + 23, 136 + 23], fill=(0, 0, 128, 255))
        im.alpha_composite(small.im, (234, 140))
        d.rectangle([258, 136, 258 + 23, 136 + 23], fill=(255, 255, 255, 255))
        im.alpha_composite(small.im, (262, 140))
    return im


def sheet(title, cells, columns, path):
    rows = (len(cells) + columns - 1) // columns
    w, h = columns * 310 + 10, rows * 200 + 50
    im = Image.new("RGBA", (w, h), (255, 255, 255, 255))
    d = ImageDraw.Draw(im)
    d.text((12, 12), title, font=ImageFont.truetype(BOLD, 18), fill=(0, 0, 0))
    for n, one in enumerate(cells):
        im.alpha_composite(one, (10 + (n % columns) * 310, 46 + (n // columns) * 200))
    im.convert("RGB").save(path)
    print("wrote", path)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    sheet("Editor: tres ideas", [
        cell(editor_a32(), editor_a16(), "A. Bloc y lápiz", "bloc de espiral con un lápiz"),
        cell(editor_b32(), editor_b16(), "B. Hoja escrita y lápiz", "una hoja con texto y una capital"),
        cell(editor_c32(), editor_c16(), "C. Pluma y tintero", "pluma de ave sobre papel crema"),
    ], 3, os.path.join(out, "editor-options.png"))
    sheet("Programas", [
        cell(files32(), files16(), "File manager", "carpeta abierta con papeles"),
        cell(viewer32(), viewer16(), "Picture viewer", "un cuadro en un caballete"),
        cell(tasks32(), tasks16(), "Task Manager", "ventana con gráfica de carga"),
        cell(defrag32(), defrag16(), "Defragmenter", "bloques que se ordenan sobre el disco"),
        cell(snake32(), snake16(), "Culebra", "la serpiente y la manzana"),
        cell(cactus32(), cactus16(), "Cactus Patch", "un cactus en su maceta"),
        cell(network32(), network16(), "Network", "dos equipos unidos por un cable"),
        cell(saver32(), saver16(), "Screen Saver", "pantalla con cielo de noche"),
        cell(display32(), display16(), "Display", "pantalla con un escritorio"),
        cell(settings32(), settings16(), "Settings", "panel con tres mandos"),
        cell(welcome32(), welcome16(), "Welcome", "un libro abierto"),
        cell(gallery32(), gallery16(), "Widget gallery", "ventana con controles"),
        cell(mascot32(), None, "About Chinchilla", "la mascota dibujada a 32x32"),
        cell(power32(), power16(), "Shut Down", "tecla roja de apagado"),
        cell(run32(), run16(), "Run...", "ventana con un símbolo de órdenes"),
        cell(service32(), service16(), "Procesos sin ventana", "desktop, netd, vncd, httpd, dbgd"),
    ], 3, os.path.join(out, "programs.png"))
    sheet("Archivos y carpetas", [
        cell(folder_icon32(), folder_icon16(), "Carpeta", "con luz y sombra"),
        cell(text32(), text16(), "Archivo de texto", "ya no es el icono del Editor"),
        cell(picture32(), picture16(), "Archivo de imagen", "una foto con borde blanco"),
    ], 3, os.path.join(out, "files.png"))


# ---- The chosen icons as C++ for core/src/gfx/art.cpp -------------------------
# python3 tools/mockup/icon_proposals.py --cpp   prints the generated block.

CHOSEN = [  # accessor name, 32x32, 16x16
    ("editor_icon", editor_a32, editor_a16),
    ("folder_icon", folder_icon32, folder_icon16),
    ("files_icon", files32, files16),
    ("text_icon", text32, text16),
    ("picture_icon", picture32, picture16),
    ("viewer_icon", viewer32, viewer16),
    ("tasks_icon", tasks32, tasks16),
    ("defrag_icon", defrag32, defrag16),
    ("snake_icon", snake32, snake16),
    ("cactus_icon", cactus32, cactus16),
    ("network_icon", network32, network16),
    ("saver_icon", saver32, saver16),
    ("display_icon", display32, display16),
    ("settings_icon", settings32, settings16),
    ("welcome_icon", welcome32, welcome16),
    ("gallery_icon", gallery32, gallery16),
    ("power_icon", power32, power16),
    ("run_icon", run32, run16),
    ("service_icon", service32, service16),
    ("mascot", mascot32, None),
]
KEYS = "KWBSRYOPGDLNTHCEFIJMQUVXZabcdefghij"


def cpp_one(name, icon):
    s = icon.size
    inks, rows = {}, []
    for y in range(s):
        row = ""
        for x in range(s):
            p = icon.im.getpixel((x, y))
            if p[3] == 0:
                row += "."
                continue
            if p not in inks:
                inks[p] = KEYS[len(inks)]
            row += inks[p]
        rows.append(row)
    per = 2 if s == 32 else 4
    out = "const Bitmap &%s_%d()\n{\n    static constexpr const char *rows[%d] = {\n" % (name, s, s)
    for k in range(0, s, per):
        out += "        " + ", ".join('"%s"' % r for r in rows[k:k + per]) + ",\n"
    out += "    };\n    static constexpr Ink inks[] = {\n"
    for p, key in inks.items():
        out += "        {'%s', rgb(%d, %d, %d)},\n" % (key, round(p[0] * 7 / 255), round(p[1] * 7 / 255),
                                                     round(p[2] * 3 / 255))
    out += "    };\n    static constexpr Pixels<%d> pixels = paint<%d>(rows, inks);\n" % (s, s)
    out += "    static constexpr Bitmap bitmap = {%d, %d, pixels.p, kClear};\n    return bitmap;\n}\n\n" % (s, s)
    return out


def cpp():
    out = ""
    for name, big, small in CHOSEN:
        out += cpp_one(name, big())
        if small is not None:
            out += cpp_one(name, small())
    return out


if __name__ == "__main__":
    if "--cpp" in sys.argv:
        sys.stdout.write(cpp())
    else:
        main()
