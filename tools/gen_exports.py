#!/usr/bin/env python3
"""Writes the table of what a loaded program may ask the system for by name.

A program kept in a file (backlog item 27, programs/) is compiled apart from
the firmware. What it calls and does not have is looked up by name when it
is loaded, in the table this script writes: the OS calls (everything in
namespace os), the pictures of core/gfx/art.hpp, the shell's message box and
dialogs, and the few functions of the C and C++ libraries a program may use.
A name that is not in the table cannot be reached: the program is refused.

The firmware's build runs it on the core library it has just compiled, so
the table always matches the firmware it is part of:

    gen_exports.py --nm xtensa-esp32s3-elf-nm --out exports.c libcore.a

With --check and program files, it says instead which of the names those
programs need are missing (tools/build_programs.sh).
"""
import argparse
import re
import subprocess
import sys

# From the core library: by name.
CORE = [
    r"^_ZN2os",  # the OS calls: os::, os::ui::, os::file::...
    r"^_ZN5shell(11message_box|3ask|16open_file_dialog|16save_file_dialog|12input_dialog|13colour_dialog)E",
]
# From the core library: every function of these files.
CORE_FILES = ["art.cpp"]

# From the C and C++ libraries and the compiler's own helpers.
RUNTIME = """
memcpy memmove memset memcmp memchr
strlen strcmp strncmp strcpy strncpy strcat strncat strchr strrchr strstr strcasecmp strncasecmp
snprintf vsnprintf sscanf strtol strtoul atoi abs labs qsort
_Znwj _Znaj _ZdlPv _ZdlPvj _ZdaPv _ZdaPvj
__cxa_pure_virtual __cxa_guard_acquire __cxa_guard_release
__divdi3 __udivdi3 __moddi3 __umoddi3 __muldi3 __ashldi3 __lshrdi3 __ashrdi3
__adddf3 __subdf3 __muldf3 __divdf3 __floatsidf __floatunsidf __fixdfsi __fixunsdfsi
__extendsfdf2 __truncdfsf2 __eqdf2 __nedf2 __ltdf2 __ledf2 __gtdf2 __gedf2
__floatdidf __floatdisf __fixdfdi __fixsfdi
""".split()


def core_names(nm, archive):
    out = subprocess.run([nm, "-g", "--defined-only", archive], check=True, capture_output=True, text=True).stdout
    names = set()
    member = ""
    for line in out.splitlines():
        if line.endswith(":"):
            member = line[:-1]
            continue
        parts = line.split()
        if len(parts) != 3 or parts[1] not in "TW":
            continue
        name = parts[2]
        if any(re.search(p, name) for p in CORE) or any(member.startswith(f) for f in CORE_FILES):
            names.add(name)
    return names


def needed(nm, program):
    out = subprocess.run([nm, "-u", program], check=True, capture_output=True, text=True).stdout
    return [line.split()[-1] for line in out.splitlines() if line.split()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", default="nm")
    ap.add_argument("--out")
    ap.add_argument("--check", nargs="*", help="program files: say what they need that is not exported")
    ap.add_argument("archive")
    args = ap.parse_args()

    names = sorted(core_names(args.nm, args.archive)) + RUNTIME
    if args.check is not None:
        have = set(names)
        bad = 0
        for program in args.check:
            missing = [n for n in needed(args.nm, program) if n not in have]
            for n in missing:
                print(f"{program}: needs {n}, which the system does not export")
            bad += len(missing)
        print(f"exports: {len(names)} names; {len(args.check)} program(s) checked, {bad} name(s) missing")
        return 1 if bad else 0

    lines = [
        "/* Written by tools/gen_exports.py from the core library: what a loaded",
        "   program may ask the system for by name. Do not edit. */",
        "struct chinchilla_export { const char *name; const void *address; };",
    ]
    for i, n in enumerate(names):
        lines.append(f'extern const char e{i}[] __asm__("{n}");')
    lines.append("const struct chinchilla_export chinchilla_exports[] = {")
    for i, n in enumerate(names):
        lines.append(f'    {{"{n}", e{i}}},')
    lines.append("    {0, 0},")
    lines.append("};")
    lines.append(f"const unsigned chinchilla_export_count = {len(names)};")
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
