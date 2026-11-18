"""Draws the chess pieces proposed for the chess game (original art).

Each piece is described once as plain shapes on a 40 by 40 grid (below) and
turned into pixels at two sizes: 40 for the board and 20 for the captured
pieces. A pixel is one of four things: see-through, outline, body or detail.
The game paints body and detail in the piece's colours, so the same six
drawings serve both sides.

    python3 tools/mockup/chess_pieces.py header out.inc   the rows, for C++
    python3 tools/mockup/chess_pieces.py sheet out.png    a sheet to look at
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


PIECES = [("pawn", pawn), ("knight", knight), ("bishop", bishop), ("rook", rook), ("queen", queen),
          ("king", king)]


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
        for size, tag in ((BIG, "Big"), (SMALL, "Small")):
            f.write("constexpr int k%sSize = %d;\n" % (tag, size))
            f.write("constexpr const char *k%s[6][%d] = {\n" % (tag, size))
            for name, draw in PIECES:
                f.write("    { // %s\n" % name)
                for row in rows(draw, size):
                    f.write('        "%s",\n' % row)
                f.write("    },\n")
            f.write("};\n")


def sheet(path):
    # Both sides on both squares of both boards, at twice the size, and the
    # small ones on the window's face.
    squares = [(255, 255, 255), (109, 146, 85), (255, 219, 170), (182, 146, 85)]
    white = {"X": (0, 0, 0), "o": (255, 255, 255), "-": (0, 0, 0)}
    black = {"X": (0, 0, 0), "o": (73, 73, 85), "-": (182, 182, 170)}
    img = Image.new("RGB", (6 * BIG, 8 * BIG + 2 * SMALL + 8), (182, 182, 170))
    for i, (name, draw) in enumerate(PIECES):
        big, small = rows(draw, BIG), rows(draw, SMALL)
        for band in range(8):
            ink = white if band % 2 == 0 else black
            sq = squares[band // 2]
            for y in range(BIG):
                for x in range(BIG):
                    ch = big[y][x]
                    img.putpixel((i * BIG + x, band * BIG + y), sq if ch == " " else ink[ch])
        for band, ink in enumerate((white, black)):
            for y in range(SMALL):
                for x in range(SMALL):
                    ch = small[y][x]
                    if ch != " ":
                        img.putpixel((i * BIG + 11 + x, 8 * BIG + 4 + band * SMALL + y), ink[ch])
    img.resize((img.width * 2, img.height * 2), Image.Resampling.NEAREST).save(path)


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("header", "sheet"):
        sys.exit(__doc__)
    (header if sys.argv[1] == "header" else sheet)(sys.argv[2])
