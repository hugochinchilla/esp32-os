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

for key, name, note, g in FONTS:
    screen(g).save(f"{OUT}/bios-text-{key}.png")

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
sheet.save(f"{OUT}/bios-text-fonts.png")
