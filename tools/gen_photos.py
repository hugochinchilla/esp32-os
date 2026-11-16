#!/usr/bin/env python3
"""Turns the public-domain NASA photographs in third_party/samples/ into the
sample pictures the firmware carries, encoded by our own writers (baseline
JPEG, PNG) so the board's decoders read exactly what the tests read.

  python3 tools/gen_photos.py OUT_FOLDER

Needs ffmpeg to read and scale the originals. See third_party/samples/README.md
for where each original comes from and its status.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_pictures  # noqa: E402

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "third_party", "samples")

# name, original, crop (w:h:x:y of the 1280x1280 original), size, kind
PHOTOS = [
    ("earthrise.jpg", "as08-14-2383.jpg", "1280:960:0:160", (640, 480), "jpeg"),
    ("blue marble.png", "as17-148-22727.jpg", "1120:1120:80:80", (320, 320), "png"),
]


def rgb_rows(path, crop, size):
    w, h = size
    raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", path, "-vf",
                          "crop=%s,scale=%d:%d:flags=area" % (crop, w, h), "-f", "rawvideo",
                          "-pix_fmt", "rgb24", "-"], capture_output=True, check=True).stdout
    assert len(raw) == w * h * 3, (path, len(raw))
    return [[tuple(raw[(y * w + x) * 3:(y * w + x) * 3 + 3]) for x in range(w)] for y in range(h)]


def main(out):
    os.makedirs(out, exist_ok=True)
    for name, original, crop, size, kind in PHOTOS:
        rows = rgb_rows(os.path.join(HERE, original), crop, size)
        path = os.path.join(out, name)
        if kind == "jpeg":
            gen_pictures.write_jpeg(path, rows, quality=85)
        else:
            gen_pictures.write_png(path, rows, "rgb8")
        print("%s: %d bytes" % (name, os.path.getsize(path)))


if __name__ == "__main__":
    main(sys.argv[1])
