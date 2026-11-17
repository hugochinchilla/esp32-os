"""Build hardware/hat/hat.kicad_pcb from parts.py: placement, routing, ground pours.

Steps (run from the repository root, needs KiCad 7+ Python and Java):
    python3 tools/hat_pcb/gen_pcb.py place      # placed, unrouted board + Specctra DSN
    python3 tools/hat_pcb/gen_pcb.py route      # runs Freerouting, imports the routes, pours ground
    python3 tools/hat_pcb/gen_pcb.py all        # both

Freerouting: set FREEROUTING_JAR to the jar (tested with freerouting-1.9.0.jar).
"""
import math
import os
import re
import subprocess
import sys
import uuid

import pcbnew

sys.path.insert(0, os.path.dirname(__file__))
import parts  # noqa: E402
from gen_sch import uid  # noqa: E402

FPDIR = "/usr/share/kicad/footprints"
OUT = os.path.join("hardware", "hat")
PCB = os.path.join(OUT, "hat.kicad_pcb")
DSN = os.path.join(OUT, "build", "hat.dsn")
SES = os.path.join(OUT, "build", "hat.ses")

W, H = 100.0, 78.0          # board size, mm
OX, OY = 100.0, 100.0       # board origin on the KiCad page
POWER_NETS = ["GND", "+5V", "+3V3", "VBUS_DN", "HUB_3V3", "VBAT"]

# Dev board sockets: right header (J2) on top, left header (J1) 25.4 mm below,
# pin 1 at the left (antenna end); the dev board's USB-C sockets face the right edge.
# The sockets are placed so the USB-C fronts line up with the hat's right edge, which
# sits against the enclosure's right wall. DEVKIT_USB_OVERHANG is the distance from
# the pin-22 centre to the USB-C front: an estimate (board is 70 mm long), to be
# measured on a real DevKitC-1 before ordering.
DEVKIT_USB_OVERHANG = 5.0
SOCK_X0 = W - DEVKIT_USB_OVERHANG - 21 * 2.54
J2_Y, J1_Y = 24.0, 49.4


def pin_x(n):
    return SOCK_X0 + (n - 1) * 2.54


# Back panel (bottom edge): VGA at the left end (room for the cable's thumbscrews), USB-A pair and audio jack, fronts at the board edge,
# so a 3D-printed case needs cut-outs in one wall (plus microSD on the left wall and
# the dev board's USB-C sockets on the right wall).
VGA_X = 28.8     # pin 1 of the HD15; body spans VGA_X - 19.8 .. VGA_X + 11.2
USB_X = 63.9     # pin 1 of the USB-A pair; body spans USB_X - 3.8 .. USB_X + 10.8
USB2_X = 73.7    # pin 1 of the second USB-A pair, on the front (top) edge, turned 180
JACK_X = 85.5    # jack centre; the footprint draws its own notch in the edge (+-6.5 mm)

# ref: (x, y, rotation in degrees), board coordinates (mm, y down)
PLACE = {
    "J2": (SOCK_X0, J2_Y, 90), "J1": (SOCK_X0, J1_Y, 90),
    "J6": (USB_X, H - 15.66, 0),
    "J9": (USB2_X, 15.66, 180), "U6": (65.5, 28.5, 0), "U7": (72.5, 28.5, 0),
    "J3": (VGA_X, H - 6.99, 0), "R32": (33.0, 15.5, 0), "R33": (33.0, 18.0, 0),
    "J4": (JACK_X, H - 4.5, 0), "J8": (96.0, 55.0, 0),
    "J5": (7.0, 15.5, 90),
    "BT1": (16.5, 56.0, 0),
    "U1": (13.0, 32.0, 0), "C3": (13.0, 27.0, 0), "C4": (13.0, 37.0, 0),
    "R21": (19.5, 29.5, 90), "R22": (21.5, 29.5, 90), "R23": (19.5, 35.0, 90),
    "R24": (19.5, 7.5, 0), "R25": (19.5, 9.7, 0), "R26": (19.5, 11.9, 0),
    "R27": (19.5, 14.1, 0), "R28": (19.5, 16.3, 0), "R29": (19.5, 18.5, 0),
    "C5": (25.0, 11.0, 90), "C6": (27.5, 11.0, 90),
    "J7": (40.0, 18.0, 90),
    "U2": (86.0, 12.5, 0), "Y1": (84.0, 5.0, 0),
    "C7": (95.0, 9.5, 90), "C8": (92.5, 13.0, 90), "C9": (95.0, 13.0, 90),
    "U4": (83.5, 55.5, 0), "U5": (83.5, 59.5, 0),
    "U3": (55.0, 10.0, 0), "C10": (55.0, 5.0, 0), "C11": (49.0, 8.0, 90), "C12": (40.5, 8.0, 0),
    "R30": (59.5, 9.0, 90), "R31": (59.5, 12.5, 90),
    "R20": (87.5, 56.0, 90), "C1": (89.5, 56.0, 90), "C2": (91.6, 56.0, 90),
}
# VGA ladder resistors sit below the dev board pin they hang from, in two rows
for ref, gpio, row in parts.VGA_LADDER:
    PLACE[ref] = (pin_x(parts.DEVKIT_LEFT.index(gpio) + 1), 54.4 + 3.2 * row, 90)

