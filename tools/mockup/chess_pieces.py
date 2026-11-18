"""Draws the chess pieces proposed for the chess game (original art).

Each piece is described once as plain shapes on a 40 by 40 grid (below) and
turned into pixels at two sizes: 40 for the board and 20 for the captured
pieces. A pixel is one of four things: see-through, outline, body or detail.
The game paints body and detail in the piece's colours, so the same six
drawings serve both sides.

    python3 tools/mockup/chess_pieces.py header out.inc   the rows, for C++
    python3 tools/mockup/chess_pieces.py sheet out.png    a sheet to look at

There are three sets, which the game will offer as themes: Contour (the
detail is all in the outline), Symbols (plain signs) and Classic (the first
drawn, with inner lines). The sheet shows them side by side in that order.
"""
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SS = 8  # each pixel is decided from 8 by 8 samples
BIG, SMALL = 40, 20


class Pen:
    """Shapes in 40-grid units, drawn `scale` times bigger."""

    def __init__(self, size):
        self.k = size * SS / 40.0
        self.body = Image.new("L", (size * SS, size * SS), 0)
        self.detail = Image.new("L", (size * SS, size * SS), 0)
        self.on = ImageDraw.Draw(self.body)
        self.marks = ImageDraw.Draw(self.detail)

    def _p(self, pts):
        return [(x * self.k, y * self.k) for x, y in pts]

    def poly(self, *pts):
        self.on.polygon(self._p(pts), fill=255)

    def oval(self, cx, cy, rx, ry=None):
        ry = rx if ry is None else ry
        self.on.ellipse(self._p([(cx - rx, cy - ry), (cx + rx, cy + ry)]), fill=255)

    def box(self, x0, y0, x1, y1):
        self.on.rectangle(self._p([(x0, y0), (x1, y1)]), fill=255)

    def cut(self, x0, y0, x1, y1):
        self.on.rectangle(self._p([(x0, y0), (x1, y1)]), fill=0)

    def notch(self, *pts):
        """Takes a bite out of the body: the outline follows it."""
        self.on.polygon(self._p(pts), fill=0)

    def line(self, *pts, width=1.0):
        self.marks.line(self._p(pts), fill=255, width=max(1, round(width * self.k)), joint="curve")

    def dot(self, cx, cy, r):
        self.marks.ellipse(self._p([(cx - r, cy - r), (cx + r, cy + r)]), fill=255)

    def foot(self, half=10.5):
        """The base every piece stands on."""
        self.poly((20 - half + 1.5, 30), (20 + half - 1.5, 30), (20 + half, 33), (20 + half, 36.5),
                  (20 - half, 36.5), (20 - half, 33))
        self.line((20 - half + 1, 32.6), (20 + half - 1, 32.6))


def pawn(p):
    p.oval(20, 13.5, 5.6)
    p.oval(20, 20.3, 6.6, 2.0)
    p.poly((17, 20), (23, 20), (25, 26), (28, 30.5), (12, 30.5), (15, 26))
    p.foot(9.5)


def rook(p):
    p.box(10.5, 6, 29.5, 13)
    p.cut(15, 5, 17.6, 9)
    p.cut(22.4, 5, 25, 9)
    p.poly((11.5, 13), (28.5, 13), (26, 16.5), (14, 16.5))
    p.box(14, 16.5, 26, 27)
    p.poly((14, 26.5), (26, 26.5), (28.5, 30.5), (11.5, 30.5))
    p.line((12, 13.2), (28, 13.2))
    p.line((14.5, 16.7), (25.5, 16.7))
    p.line((14.5, 26.6), (25.5, 26.6))
    p.foot()


def knight(p):
    p.poly((12.5, 30.5), (13, 27), (15.5, 22.5), (18.5, 19), (16, 19.5), (12.5, 22.5), (9.5, 22.8),
           (7.6, 20.8), (7.8, 18), (10, 14.5), (12.5, 11), (14.5, 8), (15.5, 4.5), (18, 6.5),
           (20, 3.8), (21.5, 7), (25.5, 9), (28.8, 13), (30.5, 18.5), (31, 25), (30.5, 30.5))
    p.dot(14.6, 12.6, 1.15)
    p.line((8.6, 20.2), (10.2, 19.6), width=0.9)
    p.line((23, 9.5), (26.5, 13.5), (28, 19), (28.3, 26))
    p.foot()


