#!/usr/bin/env python3
"""Writes the program files that come with the system as a C source file
for the firmware (backlog item 27; the owner's choice, 2026-11-17): the
firmware carries them as data and the system puts them in C:\\Programs at
start-up (shell::install_programs).

    tools/gen_bundle.py --out bundle.c build/programs/hello.app ...

The stamp names the set: the first 16 hex digits of the SHA-256 of every
file's name and bytes. When it differs from the one in the folder, the
files are written again.
"""
import argparse
import hashlib
import os


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()
    files = sorted(args.files, key=os.path.basename)
    digest = hashlib.sha256()
    out = ["/* Written by tools/gen_bundle.py; do not edit. */", "#include <stdint.h>", ""]
    rows = []
    for i, path in enumerate(files):
        name = os.path.basename(path)
        data = open(path, "rb").read()
        digest.update(name.encode() + b"\0" + len(data).to_bytes(4, "little") + data)
        out.append(f"static const uint8_t program_{i}[] __attribute__((aligned(4))) = {{")
        for at in range(0, len(data), 20):
            out.append("    " + "".join(f"{b}," for b in data[at:at + 20]))
        out.append("};")
        rows.append(f'    {{"{name}", program_{i}, {len(data)}}},')
    out += [
        "",
        "struct chinchilla_bundled {",
        "    const char *file;",
        "    const uint8_t *data;",
        "    uint32_t size;",
        "};",
        "const struct chinchilla_bundled chinchilla_bundle[] = {",
        *rows,
        "    {0, 0, 0},",
        "};",
        f"const unsigned chinchilla_bundle_count = {len(files)};",
        f'const char chinchilla_bundle_stamp[] = "{digest.hexdigest()[:16]}";',
        "",
    ]
    with open(args.out, "w") as f:
        f.write("\n".join(out))
    print(f"bundle: {len(files)} program file(s), stamp {digest.hexdigest()[:16]}")


if __name__ == "__main__":
    main()
