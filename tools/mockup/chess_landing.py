#!/usr/bin/env python3
"""Draws the four ways the computer's chess piece could land (backlog item
64) for the owner to choose from. A mock-up, not the game: it takes the
chess window's reference picture, plays the knight from c6 to e5 and moves
only the landing square. Offsets are whole pixels, as the board draws them.
The owner chose B, drop and bounce; the game's table of heights
(kLanding in core/src/shell/chess.cpp) is drop() below at 60 steps a second.

    python3 tools/mockup/chess_landing.py <folder>      writes <folder>/*.gif
"""
import math, os, sys
from PIL import Image, ImageDraw, ImageFont

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "host", "tests", "pictures",
                   "56-chess-playing.png")
HERE = sys.argv[1] if len(sys.argv) > 1 else "."
SQ, BX, BY = 40, 160, 76
FPS = 50                      # a GIF cannot hold 60; the board would run at 60
STEP = 1000 // FPS

def sq(col, row):             # col 0 = a, row 0 = rank 8
    return (BX + col * SQ, BY + row * SQ, BX + (col + 1) * SQ, BY + (row + 1) * SQ)

base = Image.open(SRC).convert("RGB")
LIGHT = base.getpixel((BX + 7 * SQ + 20, BY + 3 * SQ + 20))   # h5 is empty and light
DARK = base.getpixel((BX + 1 * SQ + 4, BY + 0 * SQ + 4))      # b8
LIME = base.getpixel((BX + 1 * SQ + 4, BY + 4 * SQ + 4))      # b4, last move on dark
YELLOW = base.getpixel((BX + 5 * SQ + 2, BY + 5 * SQ + 2))    # f3, last move on light

def recolour(im, col, row, old, new):
    x0, y0, x1, y1 = sq(col, row)
    px = im.load()
    for y in range(y0, y1):
        for x in range(x0, x1):
            if px[x, y] == old:
                px[x, y] = new

def is_dark(col, row):
    return (col + row) % 2 == 1

# A clean position: no dots, nothing picked up.
clean = base.copy()
px = clean.load()
for row in range(8):
    for col in range(8):
        x0, y0, x1, y1 = sq(col, row)
        plain = DARK if is_dark(col, row) else LIGHT
        cx, cy = x0 + 20, y0 + 20
        if px[cx, cy] not in (LIGHT, DARK, LIME, YELLOW, (0, 0, 0), (255, 255, 255)) and \
           px[x0 + 20, y0 + 30] in (LIGHT, DARK):
            # a dot on an empty square: wipe the middle
            ImageDraw.Draw(clean).rectangle((x0 + 10, y0 + 10, x1 - 10, y1 - 10), fill=plain)
        recolour(clean, col, row, LIME, DARK) if is_dark(col, row) else recolour(clean, col, row, YELLOW, LIGHT)

# The knight on c6 (a light square): everything that is not the square.
kx0, ky0, _, _ = sq(2, 2)
sprite = clean.crop(sq(2, 2)).convert("RGBA")
sp = sprite.load()
for y in range(SQ):
    for x in range(SQ):
        if sp[x, y][:3] == LIGHT:
            sp[x, y] = (0, 0, 0, 0)
bbox = sprite.getbbox()
top, bottom = bbox[1], bbox[3]

# Before: White has just played d2-d4, the computer is thinking.
before = clean.copy()
recolour(before, 3, 6, DARK, LIME)
recolour(before, 3, 4, DARK, LIME)
ImageDraw.Draw(before).rectangle((158, 426, 300, 442), fill=before.getpixel((300, 420)))

# After: c6 empty and marked, e5 marked, knight to be drawn on it.
after = clean.copy()
ImageDraw.Draw(after).rectangle(sq(2, 2)[:2] + (sq(2, 2)[2] - 1, sq(2, 2)[3] - 1), fill=YELLOW)
recolour(after, 4, 3, DARK, LIME)
ImageDraw.Draw(after).rectangle((sq(4, 3)[0] + 10, sq(4, 3)[1] + 10, sq(4, 3)[2] - 10, sq(4, 3)[3] - 10), fill=LIME)
empty_e5 = after.crop(sq(4, 3))

