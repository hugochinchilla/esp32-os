---
title: "Chosen for the Terminal window"
date: 2026-11-17
by: agent
status: to-confirm
area: console
summary: "The Terminal program: a window that is the device of a TTY (backlog 15). Its session part was replaced by \"The Terminal is an emulator\""
---

# Chosen for the Terminal window

Heading in the single-file log: "Chosen for the Terminal window (2026-11-17, to confirm)".


Backlog item 15. Journal: `docs/journal/2026-11-17-terminal-window.md`.
Chosen by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- "Terminal" in Start > Programs (`core/src/shell/terminal.cpp`): a window
  that is the device of a TTY, with a `sh::Session` behind it. Up to three at
  once, each a process.
- Changed the same day: the session is now a process of its own, `sh`, and
  the window only an emulator. See "The Terminal is an emulator; the
  session is a process of its own" below; what follows about the task, the
  lock and the kits holds for the session.
- **Its session is a job of the Terminal's process, on a task of its own**,
  wherever the machine has tasks (2026-11-17,
  `docs/journal/2026-11-17-terminal-task.md`): the desktop goes on while a
  command works and Ctrl+C stops it. Closing the window, or End Task, hangs
  up the TTY; the process goes when the session's task has returned. The
  TTY's lock guards the grid as well, and the session's task tells the UI
  task to paint with one message until it has looked. The lock and the two
  events of a session come from a pool the machine hands in
  (`shell::set_session_kits`, three on the board). Its task asks for a 12 KB
  stack, to be measured.
- Without tasks (the browser, the PC desktop program) the session runs on
  the UI task, a line at a time (`Session::step()`), as it was first built:
  the desktop waits while a command works.
- The grid is `tty::Screen`: 80 by 25, fixed, 6x12 font. It knows CR, LF,
  backspace, tab, bell, and the ANSI sequences to erase (`J`, `K`) and to
  place and move the cursor (`H`, `A` to `D`); others are ignored. A line
  wraps at the right edge and a backspace goes back over the wrap. The other
  options were a glass teletype and a full VT100.
- 175 lines above the screen are kept (16 KB with the screen, in the
  window's own memory). Page Up and Page Down look back. No scroll bar.
- Light grey on black, a blinking block cursor, a line under the character
  when another window has the keyboard. The icon was drawn by the agent.
  Both are for the owner to overrule.
- Escape is not sent to the session.