def bishop(p):
    p.oval(20, 5.6, 2.3)
    p.poly((20, 7), (23.5, 10.5), (26, 15), (26.4, 19), (24.5, 23), (15.5, 23), (13.6, 19), (14, 15),
           (16.5, 10.5))
    p.oval(20, 24.2, 7.2, 2.0)
    p.poly((17.2, 24.5), (22.8, 24.5), (24, 27.5), (27.5, 30.5), (12.5, 30.5), (16, 27.5))
    p.line((21.2, 10.5), (17.4, 16.8), width=1.2)
    p.foot()


def queen(p):
    for x, y in ((8.6, 11.5), (14.2, 8.3), (20, 7.2), (25.8, 8.3), (31.4, 11.5)):
        p.oval(x, y, 2.1)
    p.poly((11.5, 27), (8.6, 12.5), (13.6, 20), (14.2, 9.5), (17.6, 19.3), (20, 8.5), (22.4, 19.3),
           (25.8, 9.5), (26.4, 20), (31.4, 12.5), (28.5, 27))
    p.poly((11.5, 26.5), (28.5, 26.5), (29.5, 30.5), (10.5, 30.5))
    p.line((12, 24), (20, 22.6), (28, 24))
    p.line((11.5, 27.2), (28.5, 27.2))
    p.foot()


def king(p):
    p.box(18.9, 2.6, 21.1, 11)
    p.box(16.2, 4.6, 23.8, 6.8)
    p.oval(20, 13.2, 4.2, 3.6)
    p.poly((12.5, 27), (9, 20), (9.4, 16.6), (12.5, 14.6), (16.5, 15), (20, 18.5), (23.5, 15),
           (27.5, 14.6), (30.6, 16.6), (31, 20), (27.5, 27))
    p.poly((12.5, 26.5), (27.5, 26.5), (29.5, 30.5), (10.5, 30.5))
    p.line((20, 18.5), (20, 26))
    p.line((12.5, 27.2), (27.5, 27.2))
    p.foot()


CLASSIC = [("pawn", pawn), ("knight", knight), ("bishop", bishop), ("rook", rook), ("queen", queen),
           ("king", king)]


# The second set (the owner asked for one "more symbolic, less detail"):
# each piece is its sign and little else. Heavier shapes, a flat slab to
# stand on, and no inner lines but the bishop's cut and the knight's eye.
def slab(p):
    p.box(9, 31.5, 31, 36.5)


def s_pawn(p):
    p.oval(20, 14.5, 6.6)
    p.poly((16, 20), (24, 20), (28.5, 32), (11.5, 32))
    slab(p)


def s_knight(p):
    p.poly((11.5, 32), (13, 26), (18.5, 19.5), (13.5, 22.5), (9.5, 23), (7.5, 19.5), (12.5, 11.5),
           (14.5, 4.5), (19, 8), (25, 9), (30, 14.5), (31.5, 23), (30.5, 32))
    p.dot(15.2, 13.2, 1.3)
    slab(p)


def s_bishop(p):
    p.oval(20, 5.4, 2.6)
    p.poly((20, 6.5), (25, 11.5), (27, 17), (25.5, 22), (22.5, 25), (17.5, 25), (14.5, 22), (13, 17),
           (15, 11.5))
    p.poly((17, 24), (23, 24), (28, 32), (12, 32))
    p.line((22.4, 9.5), (18, 17), width=1.6)
    slab(p)


def s_rook(p):
    p.box(10.5, 6.5, 29.5, 16)
    p.cut(15, 5, 18, 10.5)
    p.cut(22, 5, 25, 10.5)
    p.box(14, 15.5, 26, 32)
    slab(p)


