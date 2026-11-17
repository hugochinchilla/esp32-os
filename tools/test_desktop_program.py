#!/usr/bin/env python3
"""Starts the desktop program (host/desktop) with no screen and checks what
its window would show.

    test_desktop_program.py build/host/chinchilla

SDL's "dummy" video driver gives a window that exists only in memory, so
this runs anywhere the tests run. It covers start-up on a new, empty C:, the
sample files landing in the PC folder, the picture reaching the window in
the board's colours, and the window being twice the screen's size.
"""
import os
import struct
import subprocess
import sys
import tempfile

LEVEL3 = [round(n * 255 / 7) for n in range(8)]
BLUE = [LEVEL3[n] for n in (0, 2, 5, 7)]
PALETTE = {(LEVEL3[v >> 5], LEVEL3[(v >> 2) & 7], BLUE[v & 3]) for v in range(256)}
GREY = (LEVEL3[5], LEVEL3[5], BLUE[2])  # the face of the taskbar


def read_bmp(path):
    """A 24-bit BMP as (width, height, rows of (r, g, b)), top row first."""
    data = open(path, "rb").read()
    assert data[:2] == b"BM", "not a BMP file"
    offset, = struct.unpack_from("<I", data, 10)
    width, height, _, bits = struct.unpack_from("<iiHH", data, 18)
    assert bits == 24, "expected 24 bits per pixel, got %d" % bits
    stride = (width * 3 + 3) & ~3
    rows = []
    for y in range(abs(height)):
        line = data[offset + y * stride: offset + y * stride + width * 3]
        rows.append([(line[x * 3 + 2], line[x * 3 + 1], line[x * 3]) for x in range(width)])
    if height > 0:
        rows.reverse()
    return width, abs(height), rows


def run(program, folder, scale):
    drive = os.path.join(folder, "drive %d" % scale, "C")  # made by the program
    shot = os.path.join(folder, "shot%d.bmp" % scale)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", ASAN_OPTIONS="detect_leaks=0")
    subprocess.run([program, "--drive", drive, "--fast", "--scale", str(scale), "--screenshot", shot,
                    "--exit-after", "700"], env=env, check=True, timeout=60)
    return drive, read_bmp(shot)


def main():
    program = sys.argv[1]
    with tempfile.TemporaryDirectory() as folder:
        drive, (width, height, rows) = run(program, folder, 1)
        assert (width, height) == (640, 480), (width, height)
        # A new C: got the sample files, as real files of the PC.
        assert os.listdir(drive), "the new C: is empty"
        # Every pixel is one of the board's 256 colours, untouched at 1x.
        colours = {p for row in rows for p in row}
        assert colours <= PALETTE, "colours that the board cannot show: %s" % sorted(colours - PALETTE)[:5]
        assert len(colours) > 4, "a blank screen"
        # The desktop is up: the taskbar's grey along the bottom.
        bar = rows[480 - 14]
        assert sum(p == GREY for p in bar) > 320, "no taskbar"
        # And not at the top (the desktop's own colour is there).
        assert sum(p == GREY for p in rows[2]) < 64, "the desktop is not painted"

        # No window is open on a first start. The start-up file is the
        # board's and starts inetd; here that is an inetd that serves
        # nothing, so no "Start-up" box says a line could not be done.
        for y in (200, 220, 240):
            assert sum(p == GREY for p in rows[y]) < 16, "a window is open after start-up (row %d)" % y

        # Twice the size: each screen pixel is a sharp 2x2 square.
        _, (width2, height2, rows2) = run(program, folder, 2)
        assert (width2, height2) == (1280, 960), (width2, height2)
        different = 0
        for y in range(0, 480 - 28):  # above the taskbar: its clock may have moved on
            for x in range(640):
                p = rows[y][x]
                if not (rows2[2 * y][2 * x] == p and rows2[2 * y + 1][2 * x + 1] == p):
                    different += 1
        assert different == 0, "%d pixels differ at twice the size" % different
    print("desktop program: ok")


if __name__ == "__main__":
    main()