HOLES = [(4.0, 3.8), (96.0, 3.8), (4.0, 74.2), (96.0, 74.2)]
FIDUCIALS = [(32.0, 3.0), (97.0, 45.0), (3.0, 40.0)]
ANTENNA_KEEPOUT = (SOCK_X0 - 12.0, 20.0, SOCK_X0, 52.0)   # no copper pour under the module antenna


def V(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(OX + x), pcbnew.FromMM(OY + y))


def load_fp(libfp):
    lib, name = libfp.split(":")
    fp = pcbnew.FootprintLoad(f"{FPDIR}/{lib}.pretty", name)
    if fp is None:
        raise KeyError(libfp)
    return fp


def add_line(board, layer, a, b, width=0.15):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(*a))
    s.SetEnd(V(*b))
    s.SetLayer(layer)
    s.SetWidth(pcbnew.FromMM(width))
    board.Add(s)


def add_rect(board, layer, x0, y0, x1, y1, width=0.15):
    for a, b in [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]:
        add_line(board, layer, a, b, width)


def add_text(board, text, x, y, size=1.0, layer=pcbnew.F_SilkS, angle=0, bold=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(V(x, y))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(size), pcbnew.FromMM(size)))
    t.SetTextThickness(pcbnew.FromMM(size * (0.2 if bold else 0.15)))
    t.SetTextAngleDegrees(angle)
    board.Add(t)


