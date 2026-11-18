---
title: "Chosen for Run and the shell's open, run and edit"
date: 2026-11-17
by: agent
status: to-confirm
area: console
summary: "Start > Run... is a program of its own; the shell's `open`, `run` and `edit` (backlog 16)"
---

# Chosen for Run and the shell's open, run and edit

Heading in the single-file log: "Chosen for Run and the shell's open, run and edit (2026-11-17, to confirm)".


Backlog item 16 in part. Journal: `docs/journal/2026-11-17-run.md`. Chosen
by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **Start > Run...** is a program of its own (`core/src/shell/run.cpp`). It
  takes a program's name (any case, or the start of it when only one begins
  so) or a path (a folder opens in the File manager, a file with its
  program; without a drive it is looked for on C:). It does not take shell
  commands. When nothing can be opened the dialog stays, under a message
  that says why. The last thing run is kept in `C:\.config\run.ini` and is
  there, selected, the next time. No list of earlier entries.
- **The shell reaches the desktop through a port**, `sh::Desktop`, so it
  still builds and runs without one. `open PATH...` (DOS name `start`),
  `run [PROGRAM]` (alone: the list) and `edit [FILE...]`. A failure is a
  line on the terminal, not a message box.
- `edit` opens the desktop's Editor, and a name that is not there yet is
  made as an empty file. A console editor is item 22.
- A program started from Run or from a Terminal is the desktop's child.
- `top` waits for item 3 and `dmesg` for a log kept in memory.
