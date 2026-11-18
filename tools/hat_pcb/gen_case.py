#!/usr/bin/env python3
"""3D model of the hat and a first two-part printed case, as OpenSCAD.

Reads the routed board and the cut-out list from gen_enclosure.py and writes
hardware/hat/case/hat-case.scad. The parts on the board are simple blocks sized
from typical datasheet values; they are there to check the case, not to be exact.

    python3 tools/hat_pcb/gen_case.py            # write the .scad
    python3 tools/hat_pcb/gen_case.py render     # also PNG views and STL files
                                                 # (needs openscad and xvfb-run)

Model frame: x as on the board, y measured from the back (connector) edge, z up
from the board's top surface.
"""
import os
import subprocess
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(__file__))
import gen_enclosure  # noqa: E402
import gen_pcb  # noqa: E402

W, H = gen_pcb.W, gen_pcb.H
OUT = os.path.join(gen_pcb.OUT, "case")
SCAD = os.path.join(OUT, "hat-case.scad")

# Case parameters, mm
GAP = 0.5        # between board edge and wall
WALL = 2.0
FLOOR = 2.0
STANDOFF = 4.0   # under the board, room for through-hole leads
BOARD_T = 1.6
INNER_H = 18.0   # above the board's top surface
ROOF = 2.0
DEVKIT_Z = 11.0  # underside of the dev board above the hat (8.5 socket + 2.5 header)


def mm(v, off):
    return pcbnew.ToMM(v) - off


def kbox(x0, y0, x1, y1, z0, z1, col):
    """Box given in board coordinates (y down), as an OpenSCAD statement."""
    return (f'color("{col}") translate([{x0:.2f},{H - y1:.2f},{z0:.2f}]) '
            f'cube([{x1 - x0:.2f},{y1 - y0:.2f},{z1 - z0:.2f}]);')


def kcyl(x, y, z0, z1, d, col):
    return (f'color("{col}") translate([{x:.2f},{H - y:.2f},{z0:.2f}]) '
            f'cylinder(d={d:.2f},h={z1 - z0:.2f});')


