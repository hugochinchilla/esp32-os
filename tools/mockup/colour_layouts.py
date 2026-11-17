#!/usr/bin/env python3
"""The arrangements tried for the colour dialog's 256 colours, as pictures.

The story is in docs/journal/2026-11-17-colour-chooser.md; this draws its
pictures into docs/look/colour-chooser/ with the colours the board really
shows (red and green in eight levels, blue in four).

    python3 tools/mockup/colour_layouts.py
"""
import colorsys
import os

from PIL import Image

L3 = [0, 36, 73, 109, 146, 182, 219, 255]
LB = [0, 73, 182, 255]
CW, CH, ZOOM = 18, 14, 2
GREY, EDGE = (182, 182, 182), (73, 73, 73)


def col(r, g, b):
    return (L3[r], L3[g], LB[b])


ALL = [col(r, g, b) for r in range(8) for g in range(8) for b in range(4)]


def draw(rows, strip=None):
    """A grid of cells; `strip` is a row of wide cells above it."""
    from PIL import ImageDraw
    top = 8 + (CH + 6 if strip else 0)
    im = Image.new("RGB", (len(rows[0]) * CW + 16, len(rows) * CH + top + 8), GREY)
    d = ImageDraw.Draw(im)
    if strip:
        w = len(rows[0]) * CW // len(strip)
        for i, c in enumerate(strip):
            d.rectangle([8 + i * w + 1, 9, 8 + i * w + w - 2, 6 + CH], fill=c, outline=EDGE)
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c is not None:
                d.rectangle([8 + x * CW + 1, top + y * CH + 1, 8 + x * CW + CW - 2, top + y * CH + CH - 2],
                            fill=c, outline=EDGE)
    return im.resize((im.width * ZOOM, im.height * ZOOM), Image.NEAREST)


def hue(c):
    return colorsys.rgb_to_hls(*[v / 255 for v in c])[0]


def luma(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def by_hue(colours, columns, rows):
    colours = sorted(colours, key=lambda c: (hue(c), c))
    grid = [[None] * columns for _ in range(rows)]
    for k in range(columns):
        for y, c in enumerate(sorted(colours[k * rows:(k + 1) * rows], key=lambda c: (luma(c), c))):
            grid[y][k] = c
    return grid


def four_squares():
    # One square for each level of blue; red across, green down.
    grid = [[None] * 16 for _ in range(16)]
    for r in range(8):
        for g in range(8):
            for b in range(4):
                grid[g + 8 * (b // 2)][r + 8 * (b % 2)] = col(r, g, b)
    return grid


def sheet():
    # The four squares in a row, red running back in every other one.
    grid = [[None] * 32 for _ in range(8)]
    for b in range(4):
        for g in range(8):
            for x in range(8):
                grid[g][b * 8 + x] = col(x if b % 2 == 0 else 7 - x, g, b)
    return grid


def main():
    out = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "look", "colour-chooser")
    os.makedirs(out, exist_ok=True)
    s = sheet()
    side = [s[g][:16] + s[7 - g][16:] for g in range(8)]
    fold = [s[g][:16] for g in range(8)] + [s[7 - g][16:] for g in range(8)]
    fold_mirrored = [s[g][:16] for g in range(8)] + [s[7 - g][16:][::-1] for g in range(8)]
    swapped = fold_mirrored[8:] + fold_mirrored[:8]
    low = sorted(ALL, key=lambda c: max(c) - min(c))
    greys = [c for c in ALL if c[0] == c[1] == c[2]]
    near = [(L3[i], L3[i], min(LB, key=lambda b: abs(b - L3[i]))) for i in range(8)]
    pictures = {
        "1-four-squares": draw(four_squares()),
        "2-by-hue": draw([[sorted(low[:16], key=luma)[y]] + by_hue(low[16:], 15, 16)[y] for y in range(16)]),
        "3-grey-strip-and-hues": draw(by_hue([c for c in ALL if c not in near], 31, 8), strip=near),
        "3b-the-four-neutral-greys": draw([greys]),
        "4-one-sheet-32x8": draw(s),
        "5a-right-half-upside-down": draw(side),
        "5b-folded": draw(fold),
        "5c-folded-and-mirrored": draw(fold_mirrored),
        "6-halves-swapped": draw(swapped),
    }
    for name, im in pictures.items():
        im.save(os.path.join(out, name + ".png"))
        print(name)


if __name__ == "__main__":
    main()