def add_zone(board, net, layer, pts, name, keepout=False):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    if keepout:
        z.SetIsRuleArea(True)
        z.SetDoNotAllowCopperPour(True)
        z.SetDoNotAllowTracks(False)
        z.SetDoNotAllowVias(False)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
        ls = pcbnew.LSET()
        ls.AddLayer(pcbnew.F_Cu)
        ls.AddLayer(pcbnew.B_Cu)
        z.SetLayerSet(ls)
    else:
        z.SetNet(net)
        z.SetLocalClearance(pcbnew.FromMM(0.3))
        z.SetMinThickness(pcbnew.FromMM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(pcbnew.FromMM(0.4))
        z.SetThermalReliefSpokeWidth(pcbnew.FromMM(0.5))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    z.SetZoneName(name)
    ol = z.Outline()
    ol.NewOutline()
    for x, y in pts:
        ol.Append(pcbnew.FromMM(OX + x), pcbnew.FromMM(OY + y))
    board.Add(z)
    return z


def build():
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.m_TrackMinWidth = pcbnew.FromMM(0.2)
    ds.m_MinClearance = pcbnew.FromMM(0.2)
    ds.m_ViasMinSize = pcbnew.FromMM(0.6)
    ds.m_MinThroughDrill = pcbnew.FromMM(0.3)
    ds.m_CopperEdgeClearance = pcbnew.FromMM(0.3)
    ds.m_HoleClearance = pcbnew.FromMM(0.25)
    ds.m_HoleToHoleMin = pcbnew.FromMM(0.25)
    ds.SetAuxOrigin(V(0, H))      # placement file origin: bottom-left corner of the board
    ds.m_MinResolvedSpokes = 1   # a pad also reached by a track may have a single spoke

    netinfo = {}
    for i, net in enumerate(sorted(parts.nets())):
        ni = pcbnew.NETINFO_ITEM(board, net)
        board.Add(ni)
        netinfo[net] = ni

    for p in parts.PARTS:
        fp = load_fp(p["fp"])
        fp.SetReference(p["ref"])
        fp.SetValue(p["value"])
        fp.SetFPIDAsString(p["fp"])
        fp.SetPath(pcbnew.KIID_PATH("/" + uid("sym", p["ref"])))
        x, y, rot = PLACE[p["ref"]]
        fp.SetPosition(V(x, y))
        fp.SetOrientationDegrees(rot)
        for k, name in (("mpn", "MPN"), ("mfr", "Manufacturer"), ("lcsc", "LCSC")):
            if p[k]:
                fp.SetProperty(name, p[k])
        attrs = fp.GetAttributes()
        if p["dnp"]:
            attrs |= pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES
        elif p["who"] == "self":
            attrs |= pcbnew.FP_EXCLUDE_FROM_POS_FILES   # hand soldered: not in the placement file
        fp.SetAttributes(attrs)
        for pad in fp.Pads():
            num = pad.GetNumber()
            if num in p["pins"]:
                net = p["pins"][num]
                if net:
                    pad.SetNet(netinfo[net])
            elif num != "":
                raise KeyError(f"{p['ref']} pad {num} not in parts.py")
        # small reference text, kept on the fab layer for the assembly drawing
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(0.8), pcbnew.FromMM(0.8)))
        fp.Reference().SetTextThickness(pcbnew.FromMM(0.12))
        if p["sym"] in ("Device:R", "Device:C"):
            # passives are too dense for readable silkscreen; their names stay on the fab layer
            fp.Reference().SetLayer(pcbnew.F_Fab)
        if p["ref"] == "J5":
            fp.Reference().SetPosition(V(11.0, 6.0))
            fp.Reference().SetTextAngleDegrees(0)
        board.Add(fp)

    for i, (x, y) in enumerate(HOLES):
        fp = load_fp("MountingHole:MountingHole_3.2mm_M3")
        fp.SetFPIDAsString("MountingHole:MountingHole_3.2mm_M3")
        fp.SetReference(f"H{i+1}")
        fp.SetPosition(V(x, y))
        fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_BOARD_ONLY | pcbnew.FP_EXCLUDE_FROM_BOM
                         | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
        fp.Reference().SetVisible(False)
        board.Add(fp)
    for i, (x, y) in enumerate(FIDUCIALS):
        fp = load_fp("Fiducial:Fiducial_1mm_Mask2mm")
        fp.SetFPIDAsString("Fiducial:Fiducial_1mm_Mask2mm")
        fp.SetReference(f"FID{i+1}")
        fp.SetPosition(V(x, y))
        fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_BOARD_ONLY | pcbnew.FP_EXCLUDE_FROM_BOM)
        fp.Reference().SetVisible(False)
        board.Add(fp)

    # outline with 2 mm rounded corners
    r = 2.0
    add_line(board, pcbnew.Edge_Cuts, (r, 0), (W - r, 0))
    add_line(board, pcbnew.Edge_Cuts, (W, r), (W, H - r))
    add_line(board, pcbnew.Edge_Cuts, (W - r, H), (JACK_X + 6.5, H))
    add_line(board, pcbnew.Edge_Cuts, (JACK_X - 6.5, H), (r, H))
    add_line(board, pcbnew.Edge_Cuts, (0, H - r), (0, r))
    for cx, cy, a0 in [(r, r, 180), (W - r, r, 270), (W - r, H - r, 0), (r, H - r, 90)]:
        arc = pcbnew.PCB_SHAPE(board)
        arc.SetShape(pcbnew.SHAPE_T_ARC)
        arc.SetLayer(pcbnew.Edge_Cuts)
        arc.SetWidth(pcbnew.FromMM(0.15))
        pt = [(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)))
              for a in (a0, a0 + 45, a0 + 90)]
        arc.SetArcGeometry(V(*pt[0]), V(*pt[1]), V(*pt[2]))
        board.Add(arc)

    # silkscreen
    add_rect(board, pcbnew.F_SilkS, SOCK_X0 - 12.0, J2_Y - 2.2, W - 0.6, J1_Y + 2.2, 0.12)
    add_text(board, "ESP32-S3-DevKitC-1, USB-C flush with this edge", SOCK_X0 + 30.0, 30.0, 1.2)
    add_text(board, "pin 1: 3V3", SOCK_X0, J1_Y - 3.0, 0.9)
    add_text(board, "CHINCHILLA HAT v0.3", SOCK_X0 + 30.0, 42.0, 1.6, bold=True)
    add_text(board, "github.com/hugochinchilla/esp32-os", SOCK_X0 + 30.0, 44.6, 0.9)
    add_text(board, "VGA", VGA_X + 5.5, 66.3, 1.2, bold=True)
    add_text(board, "AUDIO", JACK_X, 62.9, 1.0, bold=True)
    add_text(board, "PIEZO", 96.0, 61.0, 0.8, bold=True)
    add_text(board, "3V3", 98.5, 55.0, 0.8)
    add_text(board, "14", 98.2, 57.54, 0.8)
    add_text(board, "microSD", 11.0, 22.6, 1.0, bold=True)
    add_text(board, "CR2032 +", 15.5, 47.5, 1.0, bold=True)
    add_text(board, "EXP: 3V3 38 47 48 5V GND", 46.0, 15.6, 0.8)
    add_text(board, "antenna: keep clear", SOCK_X0 - 6.0, 52.5, 0.8)

    pts = [(0.4, 0.4), (W - 0.4, 0.4), (W - 0.4, H - 0.4), (0.4, H - 0.4)]
    x0, y0, x1, y1 = ANTENNA_KEEPOUT
    add_zone(board, None, pcbnew.F_Cu, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], "antenna", keepout=True)
    return board, netinfo, pts


