#!/usr/bin/env python3
"""Checks a program built for the board (tools/build_programs.sh) against
what Espressif's ELF loader 2.1.0 takes for code: it rounds the size of
.text up to a whole word, and any other section that begins inside that
word would be given an address in instruction space, where data cannot be
read byte by byte. Also that there is a .text at all, and one .rodata at
most.

    tools/check_program_elf.py <readelf> <program.elf>
"""
import re
import subprocess
import sys


def main():
    readelf, path = sys.argv[1], sys.argv[2]
    out = subprocess.run([readelf, "-S", "-W", path], capture_output=True, text=True, check=True).stdout
    sections = []
    for line in out.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]{8})\s+[0-9a-f]+\s+([0-9a-f]+)\s+\S+\s+(\S*)", line)
        if m:
            sections.append((m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4), 16), m.group(5)))
    text = [s for s in sections if s[0] == ".text"]
    if len(text) != 1:
        sys.exit(f"{path}: expected one .text section, found {len(text)}")
    start, size = text[0][2], text[0][3]
    end = start + ((size + 3) & ~3)
    bad = [s[0] for s in sections if s[0] != ".text" and "A" in s[4] and start <= s[2] < end]
    if bad:
        sys.exit(f"{path}: {', '.join(bad)} begins inside the last word of .text; the loader would take it for code")
    if sum(1 for s in sections if s[0] == ".rodata") > 1:
        sys.exit(f"{path}: more than one .rodata section")


if __name__ == "__main__":
    main()
