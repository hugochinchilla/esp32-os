"""Mock-up: the BIOS as a true 80x30 text screen (8x16 cells on 640x480) with
the mascot as a picture in the top right corner, in several free fonts.
python3 bios_text.py FONTDIR REPO OUTDIR

Spleen is in the repo and Unifont comes from the system's fonts. FONTDIR holds
the three candidates that were not chosen and are not kept in the repo:
ter-u16n.bdf (Terminus, SIL OFL), unscii-16.hex (Unscii, public domain) and
tamzen8x16r.bdf (Tamzen, free licence). The pictures are in
docs/look/bios-text/. The owner chose Spleen (2026-11-17)."""
import sys, os
from PIL import Image, ImageFont, ImageDraw
FD, REPO, OUT = sys.argv[1:4]
sys.path.insert(0, REPO + "/tools/mockup")
from kit import c, BLACK, WHITE, to_board

def bdf(path):
    g = {}; asc = desc = 0; cur = None; rows = None
    for line in open(path, encoding="latin-1"):
        p = line.split()
        if not p: continue
        k = p[0]
        if k == "FONT_ASCENT": asc = int(p[1])
        elif k == "FONT_DESCENT": desc = int(p[1])
        elif k == "ENCODING": code = int(p[1])
        elif k == "BBX": bbx = [int(v) for v in p[1:5]]
        elif k == "BITMAP": rows = []
        elif k == "ENDCHAR":
            w, h, xo, yo = bbx
            cell = [0] * 16
            top = asc - yo - h + (16 - asc - desc) // 2
            for r, (bits, n) in enumerate(rows):
                y = top + r
                if 0 <= y < 16:
                    v = bits << 16 >> (n * 4)      # left aligned in 16 bits
                    v = (v >> 8) >> max(xo, 0)
                    cell[y] = v & 0xFF
            g[code] = cell; rows = None
        elif rows is not None: rows.append((int(k, 16), len(k)))
    return g

def hexfont(path):
    g = {}
    for line in open(path):
        code, h = line.strip().split(":")
        if len(h) == 32: g[int(code, 16)] = [int(h[i:i + 2], 16) for i in range(0, 32, 2)]
    return g

def ttf(path):
    f = ImageFont.truetype(path, 16); g = {}
    for code in list(range(32, 127)) + [0x2500, 0x2550, 0x2588, 0x2591]:
        im = Image.new("1", (8, 16)); d = ImageDraw.Draw(im); d.fontmode = "1"
        d.text((0, 0), chr(code), font=f, fill=1)
        g[code] = [sum(1 << (7 - x) for x in range(8) if im.getpixel((x, y))) for y in range(16)]
    return g

FONTS = [
    ("a-spleen", "Spleen 8x16", "BSD. Already in the repo, same family as the desktop font", bdf(REPO + "/third_party/fonts/spleen/spleen-8x16.bdf")),
]
if os.path.exists(FD + "/ter-u16n.bdf"):  # the candidates that were not chosen
    FONTS += [
        ("b-terminus", "Terminus 8x16", "SIL Open Font Licence. The classic Linux console font", bdf(FD + "/ter-u16n.bdf")),
        ("c-unscii", "Unscii 16", "Public domain. Drawn to recall 80s and 90s computer text", hexfont(FD + "/unscii-16.hex")),
        ("d-tamzen", "Tamzen 8x16", "Free licence. Slim, modern terminal look", bdf(FD + "/tamzen8x16r.bdf")),
        ("e-unifont", "GNU Unifont", "SIL Open Font Licence. Plain, covers every script", ttf("/usr/share/fonts/opentype/unifont/unifont.otf")),
    ]
HI, TXT, DIM, YEL, GRN = WHITE, c(5, 5, 2), c(3, 3, 1), c(7, 7, 0), c(2, 6, 1)

def put(img, g, col, row, text, colour, bg=None):
    px = img.load()
    for i, ch in enumerate(text):
        cell = g.get(ord(ch)) or g.get(ord("-")) if ch != " " else [0] * 16
        for y in range(16):
            for x in range(8):
                on = cell[y] >> (7 - x) & 1
                if on or bg:
                    px[(col + i) * 8 + x, row * 16 + y] = colour if on else bg

