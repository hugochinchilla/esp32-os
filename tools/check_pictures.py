#!/usr/bin/env python3
"""Picture tests: renders the desktop on the PC and compares each screen, byte
for byte, with the reference pictures in host/tests/pictures/.

  python3 tools/check_pictures.py build/host/render_scenes           check
  python3 tools/check_pictures.py build/host/render_scenes --update  rewrite the references

After --update, look at every changed picture before committing it.
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dbg  # noqa: E402

REFERENCE = os.path.join(HERE, "..", "host", "tests", "pictures")
W, H = 640, 480


def read_png(path):
    """Pixel indices of a 256-colour PNG written by dbg.write_png."""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat = 8, b""
    while pos < len(data):
        size, tag = struct.unpack(">I4s", data[pos:pos + 8])
        if tag == b"IDAT":
            idat += data[pos + 8:pos + 8 + size]
        pos += 12 + size
    raw = zlib.decompress(idat)
    rows = []
    for y in range(H):
        line = raw[y * (W + 1):(y + 1) * (W + 1)]
        assert line[0] == 0, "unexpected PNG filter in %s" % path
        rows.append(line[1:])
    return b"".join(rows)


def main():
    renderer = sys.argv[1]
    update = "--update" in sys.argv
    failed = []
    with tempfile.TemporaryDirectory() as folder:
        subprocess.run([renderer, folder], check=True)
        names = sorted(n[:-4] for n in os.listdir(folder) if n.endswith(".raw"))
        assert names, "the renderer wrote nothing"
        for name in names:
            got = open(os.path.join(folder, name + ".raw"), "rb").read()
            ref = os.path.join(REFERENCE, name + ".png")
            if update:
                os.makedirs(REFERENCE, exist_ok=True)
                if not os.path.exists(ref) or read_png(ref) != got:
                    dbg.write_png(ref, W, H, got)
                    print("updated", ref)
                continue
            if not os.path.exists(ref):
                failed.append("%s: no reference picture" % name)
                continue
            want = read_png(ref)
            if want != got:
                diff = [i for i in range(W * H) if want[i] != got[i]]
                xs = [i % W for i in diff]
                ys = [i // W for i in diff]
                failed.append("%s: %d pixels differ, within x %d..%d, y %d..%d" % (
                    name, len(diff), min(xs), max(xs), min(ys), max(ys)))
                dbg.write_png(os.path.join(tempfile.gettempdir(), name + "-got.png"), W, H, got)
    if failed:
        print("\n".join(failed))
        print("(the pictures drawn now are in %s/*-got.png)" % tempfile.gettempdir())
        sys.exit(1)
    print("pictures match" if not update else "references written")


if __name__ == "__main__":
    main()
