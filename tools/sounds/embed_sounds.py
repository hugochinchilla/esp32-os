#!/usr/bin/env python3
"""Turns the system's sounds (docs/sounds/*.pcm) into a C++ file, for the
builds that play them: the desktop program and the browser version. The
board plays nothing yet and does not get them (backlog item 58).

    embed_sounds.py docs/sounds out.cpp

The order is that of sound::Id in core/include/core/sound/sound.hpp.
"""
import os
import struct
import sys

NAMES = ["boot", "close", "alert", "error", "bell"]


def main():
    folder, out = sys.argv[1], sys.argv[2]
    lines = ["// Written by tools/sounds/embed_sounds.py from docs/sounds/. Not to be edited.",
             '#include "core/sound/sound.hpp"', "", "namespace {", ""]
    for name in NAMES:
        data = open(os.path.join(folder, name + ".pcm"), "rb").read()
        samples = struct.unpack("<%dh" % (len(data) // 2), data[: len(data) // 2 * 2])
        lines.append("const int16_t k_%s[%d] = {" % (name, len(samples)))
        for i in range(0, len(samples), 16):
            lines.append(",".join(str(s) for s in samples[i:i + 16]) + ",")
        lines.append("};")
    lines += ["", "} // namespace", "", "namespace host {", "",
              "extern const sound::Clip kSystemSounds[sound::kIds];",
              "const sound::Clip kSystemSounds[sound::kIds] = {"]
    for name in NAMES:
        lines.append("    {k_%s, sizeof(k_%s) / sizeof(k_%s[0])}," % (name, name, name))
    lines += ["};", "", "} // namespace host", ""]
    with open(out, "w") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