def board_parts(board):
    o = []
    fp = board.FindFootprintByReference
    pos = lambda r: (mm(fp(r).GetPosition().x, gen_pcb.OX), mm(fp(r).GetPosition().y, gen_pcb.OY))
    cuts = {(w, n): (c, wd, zc, zh) for w, n, c, wd, zc, zh in gen_enclosure.cutouts(board)}

    # small SMD parts and chips, from their courtyards
    for f in board.GetFootprints():
        ref = f.GetReference()
        if not (ref[0] in "RCU" and ref[1:].isdigit()) or ref in ("C12", "C13"):
            continue
        bb = f.GetCourtyard(pcbnew.F_CrtYd).BBox()
        x0, y0 = mm(bb.GetLeft(), gen_pcb.OX) + 0.3, mm(bb.GetTop(), gen_pcb.OY) + 0.3
        x1, y1 = mm(bb.GetRight(), gen_pcb.OX) - 0.3, mm(bb.GetBottom(), gen_pcb.OY) - 0.3
        hgt, col = (1.5, "#222") if ref[0] == "U" else (0.6, "#222" if ref[0] == "R" else "#b8956a")
        o.append(kbox(x0, y0, x1, y1, 0, hgt, col))

    # dev board sockets, dev board, module, USB-C sockets
    sx = gen_pcb.SOCK_X0
    for y in (gen_pcb.J2_Y, gen_pcb.J1_Y):
        o.append(kbox(sx - 1.27, y - 1.27, sx + 21 * 2.54 + 1.27, y + 1.27, 0, 8.5, "#222"))
        o.append(kbox(sx - 1.27, y - 1.27, sx + 21 * 2.54 + 1.27, y + 1.27, 8.5, DEVKIT_Z, "#333"))
    yc = (gen_pcb.J2_Y + gen_pcb.J1_Y) / 2
    dx0 = W - 70.0
    o.append(kbox(dx0, yc - 14, W - 0.6, yc + 14, DEVKIT_Z, DEVKIT_Z + 1.6, "#1a1a1a"))
    o.append(kbox(dx0, yc - 9, dx0 + 25.5, yc + 9, DEVKIT_Z + 1.6, DEVKIT_Z + 2.4, "#1a1a1a"))
    o.append(kbox(dx0 + 6.5, yc - 8, dx0 + 24.5, yc + 8, DEVKIT_Z + 2.4, DEVKIT_Z + 4.7, "silver"))
    for dy in (-8.0, 8.0):
        o.append(kbox(W - 7.4, yc + dy - 4.5, W, yc + dy + 4.5, DEVKIT_Z + 1.6, DEVKIT_Z + 4.8, "silver"))

    # VGA: body, D shell and screw posts sticking out of the back edge
    c = cuts[("back", "VGA")][0]
    o.append(kbox(c - 15.4, H - 9.5, c + 15.4, H, 0, 12.5, "#2456a6"))
    o.append(kbox(c - 8.3, H, c + 8.3, H + 6.0, 2.3, 10.3, "silver"))
    for d in (-12.5, 12.5):
        o.append(f'color("silver") translate([{c + d:.2f},0,6.3]) rotate([90,0,0]) cylinder(d=5,h=5,$fn=6);')

    # stacked USB-A pairs
    c = cuts[("back", "USB-A x2 (stacked)")][0]
    o.append(kbox(c - 7.2, H - 17.5, c + 7.2, H + 0.4, 0, 15.6, "silver"))
    for z in (1.6, 9.2):
        o.append(kbox(c - 6.2, H + 0.4, c + 6.2, H + 0.5, z, z + 5.0, "#111"))
    c = cuts[("front", "USB-A x2 (stacked)")][0]
    o.append(kbox(c - 7.2, -0.4, c + 7.2, 17.5, 0, 15.6, "silver"))
    for z in (1.6, 9.2):
        o.append(kbox(c - 6.2, -0.5, c + 6.2, -0.4, z, z + 5.0, "#111"))

    # audio jack
    c = gen_pcb.JACK_X
    o.append(kbox(c - 6.0, H - 12.0, c + 6.0, H - 0.5, 0, 6.0, "#222"))
    o.append(f'color("#222") translate([{c:.2f},0.5,3.0]) rotate([90,0,0]) cylinder(d=6,h=3.5);')

    # microSD socket with a card, coin cell, capacitor, crystal, piezo header
    bb = fp("J5").GetBoundingBox(False, False)
    y0, y1 = mm(bb.GetTop(), gen_pcb.OY), mm(bb.GetBottom(), gen_pcb.OY)
    o.append(kbox(0.3, y0, 13.4, y1, 0, 1.9, "silver"))
    o.append(kbox(-2.75, (y0 + y1) / 2 - 5.5, 8.0, (y0 + y1) / 2 + 5.5, 0.6, 1.4, "#444"))
    x, y = pos("BT1")
    o.append(kcyl(x, y, 0, 4.5, 23.0, "#222"))
    o.append(kcyl(x, y, 4.5, 5.2, 20.0, "silver"))
    x, y = pos("C12")
    o.append(kcyl(x + 1.25, y, 0, 11.0, 6.3, "#1d2f6f"))
    x, y = pos("C13")
    o.append(kcyl(x + 1.75, y, 0, 11.5, 8.0, "#1d2f6f"))
    x, y = pos("Y1")
    o.append(kbox(x - 3.1, y - 2.3, x + 8.0, y + 2.3, 0, 13.5, "silver"))
    x, y = pos("J8")
    o.append(kbox(x - 1.27, y - 1.27, x + 1.27, y + 3.81, 0, 2.5, "#222"))
    for dy in (0, 2.54):
        o.append(kbox(x - 0.32, y + dy - 0.32, x + 0.32, y + dy + 0.32, 2.5, 8.5, "gold"))
    return "\n  ".join(o)


def case_cutouts(board):
    """Slots in the lid's walls, open at the bottom so the lid drops over the sockets."""
    o = []
    usbc = None
    for wall, name, c, w, zc, zh in gen_enclosure.cutouts(board):
        if name == "VGA":
            w = 31.5          # one slot for the D shell and both screw posts
        z1 = zc + zh / 2
        if wall == "back":
            o.append(f"translate([{c - w/2:.2f},-GAP-WALL-1,-1]) cube([{w:.2f},WALL+2,{z1 + 1:.2f}]);")
        elif wall == "front":
            o.append(f"translate([{c - w/2:.2f},H+GAP-1,-1]) cube([{w:.2f},WALL+2,{z1 + 1:.2f}]);")
        elif wall == "left":
            o.append(f"translate([-GAP-WALL-1,{H - c - w/2:.2f},-1]) cube([WALL+2,{w:.2f},{z1 + 1:.2f}]);")
        else:
            o.append(f"translate([W+GAP-1,{H - c - w/2:.2f},-1]) cube([WALL+2,{w:.2f},{z1 + 1:.2f}]);")
            usbc = (H - c, w, zc - zh / 2)
    return "\n    ".join(o), usbc