src = Image.open(REPO + "/docs/look/boot/boot-b-self-test.png").convert("RGB")
MASCOT = src.crop((532, 16, 620, 88))

def screen(g, grid=False):
    img = Image.new("RGB", (640, 480), BLACK)
    L = [
        (1, HI, "Chinchilla BIOS v0.1"),
        (2, TXT, "Copyleft 2026, built on nine resistors"),
        (4, TXT, "Main Processor : ESP32-S3 dual core, 240 MHz"),
        (5, TXT, "Memory Test    :   8192K OK   (external)"),
        (6, TXT, "               :    376K OK   (internal)"),
        (8, TXT, "Detecting video ....... VGA 640x480, 256 colours"),
        (9, TXT, "Detecting drive C: .... 12 MB flash disk"),
        (10, TXT, "Detecting keyboard .... serial console"),
        (11, TXT, "Detecting sound ....... none"),
        (13, TXT, "Boot device ........... OS partition, 1.28 MB, image OK"),
        (15, YEL, "Starting Chinchilla..."),
        (17, TXT, "Press any key to continue"),
        (18, TXT, "Booting in 5"),
        (28, DIM, "Press DEL to enter SETUP, F8 for boot menu"),
        (29, DIM, "17/11/2026-ESP32S3-N16R8-CHINCHILLA-00"),
    ]
    for row, colour, text in L: put(img, g, 2, row, text, colour)
    for row, colour, text in L:
        for w in ("8192K OK", "376K OK"):
            if w in text: put(img, g, 2 + text.index(w), row, w, HI)
    img.paste(MASCOT, (640 - 88 - 16, 16))      # logo: cols 67..77, rows 1..5
    return img

if len(FONTS) > 1:  # the font mock-ups, only when all five can be drawn
    for key, name, note, g in FONTS:
        screen(g).save(f"{OUT}/bios-text-{key}.png")

# ---- The reference for the PC test (host/tests/test_bios.cpp): the screen
# as core/src/bios/post.cpp lays it out, drawn here from Spleen's BDF file
# and the mascot's own squares, with the test's values. ----
MASCOT16 = [
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG",
    "GPPPGGLLLLGGPPPG", ".GGGLLLLLLLLGGG.", "..GLLLLLLLLLLG..", "..GLKLLLLLLKLG..",
    "..GLKLLLLLLKLG..", "..GLLLLNNLLLLG..", "D.GLLLLNNLLLLG.D", ".DDGLLLLLLLLGDD.",
    "D..GGLLLLLLGG..D", "....GGGGGGGG....", "................", "................"]
DARK, RED = c(2, 2, 1), c(7, 1, 0)


def post_reference(g):
    img = Image.new("RGB", (640, 480), BLACK)
    colours = {"G": TXT, "D": TXT, "L": HI, "P": c(7, 4, 2), "K": BLACK, "N": c(7, 3, 2)}
    px = img.load()
    for r, line in enumerate(MASCOT16):
        for q, ch in enumerate(line):
            if ch in colours:
                for y in range(5):
                    for x in range(5):
                        px[68 * 8 + q * 5 + x, 16 + r * 5 + y] = colours[ch]
    rows = [
        (1, [(HI, "Rodent BIOS v0.3")]),
        (2, [(TXT, "Copyleft 2026, built on nine resistors")]),
        (4, [(TXT, "Main Processor : ESP32-S3 dual core, 240 MHz")]),
        (5, [(TXT, "Memory Test    : "), (HI, "  8192K OK"), (TXT, "   (external)")]),
        (6, [(TXT, "               : "), (HI, "   376K OK"), (TXT, "   (internal)")]),
        (8, [(TXT, "Detecting video ....... VGA 640x480, 256 colours")]),
        (9, [(TXT, "Detecting drive C: .... 11 MB flash disk")]),
        (10, [(TXT, "Detecting keyboard .... serial debug link")]),
        (11, [(TXT, "Detecting sound ....... none")]),
        (13, [(TXT, "Boot device ........... ota_0, 1.4 MB, image OK")]),
        (15, [(YEL, "Starting Chinchilla...")]),
        (17, [(TXT, "Press any key to continue")]),
        (18, [(HI, "Booting in 5")]),
        (28, [(DARK, "Press DEL to enter SETUP (there is no setup)")]),
        (29, [(DARK, "17/11/2026-ESP32S3-N16R8-CHINCHILLA")]),
    ]
    for row, parts in rows:
        col = 2
        for colour, text in parts:
            put(img, g, col, row, text, colour)
            col += len(text)
    return img


