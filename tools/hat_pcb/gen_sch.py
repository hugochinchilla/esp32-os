"""Write hardware/hat/hat.kicad_sch (KiCad 7 format) from parts.py.

Every pin carries a net label, so the drawing reads as a list of blocks
rather than a wired diagram. Run from the repository root:
    python3 tools/hat_pcb/gen_sch.py
"""
import os
import re
import sys
import uuid

sys.path.insert(0, os.path.dirname(__file__))
import parts  # noqa: E402

SYMDIR = "/usr/share/kicad/symbols"
OUT = os.path.join("hardware", "hat")
NS = uuid.UUID("6d1c3f7e-2a8b-4c55-9e1a-0c0ffee0a7a1")
ROOT_UUID = str(uuid.uuid5(NS, "root"))


def uid(*a):
    return str(uuid.uuid5(NS, "/".join(a)))


def lib_block(lib, name):
    s = open(f"{SYMDIR}/{lib}.kicad_sym").read()
    i = s.find(f'(symbol "{name}"')
    if i < 0:
        raise KeyError(f"{lib}:{name}")
    d = 0
    for k in range(i, len(s)):
        if s[k] == "(":
            d += 1
        elif s[k] == ")":
            d -= 1
            if d == 0:
                return s[i:k + 1]


PIN_RE = re.compile(r'\(pin (\w+) \w+\s*\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)\s*\(length [\d.]+\)(?: hide)?\s*'
                    r'\(name "([^"]*)".*?\(number "([^"]*)"', re.S)


def custom_symbol(name, pins):
    left = [p for p in pins if p[2] == "L"]
    right = [p for p in pins if p[2] == "R"]
    rows = max(len(left), len(right))
    w = {"VGA_HD15": 22.86, "microSD_Det": 20.32, "USB_A_Stacked": 20.32,
         "USBLC6-2SC6": 12.7}.get(name, 15.24)
    h = rows * 2.54 + 2.54
    top = h / 2
    body = [f'(rectangle (start {-w/2:.2f} {top:.2f}) (end {w/2:.2f} {-top:.2f}) '
            f'(stroke (width 0.254) (type default)) (fill (type background)))']
    pin_txt = []
    geo = []
    for side, lst in (("L", left), ("R", right)):
        for i, (num, pname, _, etype) in enumerate(lst):
            y = top - 2.54 - i * 2.54
            x = -w / 2 - 2.54 if side == "L" else w / 2 + 2.54
            ang = 0 if side == "L" else 180
            pin_txt.append(f'(pin {etype} line (at {x:.2f} {y:.2f} {ang}) (length 2.54) '
                           f'(name "{pname}" (effects (font (size 1.27 1.27)))) '
                           f'(number "{num}" (effects (font (size 1.27 1.27)))))')
            geo.append((num, x, y, ang))
    txt = (f'(symbol "hat:{name}" (in_bom yes) (on_board yes)\n'
           f'  (property "Reference" "J" (at 0 {top + 1.27:.2f} 0) (effects (font (size 1.27 1.27))))\n'
           f'  (property "Value" "{name}" (at 0 {-top - 1.27:.2f} 0) (effects (font (size 1.27 1.27))))\n'
           f'  (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n'
           f'  (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n'
           f'  (symbol "{name}_0_1" {" ".join(body)})\n'
           f'  (symbol "{name}_1_1" {" ".join(pin_txt)})\n)')
    return txt, geo, (w / 2 + 2.54, top)


def load_symbols():
    syms = {}
    for p in parts.PARTS:
        lid = p["sym"]
        if lid in syms:
            continue
        lib, name = lid.split(":")
        if lib == "hat":
            txt, geo, ext = custom_symbol(name, parts.CUSTOM_SYMBOLS[name])
        else:
            blk = lib_block(lib, name)
            assert "extends" not in blk[:400], lid
            txt = blk.replace(f'(symbol "{name}"', f'(symbol "{lid}"', 1)
            geo = [(m.group(6), float(m.group(2)), float(m.group(3)), int(float(m.group(4))))
                   for m in PIN_RE.finditer(blk)]
            xs = [abs(g[1]) for g in geo] or [2.54]
            ys = [abs(g[2]) for g in geo] or [2.54]
            ext = (max(xs), max(ys))
        syms[lid] = (txt, geo, ext)
    pf = lib_block("power", "PWR_FLAG").replace('(symbol "PWR_FLAG"', '(symbol "power:PWR_FLAG"', 1)
    syms["power:PWR_FLAG"] = (pf, [("1", 0, 0, 90)], (2.54, 2.54))
    return syms


