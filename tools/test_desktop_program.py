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
    # --mute: a test run on a PC with loudspeakers plays nothing.
    subprocess.run([program, "--drive", drive, "--fast", "--mute", "--scale", str(scale), "--screenshot", shot,
                    "--exit-after", "700"], env=env, check=True, timeout=60)
    return drive, read_bmp(shot)


def read_wav(path):
    """A WAV file of 16-bit samples, one channel, as (rate, samples)."""
    data = open(path, "rb").read()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE", "not a WAV file"
    channels, rate = struct.unpack_from("<HI", data, 22)
    size, = struct.unpack_from("<I", data, 40)
    assert channels == 1 and size == len(data) - 44, (channels, size, len(data))
    return rate, struct.unpack_from("<%dh" % (size // 2), data, 44)


def sounds(rate, samples):
    """The stretches that are not silence, as (start s, end s, pitch Hz, loudest)."""
    step = rate // 100
    found, start = [], None
    for i in range(0, len(samples) + step, step):
        loud = any(abs(v) > 40 for v in samples[i:i + step])
        if loud and start is None:
            start = i
        elif not loud and start is not None:
            part = samples[start:i]
            ups = [n for n in range(1, len(part)) if part[n - 1] < 0 <= part[n]]
            pitch = (len(ups) - 1) * rate / (ups[-1] - ups[0]) if len(ups) > 1 else 0
            found.append((start / rate, i / rate, pitch, max(abs(v) for v in part)))
            start = None
    return found


def check_sound(program, folder):
    """A whole start with SDL's "dummy" sound card, which takes the samples
    at the pace of a real one and plays nothing: what would be heard is
    recorded and looked at. The self-test ends in one short beep at
    1,000 Hz; the welcome chime follows when the desktop is up."""
    drive = os.path.join(folder, "drive sound", "C")
    os.makedirs(os.path.join(drive, ".config"))
    with open(os.path.join(drive, ".config", "boot.ini"), "w") as f:
        f.write("[boot]\nselftest_pace = 20\nselftest_hold_s = 0\n")  # a quick self-test
    with open(os.path.join(drive, "keep.txt"), "w") as f:
        f.write("not a new drive: no sample files to write")
    wav = os.path.join(folder, "heard.wav")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", ASAN_OPTIONS="detect_leaks=0")
    subprocess.run([program, "--drive", drive, "--scale", "1", "--record", wav, "--exit-after", "4500"],
                   env=env, check=True, timeout=60)
    rate, samples = read_wav(wav)
    assert rate == 22050, rate
    heard = sounds(rate, samples)
    assert len(heard) >= 2, "expected a beep and the chime, heard %s" % (heard,)
    beep, chime = heard[0], heard[1]
    assert 0.15 < beep[1] - beep[0] < 0.26, "the beep lasted %.2f s" % (beep[1] - beep[0])
    assert abs(beep[2] - 1000) < 15, "the beep was at %d Hz" % beep[2]
    assert 4000 < beep[3] < 16000, "the beep's loudest sample was %d" % beep[3]
    # The chime (docs/sounds/boot.pcm, 3.5 s) starts after the beep has ended
    # and is the recording itself, sample for sample.
    assert chime[0] > beep[1] and chime[1] - chime[0] > 1.0, heard
    boot = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "sounds", "boot.pcm")
    data = open(boot, "rb").read()
    recording = struct.unpack("<%dh" % (len(data) // 2), data)
    first = next(i for i, v in enumerate(recording) if abs(v) > 40)
    at = next(i for i in range(int(chime[0] * rate) - rate // 50, len(samples)) if abs(samples[i]) > 40)
    assert samples[at:at + 8000] == recording[first:first + 8000], "the chime is not the recording"

    # --mute: no sound card is opened and nothing waits for one.
    subprocess.run([program, "--drive", drive, "--fast", "--mute", "--exit-after", "300"], env=env, check=True,
                   timeout=60)


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

        check_sound(program, folder)
    print("desktop program: ok")


if __name__ == "__main__":
    main()