def landed(dx=0, dy=0, lean=0.0):
    """The landing square with the knight moved dx, dy and leaning: the foot
    stays, the head goes `lean` pixels sideways."""
    cell = empty_e5.copy().convert("RGBA")
    layer = Image.new("RGBA", (SQ, SQ), (0, 0, 0, 0))
    for y in range(top, bottom):
        shift = round(lean * (bottom - 1 - y) / (bottom - 1 - top))
        row = sprite.crop((0, y, SQ, y + 1))
        layer.paste(row, (dx + shift, y + dy))
    cell.alpha_composite(layer)
    return cell.convert("RGB")

def shake(t):      # side to side, dying away
    return dict(dx=round(3.4 * math.exp(-t / 150) * math.sin(2 * math.pi * t / 150)))

def drop(t):       # falls the last few pixels, one small bounce
    if t < 90:
        return dict(dy=-round(4 * (1 - (t / 90) ** 2)))
    u = t - 90
    return dict(dy=-round(3.4 * math.exp(-u / 110) * abs(math.sin(math.pi * u / 150))))

def rock(t):       # foot stays, head sways and settles
    return dict(lean=4.4 * math.exp(-t / 190) * math.sin(2 * math.pi * t / 210))

def drop_rock(t):  # a short fall, then a gentle sway
    if t < 70:
        return dict(dy=-round(4 * (1 - (t / 70) ** 2)))
    u = t - 70
    return dict(lean=2.6 * math.exp(-u / 180) * math.sin(2 * math.pi * u / 210))

VARIANTS = [("A  Shake", shake, 520), ("B  Drop and bounce", drop, 460),
            ("C  Rock", rock, 620), ("D  Drop, then rock", drop_rock, 600)]
NONE = ("As it is today", lambda t: {}, 0)

HOLD_BEFORE, HOLD_AFTER = 1300, 2200
TOTAL = HOLD_BEFORE + HOLD_AFTER

def frame(variant, ms):
    if ms < HOLD_BEFORE:
        return before
    im = after.copy()
    t = ms - HOLD_BEFORE
    im.paste(landed(**variant[1](t)) if t < variant[2] else landed(), sq(4, 3)[:2])
    return im

def save(frames, path):
    both = Image.new("RGB", (frames[0].width, 2 * frames[0].height))   # every colour of the run
    both.paste(frames[0], (0, 0)); both.paste(frames[-1], (0, frames[0].height))
    pal = both.quantize(colors=128, dither=Image.Dither.NONE)
    out = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    # merge runs of equal frames into one long frame
    kept, times = [out[0]], [STEP]
    for prev, f in zip(out, out[1:]):
        if f.tobytes() == prev.tobytes():
            times[-1] += STEP
        else:
            kept.append(f); times.append(STEP)
    kept[0].save(path, save_all=True, append_images=kept[1:], duration=times, loop=0, optimize=False)

CROP = (152, 68, 488, 404)
font = ImageFont.load_default()

def labelled(im, text, scale):
    b = im.crop(CROP)
    if scale != 1:
        b = b.resize((b.width * scale, b.height * scale), Image.NEAREST)
    out = Image.new("RGB", (b.width, b.height + 18), (192, 192, 192))
    ImageDraw.Draw(out).text((6, 3), text, fill=(0, 0, 0), font=font)
    out.paste(b, (0, 18))
    return out

# One GIF per variant at twice the size, and all four side by side at true size.
for name, v in zip("abcd", VARIANTS):
    save([labelled(frame(v, ms), v[0], 2) for ms in range(0, TOTAL, STEP)],
         os.path.join(HERE, f"landing-{name}.gif"))
save([labelled(frame(NONE, ms), NONE[0], 2) for ms in range(0, TOTAL, STEP)],
     os.path.join(HERE, "landing-none.gif"))

grid = []
for ms in range(0, TOTAL, STEP):
    tiles = [labelled(frame(v, ms), v[0], 1) for v in VARIANTS]
    w, h = tiles[0].size
    g = Image.new("RGB", (2 * w + 6, 2 * h + 6), (192, 192, 192))
    for i, t in enumerate(tiles):
        g.paste(t, ((i % 2) * (w + 6), (i // 2) * (h + 6)))
    grid.append(g)
save(grid, os.path.join(HERE, "landing-all.gif"))