def prop(name, value, x, y, hide=False, size=1.27):
    h = " hide" if hide else ""
    value = value.replace('"', "'")
    return (f'    (property "{name}" "{value}" (at {x:.2f} {y:.2f} 0) '
            f'(effects (font (size {size} {size})){h}))\n')


def label(net, x, y, pin_ang):
    ang = (pin_ang + 180) % 360
    just = {0: "left bottom", 90: "left bottom", 180: "right bottom", 270: "right bottom"}[ang]
    return (f'  (label "{net}" (at {x:.2f} {y:.2f} {ang}) (fields_autoplaced) '
            f'(effects (font (size 1.27 1.27)) (justify {just})) (uuid {uid("lbl", net, str(x), str(y))}))\n')


GROUP_TITLES = {
    "devkit": "ESP32-S3-DevKitC-1 sockets and expansion header",
    "vga": "VGA: 3-bit resistor ladders (510 / 1k / 2k) into 75 ohm, sync through 100 ohm. All ground pins and the shell to ground",
    "audio": "Audio out (RC filter, DC block) and piezo beeper",
    "rtc": "Clock chip DS3231MZ on I2C, CR2032 backup",
    "sd": "microSD, 1-bit SDMMC (CLK 39, CMD 40, D0 41), card detect on GPIO12",
    "usb": "USB host: CH334R hub, switched and current-limited port power, ESD, two USB-A",
}
# (x, y, width) of each block's area on the A3 sheet, in mm
GROUP_AREAS = {
    "devkit": (15, 20, 190), "vga": (215, 20, 190),
    "rtc": (15, 120, 120), "audio": (140, 120, 120), "sd": (265, 120, 140),
    "usb": (15, 200, 390),
}


