#!/usr/bin/env python3
"""Enclosure reference drawing for the hat: board outline, M3 holes and the wall
cut-outs, read from the routed board so it stays in step with the layout.

Writes hardware/hat/enclosure.svg, enclosure.png and enclosure-cutouts.csv.
Heights above the board come from typical datasheet values and the dev board
stack (socket 8.5 mm + header plastic 2.5 mm); check them against real parts.
"""
import csv
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(__file__))
import gen_pcb  # noqa: E402

OUT = gen_pcb.OUT
W, H = gen_pcb.W, gen_pcb.H


def mm(v, off):
    return round(pcbnew.ToMM(v) - off, 2)


def cutouts(board):
    fp = board.FindFootprintByReference
    big = lambda f: sorted(mm(p.GetPosition().x, gen_pcb.OX) for p in f.Pads()
                           if p.GetDrillSize().x > pcbnew.FromMM(2))
    vga = big(fp("J3"))
    usb = big(fp("J6"))
    vga_c = round((vga[0] + vga[-1]) / 2, 2)
    usb_c = round((usb[0] + usb[-1]) / 2, 2)
    usb2 = big(fp("J9"))
    usb2_c = round((usb2[0] + usb2[-1]) / 2, 2)
    devy = (gen_pcb.J2_Y + gen_pcb.J1_Y) / 2
    sd = fp("J5").GetBoundingBox(False, False)
    sd_y = round((mm(sd.GetTop(), gen_pcb.OY) + mm(sd.GetBottom(), gen_pcb.OY)) / 2, 2)
    # wall, name, centre along the wall (x for back, y for left/right), width, z centre, height
    return [
        ("back", "VGA", vga_c, 19.0, 6.3, 10.0),
        ("back", "USB-A x2 (stacked)", usb_c, 15.0, 7.8, 16.0),
        ("back", "Audio jack", gen_pcb.JACK_X, 7.0, 3.0, 7.0),
        ("front", "USB-A x2 (stacked)", usb2_c, 15.0, 7.8, 16.0),
        ("left", "microSD slot", sd_y, 13.0, 1.0, 2.5),
        ("right", "DevKitC-1 USB-C x2", devy, 30.0, 14.2, 7.5),
    ]