# ---- The BIOS's first screen (the owner's idea, 2026-11-17): as old BIOSes
# opened with their maker's logo and an energy badge, ours with the mascot
# and a badge of our own. Shown while the monitor wakes. core/src/bios/
# post.cpp draws the same; the PC test compares the two. ----
LEAF12 = [
    ".........GGG", ".......GGGGG", ".....GGGYGGG", "....GGGYYGGG", "...GGGYYGGG.", "..GGGYYYYGG.",
    "..GGGGGYYGG.", ".GGGGGYYGG..", ".GGGGYYGGG..", ".GGGYGGGG...", "D.GGGGGG....", "DD.........."]
LEAF, LEAF_DARK = c(1, 6, 1), c(1, 3, 0)

def squares(img, rows, x0, y0, scale, colours):
    px = img.load()
    for r, line in enumerate(rows):
        for q, ch in enumerate(line):
            if ch in colours:
                for y in range(scale):
                    for x in range(scale):
                        px[x0 + q * scale + x, y0 + r * scale + y] = colours[ch]

def splash_reference(g):
    img = Image.new("RGB", (640, 480), BLACK)
    squares(img, MASCOT16, 4 * 8, 2 * 16, 8,
            {"G": TXT, "D": TXT, "L": HI, "P": c(7, 4, 2), "K": BLACK, "N": c(7, 3, 2)})
    squares(img, LEAF12, 62 * 8, 2 * 16, 4, {"G": LEAF, "Y": YEL, "D": LEAF_DARK})
    for col, row, colour, text in [
        (58, 7, LEAF, "Energy Nibbler"),
        (56, 8, TXT, "Runs on a USB port"),
        (4, 12, HI, "Rodent BIOS v0.3"),
        (4, 13, TXT, "Copyleft 2026, built on nine resistors"),
        (4, 16, DARK, "Waking the monitor..."),
        (2, 28, DARK, "Press DEL to enter SETUP (there is no setup)"),
        (2, 29, DARK, "17/11/2026-ESP32S3-N16R8-CHINCHILLA"),
    ]:
        put(img, g, col, row, text, colour)
    # The ring round the leaf, two pixels: the midpoint circle, radius 37
    # and 36, about the leaf's middle.
    px = img.load()
    for radius in (37, 36):
        x, y, err = radius, 0, 1 - radius
        while x >= y:
            for dx, dy in ((x, y), (y, x), (-y, x), (-x, y), (-x, -y), (-y, -x), (y, -x), (x, -y)):
                px[520 + dx, 56 + dy] = LEAF
            y += 1
            if err < 0:
                err += 2 * y + 1
            else:
                x -= 1
                err += 2 * (y - x) + 1
    return img


splash_reference(FONTS[0][3]).save(f"{OUT}/bios-splash.png")
post_reference(FONTS[0][3]).save(f"{OUT}/bios-post.png")

# comparison sheet: the same lines in each font, twice the size
sheet = Image.new("RGB", (640, 480), BLACK)
lab = FONTS[0][3]
y = 0
for key, name, note, g in FONTS:
    strip = Image.new("RGB", (320, 32), BLACK)
    put(strip, g, 0, 0, "Memory Test :  8192K OK  (external)", TXT)
    put(strip, g, 0, 1, "Detecting drive C: .. 12 MB @#0O1lI", HI)
    row = y * 96
    put(sheet, lab, 1, y * 6, f"{key[0].upper()}. {name}", YEL)
    put(sheet, lab, 1 + 20, y * 6, note[:57], DIM)
    sheet.paste(strip.resize((640, 64), Image.NEAREST).crop((0, 0, 624, 64)), (8, row + 20))
    y += 1
if len(FONTS) > 1:
    sheet.save(f"{OUT}/bios-text-fonts.png")