def main():
    syms = load_symbols()
    out = []
    out.append(f'(kicad_sch (version 20230121) (generator eeschema)\n  (uuid {ROOT_UUID})\n  (paper "A3")\n')
    out.append('  (title_block (title "Chinchilla hat for ESP32-S3-DevKitC-1") (rev "0.1") '
               '(comment 1 "Generated by tools/hat_pcb/gen_sch.py from tools/hat_pcb/parts.py") '
               '(comment 2 "Prototype, not yet built"))\n')
    out.append("  (lib_symbols\n")
    for lid, (txt, _, _) in syms.items():
        out.append("    " + txt + "\n")
    out.append("  )\n")

    placed = []
    by_group = {}
    for p in parts.PARTS:
        by_group.setdefault(p["group"], []).append(p)
    for g, plist in by_group.items():
        gx, gy, gw = GROUP_AREAS[g]
        out.append(f'  (text "{GROUP_TITLES[g]}" (at {gx:.2f} {gy:.2f} 0) '
                   f'(effects (font (size 2 2) bold) (justify left bottom)) (uuid {uid("title", g)}))\n')
        x, y, rowh = gx, gy + 8, 0
        for p in plist:
            _, geo, (ex, ey) = syms[p["sym"]]
            labw = 14 if any(a in (0, 180) for _, _, _, a in geo) else 2
            labh = 12 if any(a in (90, 270) for _, _, _, a in geo) else 2
            cw = 2 * (ex + labw) + 6
            ch = 2 * (ey + labh) + 4
            if x + cw > gx + gw and x > gx:
                x, y, rowh = gx, y + rowh, 0
            cx, cy = x + cw / 2, y + ch / 2
            cx, cy = round(cx / 1.27) * 1.27, round(cy / 1.27) * 1.27
            placed.append((p, cx, cy, geo, ey))
            x += cw
            rowh = max(rowh, ch)

    flagged = set()
    for p, cx, cy, geo, ey in placed:
        su = uid("sym", p["ref"])
        out.append(f'  (symbol (lib_id "{p["sym"]}") (at {cx:.2f} {cy:.2f} 0) (unit 1)\n'
                   f'    (in_bom {"no" if p["dnp"] else "yes"}) (on_board yes) (dnp {"yes" if p["dnp"] else "no"})\n'
                   f'    (uuid {su})\n')
        out.append(prop("Reference", p["ref"], cx, cy - ey - 2.0))
        out.append(prop("Value", p["value"], cx, cy + ey + 2.0))
        out.append(prop("Footprint", p["fp"], cx, cy, hide=True))
        out.append(prop("Datasheet", "", cx, cy, hide=True))
        for k in ("mfr", "mpn", "lcsc", "desc"):
            name = {"mfr": "Manufacturer", "mpn": "MPN", "lcsc": "LCSC", "desc": "Description"}[k]
            out.append(prop(name, p[k], cx, cy, hide=True))
        for num, _, _, _ in geo:
            out.append(f'    (pin "{num}" (uuid {uid("pin", p["ref"], num)}))\n')
        out.append(f'    (instances (project "hat" (path "/{ROOT_UUID}" (reference "{p["ref"]}") (unit 1))))\n  )\n')
        for num, px, py, ang in geo:
            sx, sy = cx + px, cy - py
            net = p["pins"].get(num)
            if net is None:
                out.append(f'  (no_connect (at {sx:.2f} {sy:.2f}) (uuid {uid("nc", p["ref"], num)}))\n')
            else:
                out.append(label(net, sx, sy, ang))
                if net in parts.POWER_NETS and net not in flagged:
                    flagged.add(net)

    # Power flags, in a row at the bottom
    for i, net in enumerate(sorted(flagged)):
        fx, fy = 30 + i * 25.4, 280.67
        out.append(f'  (symbol (lib_id "power:PWR_FLAG") (at {fx:.2f} {fy:.2f} 0) (unit 1)\n'
                   f'    (in_bom yes) (on_board yes) (dnp no) (uuid {uid("flag", net)})\n')
        out.append(prop("Reference", f"#FLG0{i+1}", fx, fy - 3.81, hide=True))
        out.append(prop("Value", "PWR_FLAG", fx, fy - 5.08))
        out.append(prop("Footprint", "", fx, fy, hide=True))
        out.append(prop("Datasheet", "", fx, fy, hide=True))
        out.append(f'    (pin "1" (uuid {uid("flagpin", net)}))\n'
                   f'    (instances (project "hat" (path "/{ROOT_UUID}" (reference "#FLG0{i+1}") (unit 1))))\n  )\n')
        out.append(label(net, fx, fy, 90))
    out.append(f'  (text "Power flags" (at 25 272 0) (effects (font (size 1.5 1.5)) (justify left bottom)) (uuid {uid("pft")}))\n')

    out.append(f'  (sheet_instances (path "/" (page "1")))\n)\n')
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "hat.kicad_sch"), "w") as f:
        f.write("".join(out))

    # Project symbol library with the custom symbols, so the schematic can be edited
    with open(os.path.join(OUT, "hat.kicad_sym"), "w") as f:
        f.write("(kicad_symbol_lib (version 20220914) (generator hat_gen)\n")
        for lid, (txt, _, _) in syms.items():
            if lid.startswith("hat:"):
                f.write("  " + txt.replace('(symbol "hat:', '(symbol "') + "\n")
        f.write(")\n")
    with open(os.path.join(OUT, "sym-lib-table"), "w") as f:
        f.write('(sym_lib_table\n  (lib (name "hat")(type "KiCad")(uri "${KIPRJMOD}/hat.kicad_sym")(options "")(descr "Chinchilla hat symbols"))\n)\n')
    print("wrote", os.path.join(OUT, "hat.kicad_sch"), len(placed), "symbols")


if __name__ == "__main__":
    main()