def svg(board, cuts):
    s = 6.0                      # px per mm
    m = 40                       # margin, px
    elev_h = 24.0                # mm drawn for the wall elevations
    wd = W * s + 2 * m + 150
    ht = (H + elev_h + 14) * s + 3 * m
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{wd:.0f}" height="{ht:.0f}" '
         f'font-family="DejaVu Sans, sans-serif" font-size="11">',
         f'<rect width="100%" height="100%" fill="white"/>']
    X = lambda x: m + x * s
    Y = lambda y: m + y * s
    o.append(f'<text x="{m}" y="{m-18}" font-size="15" font-weight="bold">Hat top view, '
             f'{W:.0f} x {H:.0f} mm, origin at the top-left corner</text>')
    o.append(f'<rect x="{X(0)}" y="{Y(0)}" width="{W*s}" height="{H*s}" rx="{2*s}" '
             f'fill="#e8f2e8" stroke="black" stroke-width="2"/>')
    # dev board footprint
    x0 = gen_pcb.SOCK_X0 - (70.0 - 21 * 2.54 - gen_pcb.DEVKIT_USB_OVERHANG)
    y0 = (gen_pcb.J2_Y + gen_pcb.J1_Y) / 2 - 14
    o.append(f'<rect x="{X(x0)}" y="{Y(y0)}" width="{(W-x0)*s}" height="{28*s}" fill="none" '
             f'stroke="#555" stroke-dasharray="6,4"/>')
    o.append(f'<text x="{X(x0+3)}" y="{Y(y0+15)}" fill="#555">ESP32-S3-DevKitC-1 (70 x 28 mm, '
             f'on sockets, about 16 mm tall)</text>')
    for f in board.GetFootprints():
        if f.GetReference().startswith("H"):
            x, y = mm(f.GetPosition().x, gen_pcb.OX), mm(f.GetPosition().y, gen_pcb.OY)
            o.append(f'<circle cx="{X(x)}" cy="{Y(y)}" r="{1.6*s}" fill="white" stroke="black"/>')
            o.append(f'<text x="{X(x)}" y="{Y(y)+(3.4 if y < H/2 else -2.6)*s}" text-anchor="middle">'
                     f'M3 ({x:g}, {y:g})</text>')
    colours = {"back": "#c0392b", "front": "#b9770e", "left": "#2471a3", "right": "#7d3c98"}
    for wall, name, c, w, zc, zh in cuts:
        col = colours[wall]
        if wall == "back":
            o.append(f'<rect x="{X(c-w/2)}" y="{Y(H)-4}" width="{w*s}" height="8" fill="{col}"/>')
            o.append(f'<text x="{X(c)}" y="{Y(H)-10}" text-anchor="middle" fill="{col}">{name}</text>')
            o.append(f'<text x="{X(c)}" y="{Y(H)+20}" text-anchor="middle" fill="{col}">x = {c:g}</text>')
        elif wall == "front":
            o.append(f'<rect x="{X(c-w/2)}" y="{Y(0)-4}" width="{w*s}" height="8" fill="{col}"/>')
            for i, t in enumerate([name, f"x = {c:g}, {w:g} wide", f"z = {zc:g}, {zh:g} tall"]):
                o.append(f'<text x="{X(c)}" y="{Y(0)+20+i*14}" text-anchor="middle" fill="{col}">{t}</text>')
        else:
            xe = 0 if wall == "left" else W
            o.append(f'<rect x="{X(xe)-4}" y="{Y(c-w/2)}" width="8" height="{w*s}" fill="{col}"/>')
            lines = [name, f"y = {c:g}, {w:g} wide", f"z = {zc:g}, {zh:g} tall"]
            for i, t in enumerate(lines):
                o.append(f'<text x="{X(xe)+8}" y="{Y(c)-10+i*14}" fill="{col}">{t}</text>')
    # back wall elevation, seen from outside
    top = Y(H) + 3 * m
    o.append(f'<text x="{m}" y="{top-14}" font-size="15" font-weight="bold">Back wall seen from '
             f'outside (z = height above the hat\'s top surface)</text>')
    zb = top + elev_h * s
    o.append(f'<line x1="{X(0)}" y1="{zb}" x2="{X(W)}" y2="{zb}" stroke="black" stroke-width="2"/>')
    o.append(f'<text x="{X(W)+4}" y="{zb+4}">z = 0 (board top)</text>')
    for z in (5, 10, 15, 20):
        o.append(f'<line x1="{X(0)}" y1="{zb-z*s}" x2="{X(W)}" y2="{zb-z*s}" stroke="#ddd"/>')
        o.append(f'<text x="{X(0)-4}" y="{zb-z*s+4}" text-anchor="end" fill="#888">{z}</text>')
    for wall, name, c, w, zc, zh in cuts:
        if wall != "back":
            continue
        z0 = max(zc - zh / 2, 0)
        o.append(f'<rect x="{X(c-w/2)}" y="{zb-(z0+zh)*s}" width="{w*s}" height="{zh*s}" '
                 f'fill="none" stroke="{colours[wall]}" stroke-width="2"/>')
        o.append(f'<text x="{X(c)}" y="{zb-(z0+zh)*s-4}" text-anchor="middle" '
                 f'fill="{colours[wall]}">{w:g} x {zh:g}</text>')
        if name == "VGA":   # the two jack-screw posts beside the D shell
            for dx in (-12.5, 12.5):
                o.append(f'<circle cx="{X(c+dx)}" cy="{zb-zc*s}" r="{1.75*s}" fill="none" '
                         f'stroke="{colours[wall]}" stroke-width="2"/>')
            o.append(f'<text x="{X(c)}" y="{zb+16}" text-anchor="middle" fill="{colours[wall]}">'
                     f'+ two 3.5 mm holes 25 mm apart</text>')
    o.append("</svg>")
    return "\n".join(o)


def main():
    board = pcbnew.LoadBoard(gen_pcb.PCB)
    cuts = cutouts(board)
    path = os.path.join(OUT, "enclosure.svg")
    with open(path, "w") as f:
        f.write(svg(board, cuts))
    try:
        import cairosvg
        cairosvg.svg2png(url=path, write_to=os.path.join(OUT, "enclosure.png"), output_width=1300)
    except ImportError:
        pass
    with open(os.path.join(OUT, "enclosure-cutouts.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Wall", "Opening", "Centre along wall (mm)", "Width (mm)",
                    "Centre height above board (mm)", "Height (mm)"])
        for row in cuts:
            w.writerow(row)
    for row in cuts:
        print(row)


if __name__ == "__main__":
    main()
