"""Fabrication outputs for the hat: Gerbers, drill files, BOM, pick-and-place.

Run from the repository root after gen_pcb.py:
    python3 tools/hat_pcb/gen_outputs.py
Writes hardware/hat/fab/.
"""
import csv
import os
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, os.path.dirname(__file__))
import parts  # noqa: E402

HAT = os.path.join("hardware", "hat")
FAB = os.path.join(HAT, "fab")
PCB = os.path.join(HAT, "hat.kicad_pcb")
SCH = os.path.join(HAT, "hat.kicad_sch")


def run(*args):
    subprocess.run(["kicad-cli", *args], check=True, stdout=subprocess.DEVNULL)


def natural(ref):
    i = len(ref.rstrip("0123456789"))
    return (ref[:i], int(ref[i:] or 0))


def bom():
    groups = {}
    for p in parts.PARTS:
        key = (p["value"], p["fp"], p["mfr"], p["mpn"], p["dnp"], p["who"])
        groups.setdefault(key, []).append(p)
    rows = []
    for (value, fp, mfr, mpn, dnp, who), ps in groups.items():
        refs = sorted((p["ref"] for p in ps), key=natural)
        th = any(k in fp for k in ("PinSocket", "PinHeader", "Dsub", "Jack", "USB_A", "HC49", "THT"))
        rows.append({
            "Designator": ",".join(refs), "Quantity": len(refs), "Value": value,
            "Footprint": fp.split(":")[1], "Manufacturer": mfr, "MPN": mpn,
            "LCSC": ps[0]["lcsc"], "Description": ps[0]["desc"],
            "Mount": "THT" if th else "SMD",
            "Fitted by": "not fitted" if dnp else ("assembler" if who == "assembler" else "you"),
        })
    order = {"assembler": 0, "you": 1, "not fitted": 2}
    rows.sort(key=lambda r: (order[r["Fitted by"]], natural(r["Designator"].split(",")[0])))
    fields = list(rows[0])
    with open(os.path.join(FAB, "hat-bom.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)
    asm = [r for r in rows if r["Fitted by"] == "assembler"]
    with open(os.path.join(FAB, "hat-bom-assembly.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields[:-2])
        w.writeheader()
        w.writerows({k: r[k] for k in fields[:-2]} for r in asm)
    hand = [r for r in rows if r["Fitted by"] == "you"]
    with open(os.path.join(FAB, "hat-hand-solder.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields[:-1])
        w.writeheader()
        w.writerows({k: r[k] for k in fields[:-1]} for r in hand)
    print(f"BOM: {len(asm)} assembler lines ({sum(r['Quantity'] for r in asm)} parts), "
          f"{len(hand)} hand-solder lines ({sum(r['Quantity'] for r in hand)} parts)")


def filter_pos(path):
    """Keep only the parts the assembler places."""
    keep = {p["ref"] for p in parts.PARTS if p["who"] == "assembler" and not p["dnp"]}
    rows = list(csv.reader(open(path)))
    out = [rows[0]] + [r for r in rows[1:] if r[0] in keep]
    missing = keep - {r[0] for r in out[1:]}
    assert not missing, missing
    with open(path, "w", newline="") as f:
        csv.writer(f).writerows(out)


def main():
    if os.path.isdir(FAB):
        shutil.rmtree(FAB)
    gdir = os.path.join(FAB, "gerbers")
    os.makedirs(gdir)
    run("pcb", "export", "gerbers", "--layers",
        "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts",
        "--subtract-soldermask", "-o", gdir + "/", PCB)
    run("pcb", "export", "drill", "--format", "excellon", "--excellon-separate-th",
        "--generate-map", "--map-format", "pdf", "-o", gdir + "/", PCB)
    with zipfile.ZipFile(os.path.join(FAB, "hat-gerbers.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        for name in sorted(os.listdir(gdir)):
            z.write(os.path.join(gdir, name), name)
    run("pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "front",
        "--use-drill-file-origin", "-o", os.path.join(FAB, "hat-pos-top.csv"), PCB)
    filter_pos(os.path.join(FAB, "hat-pos-top.csv"))
    run("pcb", "export", "pdf", "--layers", "F.Fab,Edge.Cuts", "--include-border-title",
        "-o", os.path.join(FAB, "hat-assembly-top.pdf"), PCB)
    run("sch", "export", "pdf", "-o", os.path.join(FAB, "hat-schematic.pdf"), SCH)
    bom()
    print("outputs in", FAB)


if __name__ == "__main__":
    main()