def add_pours(board, netinfo, pts):
    """Save, reload (the zone filler needs a board loaded from disk), pour ground, save."""
    save(board)
    board = pcbnew.LoadBoard(PCB)
    gnd = board.GetNetsByName()["GND"]
    # Join neighbouring ground pins of the sockets with short tracks, so no sliver of
    # pour is left stranded between their thermal reliefs.
    for fp in board.GetFootprints():
        pads = [p for p in fp.Pads() if p.GetNetname() == "GND" and p.HasHole()]
        if fp.GetReference() in ("J1", "J2"):
            for a in pads:
                a.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
        for a in pads:
            for b in pads:
                if a.GetNumber() < b.GetNumber() and (a.GetPosition() - b.GetPosition()).EuclideanNorm() < pcbnew.FromMM(2.6):
                    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
                        t = pcbnew.PCB_TRACK(board)
                        t.SetStart(a.GetPosition())
                        t.SetEnd(b.GetPosition())
                        t.SetWidth(pcbnew.FromMM(0.5))
                        t.SetLayer(layer)
                        t.SetNet(gnd)
                        board.Add(t)
    for layer, name in ((pcbnew.F_Cu, "GND_top"), (pcbnew.B_Cu, "GND_bottom")):
        add_zone(board, gnd, layer, pts, name)
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    # Stitching vias tie the two ground pours together wherever both have copper
    zones = [z for z in board.Zones() if z.GetZoneName() in ("GND_top", "GND_bottom")]
    n = 0
    for gx in range(4, int(W) - 3, 8):
        for gy in range(4, int(H) - 3, 8):
            ring = [(gx, gy)] + [(gx + 0.75 * math.cos(a * math.pi / 4), gy + 0.75 * math.sin(a * math.pi / 4))
                                 for a in range(8)]
            if all(z.HitTestFilledArea(z.GetLayer(), V(x, y)) for z in zones for x, y in ring):
                via = pcbnew.PCB_VIA(board)
                via.SetPosition(V(gx, gy))
                via.SetWidth(pcbnew.FromMM(0.6))
                via.SetDrill(pcbnew.FromMM(0.3))
                via.SetNet(gnd)
                board.Add(via)
                n += 1
    # Any pour fragment without a track or via of its own gets a via to the other layer
    for z in zones:
        polys = z.GetFilledPolysList(z.GetLayer())
        other = [o for o in zones if o is not z][0]
        for i in range(polys.OutlineCount()):
            o = polys.Outline(i)
            if any(o.PointInside(t.GetPosition() if t.GetClass() == "PCB_VIA" else t.GetStart())
                   for t in board.GetTracks() if t.GetNetname() == "GND"):
                continue
            bb = o.BBox()
            c = bb.Centre()
            cands = [pcbnew.VECTOR2I(c.x + dx * pcbnew.FromMM(0.25), c.y + dy * pcbnew.FromMM(0.25))
                     for dx in range(-40, 41) for dy in range(-40, 41)]
            cands.sort(key=lambda q: (q - c).EuclideanNorm())
            for pt in cands:
                ring = [pcbnew.VECTOR2I(pt.x + int(pcbnew.FromMM(0.75) * math.cos(a * math.pi / 4)),
                                        pt.y + int(pcbnew.FromMM(0.75) * math.sin(a * math.pi / 4))) for a in range(8)]
                if all(o.PointInside(q) and other.HitTestFilledArea(other.GetLayer(), q) for q in ring + [pt]):
                    via = pcbnew.PCB_VIA(board)
                    via.SetPosition(pt)
                    via.SetWidth(pcbnew.FromMM(0.6))
                    via.SetDrill(pcbnew.FromMM(0.3))
                    via.SetNet(gnd)
                    board.Add(via)
                    n += 1
                    break
    print(f"{n} ground stitching vias")
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    save(board)
    return board


