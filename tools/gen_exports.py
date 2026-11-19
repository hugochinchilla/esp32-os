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

With --pc as well, the program files and the library are the PC's (the
tests' build): only the names of this project's own code are looked at,
since the PC's C library and checkers are not the board's. It tells on the
PC, where there is no compiler for the board, that a program asks for
something the firmware would not let it have.
"""
import argparse
import re
import subprocess
import sys

# From the core library: by name.
CORE = [
    r"^_ZN2os",  # the OS calls: os::, os::ui::, os::file::...
    r"^_ZN5shell(11message_box|3ask|16open_file_dialog|16save_file_dialog|12input_dialog|13colour_dialog)E",
    r"^_ZN5shell(13load_settings|13save_settings)E",  # a program's own settings file
    r"^_ZNK?2fs3Ini",  # the settings file's sections and keys
    r"^_ZN2fs4path",  # a path's parts: its name, its folder, its extension
    r"^_ZN2fs10error_textE",  # a file error in plain words
    r"^_ZNK?2sh15TerminalSession",  # the Terminal's session: what is typed goes in, the size, the hang-up
    r"^_ZN3msg4postE",  # a message to a process, from another task (the Terminal's session writes from its own)
    r"^_ZN3gfx(5shown|7nearest)E",  # a colour of the screen from red, green and blue, and back
    r"^_ZNK?5image",  # the picture decoders and the conversion to the board's colours
]
# Variables of the core library, by name: few, and each for a reason.
CORE_DATA = [
    r"^_ZN3gfx7g_themeE$",  # the theme in use: gfx::face() and the like read it in the program's own code
]
# From the core library: every function of these files.
CORE_FILES = ["art.cpp", "rect.cpp"]

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
setjmp longjmp __errno __bswapsi2 __divsf3
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
        if len(parts) != 3:
            continue
        name = parts[2]
        if parts[1] in "BDGRS" and any(re.search(p, name) for p in CORE_DATA):
            names.add(name)
            continue
        if parts[1] not in "TW":
            continue
        if any(re.search(p, name) for p in CORE) or any(member.startswith(f) for f in CORE_FILES):
            names.add(name)
    return names


def needed(nm, program):
    # The dynamic names: a program file is stripped of the others, and
    # without -D a stripped file seems to need nothing.
    out = subprocess.run([nm, "-D", "-u", program], check=True, capture_output=True, text=True).stdout
    return [line.split()[-1] for line in out.splitlines() if line.split()]


def ours(name):
    """A C++ name of this project's code: not the C library's, the C++
    library's (std, operator new and delete, the ABI's) or a checker's."""
    return name.startswith("_Z") and not re.match(r"_Z(St|NSt|NKSt|n[wa]|d[la]|T[VIS]N10__cxxabiv)", name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", default="nm")
    ap.add_argument("--out")
    ap.add_argument("--check", nargs="*", help="program files: say what they need that is not exported")
    ap.add_argument("--pc", action="store_true", help="with --check: the PC's build, this project's own names only")
    ap.add_argument("archive")
    args = ap.parse_args()

    names = sorted(core_names(args.nm, args.archive)) + RUNTIME
    if args.check is not None:
        have = set(names)
        bad = 0
        for program in args.check:
            missing = [n for n in needed(args.nm, program) if n not in have and (not args.pc or ours(n))]
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