def scad(board):
    holes = [(mm(f.GetPosition().x, gen_pcb.OX), H - mm(f.GetPosition().y, gen_pcb.OY))
             for f in board.GetFootprints() if f.GetReference().startswith("H")]
    cuts, usbc = case_cutouts(board)
    return f"""// Generated by tools/hat_pcb/gen_case.py. Edit the script, not this file.
// Chinchilla hat: board model and a two-part printed case (tray + lid).
// part = "all" | "board" | "base" | "lid" | "closed" | "exploded" | "open"
part = "all";
$fn = 48;

W = {W}; H = {H};
GAP = {GAP}; WALL = {WALL}; FLOOR = {FLOOR}; STANDOFF = {STANDOFF};
BOARD_T = {BOARD_T}; INNER_H = {INNER_H}; ROOF = {ROOF};
HOLES = {[[round(x, 2), round(y, 2)] for x, y in holes]};

module rbox(size, r) {{
  hull() for (x = [r, size[0] - r], y = [r, size[1] - r])
    translate([x, y, 0]) cylinder(r = r, h = size[2]);
}}

module board() {{
  color("#1f6b3a") difference() {{
    translate([0, 0, -BOARD_T]) rbox([W, H, BOARD_T], 2);
    for (p = HOLES) translate([p[0], p[1], -BOARD_T - 1]) cylinder(d = 3.2, h = BOARD_T + 2);
  }}
  {board_parts(board)}
}}

// Tray: floor, low walls up to the board's top surface, four standoffs,
// and a tab that closes the lid's slot below the dev board's USB-C sockets.
module base() {{
  zb = -BOARD_T - STANDOFF;
  difference() {{
    translate([-GAP - WALL, -GAP - WALL, zb - FLOOR])
      rbox([W + 2 * GAP + 2 * WALL, H + 2 * GAP + 2 * WALL, FLOOR + STANDOFF + BOARD_T], 3);
    translate([-GAP, -GAP, zb]) cube([W + 2 * GAP, H + 2 * GAP, STANDOFF + BOARD_T + 1]);
  }}
  for (p = HOLES) translate([p[0], p[1], zb]) difference() {{
    cylinder(d = 7, h = STANDOFF);
    translate([0, 0, -FLOOR + 0.6]) cylinder(d = 2.8, h = STANDOFF + FLOOR);
  }}
  translate([W + GAP, {usbc[0] - usbc[1] / 2 + 0.3:.2f}, 0]) cube([WALL, {usbc[1] - 0.6:.2f}, {usbc[2] - 0.3:.2f}]);
}}

// Lid: walls and roof. Its wall slots are open at the bottom, so it drops over
// the sockets. Four posts press the board onto the standoffs; M3 x 25 screws
// go through the roof, the posts and the board into the standoffs.
module lid() {{
  difference() {{
    translate([-GAP - WALL, -GAP - WALL, 0])
      rbox([W + 2 * GAP + 2 * WALL, H + 2 * GAP + 2 * WALL, INNER_H + ROOF], 3);
    translate([-GAP, -GAP, -1]) cube([W + 2 * GAP, H + 2 * GAP, INNER_H + 1]);
    {cuts}
    for (p = HOLES) translate([p[0], p[1], 0]) {{
      cylinder(d = 3.4, h = INNER_H + ROOF + 1);
      translate([0, 0, INNER_H + ROOF - 1.2]) cylinder(d = 6.2, h = 2);
    }}
  }}
  for (p = HOLES) translate([p[0], p[1], 0.2]) difference() {{
    cylinder(d = 7, h = INNER_H);
    translate([0, 0, -1]) cylinder(d = 3.4, h = INNER_H + 2);
  }}
}}

CASE = "#d9d4c7";
if (part == "board") board();
if (part == "base") base();
if (part == "lid") lid();
if (part == "all" || part == "closed") {{ board(); color(CASE) base(); color(CASE) lid(); }}
if (part == "open") {{ board(); color(CASE) base(); }}
if (part == "exploded") {{
  board(); color(CASE) translate([0, 0, -22]) base(); color(CASE) translate([0, 0, 40]) lid();
}}
"""


VIEWS = [  # name, part, camera rotation (x, y, z)
    ("board-back", "board", (55, 0, 20)),
    ("board-front", "board", (55, 0, 200)),
    ("board-top", "board", (0, 0, 0)),
    ("case-open", "open", (55, 0, 25)),
    ("case-back", "closed", (60, 0, 25)),
    ("case-front", "closed", (60, 0, 215)),
    ("case-right", "closed", (60, 0, 115)),
    ("case-exploded", "exploded", (65, 0, 30)),
]


def render():
    for name, part, rot in VIEWS:
        cam = f"{W/2},{H/2},5,{rot[0]},{rot[1]},{rot[2]},0"
        subprocess.run(["xvfb-run", "-a", "openscad", "-o", os.path.join(OUT, name + ".png"),
                        "-D", f'part="{part}"', "--camera", cam, "--viewall", "--autocenter",
                        "--imgsize", "1600,1100", "--colorscheme", "Tomorrow",
                        "--projection", "p", SCAD], check=True, capture_output=True)
        print("rendered", name)
    for part in ("base", "lid"):
        subprocess.run(["openscad", "-o", os.path.join(OUT, f"hat-case-{part}.stl"),
                        "-D", f'part="{part}"', SCAD], check=True, capture_output=True)
        print("exported", part)


def main():
    os.makedirs(OUT, exist_ok=True)
    board = pcbnew.LoadBoard(gen_pcb.PCB)
    with open(SCAD, "w") as f:
        f.write(scad(board))
    print("wrote", SCAD)
    if len(sys.argv) > 1 and sys.argv[1] == "render":
        render()


if __name__ == "__main__":
    main()
