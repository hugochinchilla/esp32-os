#!/usr/bin/env python3
"""Backlog item 20: which programs keep to the OS calls (core/os/os.hpp).

A program that has moved asks the system only through os::, so it may not
name the window manager, the drives, the System port or the process table
itself. This fails if one of the moved files does, and says how far the move
has got. To move a program: change its calls, then add its file to MOVED.
The programs kept in files (programs/) are all checked the same way.

    python3 tools/check_os_calls.py [repository]
"""
import os
import re
import sys

# Programs (files of core/src/shell) that use only the OS calls.
MOVED = [
    "about.cpp",
    "colour_dialog.cpp",
    "dialogs.cpp",
    "display.cpp",
    "file_ops.cpp",
    "bluetooth.cpp",
    "network.cpp",
    "run.cpp",
    "screensaver.cpp",
    "services.cpp",
    "settings.cpp",
    "shutdown.cpp",
    "task_manager.cpp",
    "trash.cpp",
    "welcome.cpp",
]

# Not programs: the desktop itself, what it keeps running (the network, the
# way down to the power-off screen), and what the programs share.
SYSTEM = [
    "apps.cpp",
    "bluetooth_service.cpp",
    "boot.cpp",
    "desktop.cpp",
    "listing.cpp",
    "loaded.cpp",
    "network_service.cpp",
    "power.cpp",
    "programs.cpp",
    "sessions.cpp",
    "wallpaper.cpp",
]

# What a moved program may not write, and what to write instead.
FORBIDDEN = [
    (r"\bmanager\(\)", "os::show, os::start_timer, os::centred..."),
    (r"(?<!file::)\bstorage\(\)", "os::file::..."),
    (r"\bsystem_port\(\)", "os::restart"),
    (r"\bproc::table\(\)", "os::self, os::spawn, os::As"),
    (r"\bwm::Wait\b", "os::Busy"),
    (r"\bjob::jobs\(\)", "os::start_job"),
    (r"\bg_(wm|storage|system)\b", "the os:: calls"),
]


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..")
    folder = os.path.join(root, "core", "src", "shell")
    files = sorted(f for f in os.listdir(folder) if f.endswith(".cpp"))
    bad = 0
    for name in MOVED + SYSTEM:
        if name not in files:
            print(f"{name}: listed in tools/check_os_calls.py but not in core/src/shell")
            bad += 1
    for name in MOVED:
        if name not in files:
            continue
        shown = f"core/src/shell/{name}"
        with open(os.path.join(root, shown), encoding="utf-8") as f:
            lines = strip_comments(f.read()).split("\n")
        for number, line in enumerate(lines, 1):
            for pattern, instead in FORBIDDEN:
                if re.search(pattern, line):
                    print(f"{shown}:{number}: {line.strip()}")
                    print(f"    a program that has moved to the OS calls uses {instead}")
                    bad += 1
    # The programs kept in files (programs/) are compiled apart from the
    # system: all of them keep to the OS calls, with no list to be on.
    apart = os.path.join(root, "programs")
    for program in sorted(os.listdir(apart)):
        if not os.path.isdir(os.path.join(apart, program)):
            continue
        for name in sorted(os.listdir(os.path.join(apart, program))):
            if not name.endswith((".cpp", ".hpp")):
                continue
            with open(os.path.join(apart, program, name), encoding="utf-8") as f:
                lines = strip_comments(f.read()).split("\n")
            for number, line in enumerate(lines, 1):
                for pattern, instead in FORBIDDEN:
                    if re.search(pattern, line):
                        print(f"programs/{program}/{name}:{number}: {line.strip()}")
                        print(f"    a program uses {instead}")
                        bad += 1
    programs = [f for f in files if f not in SYSTEM]
    left = [f for f in programs if f not in MOVED]
    print(f"OS calls: {len(programs) - len(left)} of {len(programs)} programs moved")
    if left:
        print("still to move: " + ", ".join(left))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