def s_queen(p):
    for x, y in ((8.2, 11.5), (20, 7.4), (31.8, 11.5)):
        p.oval(x, y, 2.7)
    p.poly((11, 32), (8.2, 12.5), (14.5, 21), (20, 9), (25.5, 21), (31.8, 12.5), (29, 32))
    slab(p)


def s_king(p):
    p.box(18.2, 2.6, 21.8, 15)
    p.box(14.4, 5.8, 25.6, 9.2)
    p.poly((12.5, 32), (9, 19.5), (12, 14.5), (28, 14.5), (31, 19.5), (27.5, 32))
    slab(p)


SYMBOLS = [("pawn", s_pawn), ("knight", s_knight), ("bishop", s_bishop), ("rook", s_rook),
           ("queen", s_queen), ("king", s_king)]


# The third set (the owner: "add more detail to the pieces by contour, not
# by internal lines"): turned pieces seen against the light. Collars,
# waists, a stepped foot, the bishop's cut and the knight's mane are all in
# the outline; nothing is drawn inside.
def step(p):
    p.box(11, 29.5, 29, 33)
    p.box(8.5, 32.5, 31.5, 36.5)


def c_pawn(p):
    p.oval(20, 12.3, 5.4)
    p.oval(20, 19, 7.2, 1.9)
    p.poly((17.5, 19), (22.5, 19), (24, 24), (27, 29.8), (13, 29.8), (16, 24))
    step(p)


def c_knight(p):
    p.poly((12.5, 29.8), (13.5, 25.5), (16, 21.5), (19, 18.5), (16, 19.3), (13, 22), (10, 22.8),
           (7.8, 21), (7.6, 18.5), (9.6, 15), (12, 11.5), (13.8, 8.5), (14.2, 4.2), (16.8, 7),
           (18.6, 3.6), (20.6, 7.2), (24.2, 8.4), (24.6, 11.8), (28, 12.2), (28, 16.4),
           (31, 17.2), (30.4, 22), (32, 23.4), (31.2, 29.8))
    p.notch((7, 20.4), (10.6, 19.2), (10.6, 20.4))
    step(p)


def c_bishop(p):
    p.oval(20, 4.8, 2.3)
    p.poly((20, 6.3), (23.5, 9.8), (26, 14.5), (26.4, 18.5), (24.5, 22.5), (15.5, 22.5), (13.6, 18.5),
           (14, 14.5), (16.5, 9.8))
    p.notch((25, 10.4), (27.6, 12.4), (21.6, 16.6), (20.8, 15.2))
    p.oval(20, 23.6, 7.6, 1.9)
    p.poly((17.5, 24), (22.5, 24), (24, 27), (27, 29.8), (13, 29.8), (16, 27))
    step(p)


def c_rook(p):
    p.box(10.5, 5.5, 29.5, 12)
    p.cut(15, 4, 17.6, 8.6)
    p.cut(22.4, 4, 25, 8.6)
    p.poly((10.5, 12), (29.5, 12), (26, 15.5), (14, 15.5))
    p.box(14.5, 15, 25.5, 26)
    p.poly((14.5, 25), (25.5, 25), (28, 29.8), (12, 29.8))
    step(p)


def c_queen(p):
    for x, y in ((8.6, 10.5), (14.2, 7.3), (20, 6.2), (25.8, 7.3), (31.4, 10.5)):
        p.oval(x, y, 2.1)
    p.poly((12, 26), (8.6, 11.5), (13.6, 19), (14.2, 8.5), (17.6, 18.3), (20, 7.5), (22.4, 18.3),
           (25.8, 8.5), (26.4, 19), (31.4, 11.5), (28, 26))
    p.oval(20, 26.3, 10.4, 1.9)
    p.poly((13, 26.5), (27, 26.5), (28, 29.8), (12, 29.8))
    step(p)


def c_king(p):
    p.box(18.8, 2.2, 21.2, 10.5)
    p.box(16, 4.2, 24, 6.5)
    p.oval(20, 12.6, 4.2, 3.4)
    p.poly((13, 26), (9, 19.5), (9.4, 16.2), (12.5, 14.2), (16.5, 14.6), (20, 18), (23.5, 14.6),
           (27.5, 14.2), (30.6, 16.2), (31, 19.5), (27, 26))
    p.oval(20, 26.3, 9.6, 1.9)
    p.poly((13, 26.5), (27, 26.5), (28, 29.8), (12, 29.8))
    step(p)


