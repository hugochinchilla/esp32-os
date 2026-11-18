---
title: "The Terminal is an emulator; the session is a process of its own"
date: 2026-11-17
by: owner
status: decided
area: console
summary: "Two processes: `Terminal` is only an emulator, and the shell session is a process of its own (backlog 44)"
---

# The Terminal is an emulator; the session is a process of its own

Heading in the single-file log: "The Terminal is an emulator; the session is a process of its own (2026-11-17, the owner's request)".


Backlog item 44. Journal: `docs/journal/2026-11-17-terminal-split.md`. This
replaces, in "Chosen for the Terminal window", the session being a job of
the Terminal's process.

- **Two processes** (the owner): `Terminal` is a terminal emulator, a
  window with a grid, the keys and a scroll bar, and nothing else. The TTY
  and the command shell are a session, `sh::TerminalSession` in `core/sh`,
  running as a process `sh` that is the child of the one that asked for it.
  The shell's task, its stack and what its commands use are `sh`'s.
- **Closing the window ends the session, as xterm does** (the owner's
  choice on a card; a session that lives on to be reattached was the other
  option). The TTY is hung up; `sh` goes when its task has returned. The
  window does not wait: it is destroyed when it closes, like every other.
- The other way round, `exit`, Ctrl+D, or `kill` and End Task on `sh` close
  the window (the agent's, to confirm).
- A program asks for a session with `os::open_session(device, why)` and is
  its device: a `ports::ByteSink` for what is shown, `typed()` for the keys.
  The system makes the process and takes it away (`sessions.cpp`). With
  this `terminal.cpp` uses only the OS calls: all 17 programs have moved.
- Not chosen: one `tty` server process holding every session (one row
  would carry three shells' figures, and `kill` could not end one of
  them); splitting the classes but keeping one process (nothing would be
  independent of the window).
- Left as they are for now: the serial console and telnet make their own
  TTY and shell; the shell's commands reach the system through `sh::Env`.