def write_dsn(board):
    os.makedirs(os.path.dirname(DSN), exist_ok=True)
    assert pcbnew.ExportSpecctraDSN(board, DSN)
    s = open(DSN).read()
    # power nets get wider tracks: add a net class to the DSN
    nets = " ".join(POWER_NETS)
    cls = (f'    (class power {nets}\n      (circuit (use_via "Via[0-1]_800:400_um"))\n'
           f'      (rule (width 400) (clearance 200))\n    )\n')
    # remove power nets from the default class
    m = re.search(r"\(class kicad_default(.*?)\(circuit", s, re.S)
    body = m.group(1)
    for n in POWER_NETS:
        for q in (n, f'"{n}"'):
            body = re.sub(r"(?<=\s)" + re.escape(q) + r"(?=\s)", "", body)
    s = s[:m.start(1)] + body + s[m.end(1):]
    i = s.find("(class kicad_default")
    s = s[:i] + cls.lstrip() + "    " + s[i:]
    # add the 0.8/0.4 via padstack next to the default one
    if 'Via[0-1]_800:400_um' not in s.split("(network")[0]:
        s = s.replace("(library\n", '(library\n    (padstack "Via[0-1]_800:400_um"\n'
                      '      (shape (circle F.Cu 800 0 0))\n      (shape (circle B.Cu 800 0 0))\n'
                      '      (attach off)\n    )\n', 1)
    # The antenna rule area only forbids copper pour; KiCad exports it as a full keepout.
    s = re.sub(r'\n\s*\(keepout "" \(polygon [^)]*\)\s*\)', "", s)
    # Route 0.25 mm signal tracks, and keep tracks 0.15 mm further from the edge than
    # Freerouting's 0.2 mm clearance by routing inside a slightly smaller outline.
    s = s.replace("(rule\n      (width 200)", "(rule\n      (width 250)")
    s = re.sub(r"\(class kicad_default(.*?)\(rule \(width 200\)", lambda m: m.group(0).replace("(width 200)", "(width 250)"), s, flags=re.S)
    i = 0.15
    pts = [(i, i), (W - i, i), (W - i, H - i), (JACK_X + 6.5, H - i), (JACK_X + 2.2, H - 1.5),
           (JACK_X - 2.2, H - 1.5), (JACK_X - 6.5, H - i), (i, H - i)]
    path = "  ".join(f"{round((OX + x) * 1000)} {round(-(OY + y) * 1000)}" for x, y in pts + pts[:1])
    s = re.sub(r"\(boundary\s*\(path pcb 0 [^)]*\)\s*\)", f"(boundary\n      (path pcb 0  {path})\n    )", s, count=1)
    open(DSN, "w").write(s)


