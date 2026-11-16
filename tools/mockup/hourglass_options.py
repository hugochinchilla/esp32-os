"""Draws the candidate busy pointers (original art) for the owner to choose.

Each is a grid like the ones in core/src/video/pointer_shapes.cpp: 'X' black,
'.' white, ' ' see-through. Writes one sheet: every candidate enlarged, and at
its real size next to the arrow on the three backgrounds it will be seen on.

    python3 tools/mockup/hourglass_options.py out.png
"""
import sys

from PIL import Image, ImageDraw

ARROW = [
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.........X ",
    "X......XXXXX",
    "X...X..X    ",
    "X..X X..X   ",
    "X.X  X..X   ",
    "XX    X..X  ",
    "X     X..X  ",
    "       X..X ",
    "       XXX  ",
]

# A: straight glass between two flat caps, sand running.
SAND_TIMER = [
    "XXXXXXXXXXXXXXX",
    "X.............X",
    "XXXXXXXXXXXXXXX",
    " X...........X ",
    " X...........X ",
    " X.XXXXXXXXX.X ",
    "  X.XXXXXXX.X  ",
    "   X.XXXXX.X   ",
    "    X.XXX.X    ",
    "     X.X.X     ",
    "      XXX      ",
    "     X.X.X     ",
    "    X.....X    ",
    "   X...X...X   ",
    "  X.........X  ",
    " X.....X.....X ",
    " X....XXX....X ",
    " X..XXXXXXX..X ",
    " X.XXXXXXXXX.X ",
    "XXXXXXXXXXXXXXX",
    "X.............X",
    "XXXXXXXXXXXXXXX",
]

# B: two round bulbs with solid caps.
ROUND_GLASS = [
    "  XXXXXXXXXXX  ",
    " XXXXXXXXXXXXX ",
    "  X.........X  ",
    " X...........X ",
    " X...........X ",
    " X.XXXXXXXXX.X ",
    " X.XXXXXXXXX.X ",
    "  X.XXXXXXX.X  ",
    "   XX.XXX.XX   ",
    "     XX.XX     ",
    "      X.X      ",
    "     XX.XX     ",
    "   XX.....XX   ",
    "  X....X....X  ",
    " X...........X ",
    " X.....X.....X ",
    " X....XXX....X ",
    " X..XXXXXXX..X ",
    "  X.XXXXXXX.X  ",
    " XXXXXXXXXXXXX ",
    "  XXXXXXXXXXX  ",
]

SMALL_GLASS = [
    "XXXXXXXXX",
    " X.....X ",
    " X.XXX.X ",
    "  X.X.X  ",
    "   X.X   ",
    "  X...X  ",
    " X..X..X ",
    " X.XXX.X ",
    "XXXXXXXXX",
]

# D: the sand timer with a chinchilla's ears on its top cap.
EARS = [
    "  XX       XX  ",
    " X..X     X..X ",
    "X....X   X....X",
    "X....X   X....X",
]


def beside(left, right, dx, dy):
    """`right` placed at (dx, dy) on a sheet that also holds `left`."""
    w = max(len(left[0]), dx + len(right[0]))
    h = max(len(left), dy + len(right))
    rows = [[" "] * w for _ in range(h)]
    for grid, ox, oy in ((left, 0, 0), (right, dx, dy)):
        for y, row in enumerate(grid):
            for x, ch in enumerate(row):
                if ch != " ":
                    rows[oy + y][ox + x] = ch
    return ["".join(r) for r in rows]


CANDIDATES = [
    ("A  Sand timer", SAND_TIMER),
    ("B  Round glass", ROUND_GLASS),
    ("C  Arrow and glass", beside(ARROW, SMALL_GLASS, 12, 10)),
    ("D  Chinchilla timer", EARS + SAND_TIMER),
]

INK = {"X": (0, 0, 0), ".": (255, 255, 255)}
# Desktop teal, window grey, and the white of a document.
BACKGROUNDS = [(0, 109, 85), (182, 182, 170), (255, 255, 255)]


def stamp(img, grid, ox, oy, scale):
    d = ImageDraw.Draw(img)
    for y, row in enumerate(grid):
        for x, ch in enumerate(row):
            if ch in INK:
                d.rectangle([ox + x * scale, oy + y * scale, ox + (x + 1) * scale - 1, oy + (y + 1) * scale - 1],
                            fill=INK[ch])


def main(out):
    big, cell_w, top = 8, 230, 34  # 27 rows of room
    sheet = Image.new("RGB", (cell_w * len(CANDIDATES), top + 27 * big + 3 * 44 + 16), BACKGROUNDS[1])
    d = ImageDraw.Draw(sheet)
    for i, (name, grid) in enumerate(CANDIDATES):
        x0 = i * cell_w
        d.rectangle([x0 + 8, top, x0 + cell_w - 8, top + 27 * big], fill=BACKGROUNDS[0])
        d.text((x0 + 12, 10), "%s  (%dx%d)" % (name, len(grid[0]), len(grid)), fill=(0, 0, 0))
        stamp(sheet, grid, x0 + (cell_w - len(grid[0]) * big) // 2, top + (27 - len(grid)) * big // 2, big)
        for b, colour in enumerate(BACKGROUNDS):
            y0 = top + 27 * big + 8 + b * 44
            d.rectangle([x0 + 8, y0, x0 + cell_w - 8, y0 + 38], fill=colour)
            stamp(sheet, ARROW, x0 + 60, y0 + 8, 1)
            stamp(sheet, grid, x0 + 120, y0 + 6, 1)
    sheet.save(out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "hourglass-options.png")
