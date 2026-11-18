#!/usr/bin/env python3
"""Rebuild the list of decisions in docs/DECISIONS.md.

Each decision is a file in docs/decisions/ that starts with a block of
`key: value` lines between two `---` lines. This tool reads those blocks and
writes one line per decision, grouped by area, between the two markers in
docs/DECISIONS.md.

    tools/decisions_index.py            rewrite the list
    tools/decisions_index.py --check    fail if the list is out of date or a
                                        file's block is wrong (the PC tests
                                        run this)

A repository root may be given as the last argument.
"""
import json
import pathlib
import re
import sys

# Area, and what it covers, in the order the index shows them.
AREAS = [
    ("project", "Goal, ways of working, what was rejected, what is open"),
    ("hardware", "The board, wiring, pins, what the bench showed"),
    ("video", "The picture: signal, memory, painting, frame time"),
    ("boot", "Flash layout, the BIOS stage, start-up screens"),
    ("system", "Processes, tasks, signals, init, OS calls, memory"),
    ("storage", "File system, folders, the Trash, settings files, saving"),
    ("desktop", "Window manager, menus, taskbar, themes, icons, pointer"),
    ("programs", "The programs and games, one decision each"),
    ("console", "TTY, command shell, Terminal, serial console"),
    ("network", "Wi-Fi, remote screen, telnet, updates"),
    ("tools", "The debug tool, the PC and browser builds, deployment"),
]
BY = ("owner", "agent", "owner+agent")
STATUS = ("decided", "to-confirm", "open", "replaced", "idea", "reference")
KEYS = ("title", "date", "by", "status", "area", "summary")
BEGIN = "<!-- decisions:begin (written by tools/decisions_index.py; do not edit by hand) -->"
END = "<!-- decisions:end -->"


def read_block(path):
    lines = path.read_text(encoding="utf-8").split("\n")
    if lines[0] != "---":
        raise ValueError("does not start with ---")
    block = {}
    for line in lines[1:]:
        if line == "---":
            break
        key, sep, value = line.partition(": ")
        if not sep:
            raise ValueError(f"not a `key: value` line: {line!r}")
        block[key] = json.loads(value) if value.startswith('"') else value
    else:
        raise ValueError("no closing ---")
    for key in KEYS:
        if not block.get(key):
            raise ValueError(f"`{key}` is missing")
    if not re.fullmatch(r"\d{4}-\d\d-\d\d", block["date"]):
        raise ValueError("`date` is not YYYY-MM-DD")
    if block["by"] not in BY:
        raise ValueError(f"`by` is not one of {', '.join(BY)}")
    if block["status"] not in STATUS:
        raise ValueError(f"`status` is not one of {', '.join(STATUS)}")
    if block["area"] not in [a for a, _ in AREAS]:
        raise ValueError(f"`area` is not one of {', '.join(a for a, _ in AREAS)}")
    if "\n" in block["summary"] or len(block["summary"]) > 160:
        raise ValueError("`summary` is longer than 160 characters")
    if not re.fullmatch(r"[a-z0-9]+(-[a-z0-9]+)*", path.stem):
        raise ValueError("the file name is not lower-case words joined by hyphens")
    return block


def build(root):
    entries, errors = [], []
    for path in sorted((root / "docs/decisions").glob("*.md")):
        try:
            entries.append((path.name, read_block(path)))
        except (ValueError, json.JSONDecodeError) as error:
            errors.append(f"docs/decisions/{path.name}: {error}")
    out = [BEGIN, ""]
    for area, covers in AREAS:
        mine = [e for e in entries if e[1]["area"] == area]
        if not mine:
            continue
        out += [f"### {area}", "", f"{covers}.", ""]
        for name, b in sorted(mine, key=lambda e: (e[1]["date"], e[1]["title"].lower())):
            out.append(f"- [{b['title']}](decisions/{name}) "
                       f"({b['date']}, {b['by']}, {b['status']}): {b['summary']}")
        out.append("")
    out.append(END)
    return "\n".join(out), errors, len(entries)


def main(argv):
    check = "--check" in argv
    rest = [a for a in argv if a != "--check"]
    root = pathlib.Path(rest[0]) if rest else pathlib.Path(__file__).resolve().parent.parent
    index = root / "docs/DECISIONS.md"
    text = index.read_text(encoding="utf-8")
    if BEGIN not in text or END not in text:
        print("docs/DECISIONS.md: the two decisions markers are missing", file=sys.stderr)
        return 1
    listing, errors, count = build(root)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    new = text[:text.index(BEGIN)] + listing + text[text.index(END) + len(END):]
    if new == text:
        print(f"docs/DECISIONS.md lists all {count} decisions")
        return 0
    if check:
        print("docs/DECISIONS.md is out of date: run tools/decisions_index.py", file=sys.stderr)
        return 1
    index.write_text(new, encoding="utf-8")
    print(f"docs/DECISIONS.md rewritten, {count} decisions")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