def run_freerouting():
    # Freerouting 1.9 needs a display even in batch mode: run it under xvfb-run on a server.
    jar = os.environ["FREEROUTING_JAR"]
    cmd = ["xvfb-run", "-a", "java", "-jar", jar, "-de", DSN, "-do", SES, "-mp", "100", "-da"]
    print(" ".join(cmd))
    subprocess.run(cmd, check=True, timeout=1500)


def import_ses(board):
    s = open(SES).read()
    res = re.search(r"\(routes\s*\(resolution (\w+) (\d+)\)", s)
    unit, div = res.group(1), int(res.group(2))
    scale = {"um": 1e-3, "mm": 1.0, "mil": 0.0254}[unit] / div   # -> mm
    netmap = {str(k): v for k, v in board.GetNetsByName().items()}
    n_wires = n_vias = 0
    for m in re.finditer(r'\(net ("[^"]*"|\S+)\s*(.*?)\n\s*\)\s*(?=\(net |\)\s*\)\s*\)\s*$)', s.split("(network_out", 1)[1], re.S):
        name = m.group(1).strip('"')
        net = netmap[name]
        body = m.group(2)
        for w in re.finditer(r"\(wire\s*\(path (\S+) (\d+)((?:\s+-?\d+)+)\s*\)", body):
            layer = board.GetLayerID(w.group(1))
            width = int(w.group(2)) * scale
            c = [int(v) * scale for v in w.group(3).split()]
            ptsl = list(zip(c[0::2], c[1::2]))
            for (xa, ya), (xb, yb) in zip(ptsl, ptsl[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(xa), pcbnew.FromMM(-ya)))
                t.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(xb), pcbnew.FromMM(-yb)))
                t.SetWidth(pcbnew.FromMM(width))
                t.SetLayer(layer)
                t.SetNet(net)
                board.Add(t)
                n_wires += 1
        for v in re.finditer(r'\(via "?([^"\s]+)"? (-?\d+) (-?\d+)', body):
            mm = re.search(r"_(\d+):(\d+)_um", v.group(1))
            dia, drill = (int(mm.group(1)) / 1000, int(mm.group(2)) / 1000) if mm else (0.6, 0.3)
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(int(v.group(2)) * scale),
                                            pcbnew.FromMM(-int(v.group(3)) * scale)))
            via.SetWidth(pcbnew.FromMM(dia))
            via.SetDrill(pcbnew.FromMM(drill))
            via.SetNet(net)
            board.Add(via)
            n_vias += 1
    print(f"imported {n_wires} track segments, {n_vias} vias")


def save(board, path=PCB):
    board.SetFileName(path)
    pcbnew.SaveBoard(path, board)
    # net classes live in the project file: wide tracks for power nets
    import json
    pro = path.replace(".kicad_pcb", ".kicad_pro")
    d = json.load(open(pro))
    ns = d["net_settings"]
    default = dict(ns["classes"][0], track_width=0.25)
    power = dict(default, name="Power", track_width=0.4, via_diameter=0.8, via_drill=0.4)
    ns["classes"] = [default, power]
    ns["netclass_patterns"] = [{"netclass": "Power", "pattern": n} for n in POWER_NETS]
    d["meta"]["filename"] = os.path.basename(pro)
    json.dump(d, open(pro, "w"), indent=2, sort_keys=True)


def main(step):
    if step in ("place", "all"):
        board, netinfo, pts = build()
        save(board)
        write_dsn(board)
        print("placed board saved; DSN at", DSN)
    if step in ("route", "all"):
        board, netinfo, pts = build()
        if not os.path.exists(SES) or os.environ.get("REROUTE"):
            run_freerouting()
        import_ses(board)
        board = add_pours(board, netinfo, pts)
        pcbnew.WriteDRCReport(board, os.path.join(OUT, "build", "drc.rpt"), pcbnew.EDA_UNITS_MILLIMETRES, True)
        print("routed board saved; DRC report in", os.path.join(OUT, "build", "drc.rpt"))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "all")