CONTOUR = [("pawn", c_pawn), ("knight", c_knight), ("bishop", c_bishop), ("rook", c_rook),
           ("queen", c_queen), ("king", c_king)]
# The sets the game will offer, by the names its menu shows.
SETS = [("Contour", CONTOUR), ("Symbols", SYMBOLS), ("Classic", CLASSIC)]


def rows(draw, size):
    """The piece as text rows: ' ' clear, 'X' outline, 'o' body, '-' detail."""
    pen = Pen(size)
    draw(pen)
    small = lambda im: im.resize((size, size), Image.Resampling.BOX)
    body = small(pen.body).point(lambda v: 255 if v >= 112 else 0)
    inside = body.filter(ImageFilter.MinFilter(3))  # one pixel in from the edge
    # In the small drawing only the strongest marks survive.
    detail = small(pen.detail).point(lambda v: 255 if v >= (70 if size >= 32 else 150) else 0)
    out = []
    for y in range(size):
        row = ""
        for x in range(size):
            if not body.getpixel((x, y)):
                row += " "
            elif not inside.getpixel((x, y)):
                row += "X"
            elif detail.getpixel((x, y)):
                row += "-"
            else:
                row += "o"
        out.append(row)
    return out


def header(path):
    with open(path, "w") as f:
        f.write("// Made by tools/mockup/chess_pieces.py: do not edit.\n")
        f.write("constexpr int kSets = %d;\n" % len(SETS))
        f.write("constexpr const char *kSetNames[kSets] = {%s};\n" % ", ".join('"%s"' % n for n, _ in SETS))
        for size, tag in ((BIG, "Big"), (SMALL, "Small")):
            f.write("constexpr int k%sSize = %d;\n" % (tag, size))
            f.write("constexpr const char *k%s[kSets][6][%d] = {\n" % (tag, size))
            for set_name, pieces in SETS:
                f.write("    { // %s\n" % set_name)
                for name, draw in pieces:
                    f.write("        { // %s\n" % name)
                    for row in rows(draw, size):
                        f.write('            "%s",\n' % row)
                    f.write("        },\n")
                f.write("    },\n")
            f.write("};\n")


def sheet(path):
    # Both sides on both squares of both boards, at twice the size, and the
    # small ones on the window's face.
    squares = [(255, 255, 255), (109, 146, 85), (255, 219, 170), (182, 146, 85)]
    white = {"X": (0, 0, 0), "o": (255, 255, 255), "-": (0, 0, 0)}
    black = {"X": (0, 0, 0), "o": (73, 73, 85), "-": (182, 182, 170)}
    img = Image.new("RGB", (len(SETS) * (6 * BIG + 8) - 8, 8 * BIG + 2 * SMALL + 8), (182, 182, 170))
    # The sets side by side, in the order of the game's menu.
    both = [(n * (6 * BIG + 8) + i * BIG, d) for n, (_, pieces) in enumerate(SETS)
            for i, (_, d) in enumerate(pieces)]
    for left, draw in both:
        big, small = rows(draw, BIG), rows(draw, SMALL)
        for band in range(8):
            ink = white if band % 2 == 0 else black
            sq = squares[band // 2]
            for y in range(BIG):
                for x in range(BIG):
                    ch = big[y][x]
                    img.putpixel((left + x, band * BIG + y), sq if ch == " " else ink[ch])
        for band, ink in enumerate((white, black)):
            for y in range(SMALL):
                for x in range(SMALL):
                    ch = small[y][x]
                    if ch != " ":
                        img.putpixel((left + 10 + x, 8 * BIG + 4 + band * SMALL + y), ink[ch])
    img.resize((img.width * 2, img.height * 2), Image.Resampling.NEAREST).save(path)


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("header", "sheet"):
        sys.exit(__doc__)
    (header if sys.argv[1] == "header" else sheet)(sys.argv[2])
