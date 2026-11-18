---
title: "Chosen for the TTY"
date: 2026-11-17
by: agent
status: to-confirm
area: console
summary: "`tty::Tty` sits between a device and a session: line editing, echo, control keys (backlog 12)"
---

# Chosen for the TTY

Heading in the single-file log: "Chosen for the TTY (2026-11-17, to confirm)".


Backlog item 12. Journal: `docs/journal/2026-11-17-tty.md`. Chosen by the
agent under the owner's "dejo a tu criterio" of 2026-11-16.

- `tty::Tty` (`core/include/core/tty/tty.hpp`) sits between a device (it
  calls `input()` with typed bytes and is a `ports::ByteSink` for what is
  shown) and a program (`read()` a line, `write()` text).
- The TTY edits the line, as Unix's cooked mode does, so every device and
  every program gets the same editing. The other options were the shell
  (readline's way) and each device. A raw mode hands keys over as they come.
- More editing than Unix: arrows, Home, End, Delete, Ctrl+A/E/W/U/K, and
  earlier lines with up and down (384 bytes of them), all in the TTY. Tab
  does nothing until the shell can complete names.
- The line is redrawn with characters, spaces and backspaces only, no escape
  codes. Known limit: editing the middle of a line that has wrapped looks
  wrong on a real terminal. Lines stop at 159 characters.
- A program waits for a line on a new port, `ports::Event` (`set()`,
  `wait(ms)`); without one `read()` never waits. The board's adapter comes
  with item 14.
- Ctrl+C drops the typed line and the lines typed ahead, shows `^C` and
  raises a flag: `read()` answers `kInterrupted` once, a busy program looks at
  `interrupted()`. Making it the INT signal of the process table is the
  shell's job (item 13). Ctrl+D on an empty line, and the device going away,
  are "end of input".
- Text is UTF-8 at both ends (typed bytes, echo, `read()`, `write()`), like
  the rest of the OS's interfaces. Only the line being typed is Latin-1
  inside, one byte per column; a character Latin-1 lacks becomes `?`. The
  other options were Latin-1 everywhere and UTF-8 everywhere.
- After a Ctrl+C, what the program writes is dropped until it has taken
  notice. `input()` raises the interrupt before asking for the lock, so a
  command that prints without pause still sees it.
