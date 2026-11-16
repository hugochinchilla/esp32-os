"""Mock-up kit: draws previews in the board's real 256 colours."""
from PIL import Image, ImageDraw, ImageFont

L3 = [round(n * 255 / 7) for n in range(8)]
LB = [L3[n] for n in (0, 2, 5, 7)]
PALETTE = [(L3[v >> 5], L3[(v >> 2) & 7], LB[v & 3]) for v in range(256)]


def c(r, g, b):
    """Board colour from levels r,g 0..7 and b 0..3."""
    return (L3[r], L3[g], LB[b])


BLACK, WHITE = c(0, 0, 0), c(7, 7, 3)
GREY, DARK = c(5, 5, 2), c(2, 2, 1)

_pal_img = Image.new("P", (1, 1))
_pal_img.putpalette([x for rgb in PALETTE for x in rgb])


def to_board(img, dither=False):
    """Reduces any picture to the board's colours."""
    return img.convert("RGB").quantize(
        palette=_pal_img, dither=Image.Dither.ORDERED if False else
        (Image.Dither.FLOYDSTEINBERG if dither else Image.Dither.NONE)).convert("RGB")


class BdfFont:
    def __init__(self, path, bold_from=None):
        self.glyphs = {}
        self.ascent = self.descent = 0
        cur = None
        rows = None
        for line in open(path, encoding="latin-1"):
            p = line.split()
            if not p:
                continue
            k = p[0]
            if k == "FONT_ASCENT":
                self.ascent = int(p[1])
            elif k == "FONT_DESCENT":
                self.descent = int(p[1])
            elif k == "STARTCHAR":
                cur = {}
            elif k == "ENCODING":
                cur["code"] = int(p[1])
            elif k == "DWIDTH":
                cur["adv"] = int(p[1])
            elif k == "BBX":
                cur["bbx"] = tuple(int(v) for v in p[1:5])
            elif k == "BITMAP":
                rows = []
            elif k == "ENDCHAR":
                cur["rows"] = rows
                self.glyphs[cur["code"]] = cur
                cur = rows = None
            elif rows is not None:
                rows.append(int(k, 16) << (64 - 4 * len(k)))
        self.height = self.ascent + self.descent
        self.smear = False

    def bold(self):
        import copy
        f = copy.copy(self)
        f.smear = True
        return f

    def width(self, text):
        return sum(self.glyphs.get(ord(ch), self.glyphs[32])["adv"] + (1 if self.smear else 0)
                   for ch in text)

    def draw(self, img, x, y, text, colour):
        px = img.load()
        W, H = img.size
        base = y + self.ascent
        for ch in text:
            g = self.glyphs.get(ord(ch), self.glyphs[32])
            w, h, xo, yo = g["bbx"]
            top = base - yo - h
            for r, bits in enumerate(g["rows"]):
                for col in range(w):
                    if bits & (1 << (63 - col)):
                        for dx in ((0, 1) if self.smear else (0,)):
                            X, Y = x + xo + col + dx, top + r
                            if 0 <= X < W and 0 <= Y < H:
                                px[X, Y] = colour
            x += g["adv"] + (1 if self.smear else 0)
        return x


class TtfFont:
    def __init__(self, path, size):
        self.f = ImageFont.truetype(path, size)
        a, d = self.f.getmetrics()
        self.ascent, self.descent = a, d
        self.height = a + d

    def bold(self):
        return self

    def width(self, text):
        return round(self.f.getlength(text))

    def draw(self, img, x, y, text, colour):
        d = ImageDraw.Draw(img)
        d.fontmode = "1"
        d.text((x, y), text, font=self.f, fill=colour)
        return x + self.width(text)
