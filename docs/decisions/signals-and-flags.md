---
title: "Chosen for signals and flags"
date: 2026-11-16
by: agent
status: to-confirm
area: system
summary: "TERM and KILL, acted on at the window manager's safe point; protected processes; End Task (backlog 4, 5)"
---

# Chosen for signals and flags

Heading in the single-file log: "Chosen for signals and flags (2026-11-16, to confirm)".


Backlog items 4 and 5, built on a branch and tested on the PC only. The
options and the reasons are in `docs/journal/2026-11-16-signals-and-flags.md`.
Each is easy to change. This replaces what the two sections above say End
Task does.

- One function sends a signal: `proc::Table::signal(pid, signal)`, with TERM
  and KILL. It does nothing to the process: the signal waits on the row, and
  the window manager acts on it at its safe point (the end of each event and
  tick). It answers Ok, NoSuchProcess, NotPermitted (protected) or
  NotSupported (a service). The other option was to act at once in the
  sender's call.
- TERM asks every window of the process, as its close button would, after
  bringing it to the front. A window held by a dialog is not asked: the
  dialog comes forward and the program stays.
- KILL closes the process's menus and windows (dialogs first), stops its
  timers and removes its row, without `close_requested()`. `closed()` still
  runs, because window objects are used again by the next launch.
- Protected processes (`desktop` today) refuse both signals. Task Manager
  greys End Task out on them.
- A process without windows cannot be signalled yet (NotSupported), and Task
  Manager has a message for it. Since the service rows were removed (see
  "Only real tasks are listed" below) no row gives that answer today. The
  first real service gets a stop function with its row (backlog item 5b).
- A signal sent from another task (the shell's `kill`, later) must go
  through the UI message queue (`msg::post`, `core/msg/queue.hpp`), so that
  `signal()` runs on the UI task and the sleeping loop wakes at once. Signals
  sent on the UI task, as End Task does, need nothing more. (Noted when the
  message queue merged, 2026-11-16.)
- End Task sends TERM. If the process is still there 5 seconds later, a box
  offers End Now (KILL) or Cancel, but only once the Task Manager is the
  window in front, so it never lands on a question the program is asking.
  End Task again on the same process offers at once. The other options were a
  box after 5 seconds whatever is on screen, or the button turning into "End
  Now". **Confirmed by the owner, 2026-11-16**, on a card with those three options.
- Respawn: `Table::set_restart(pid, start, context)`. When such a process
  ends, for any reason, its start function runs at the next safe point as
  init, and the new process is init's child with a new PID. No process on the
  real desktop has the flag yet (the taskbar gets it in item 21), and there
  is no brake on restarting too fast (with init, item 18).
