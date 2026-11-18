---
title: "Chosen for the command shell"
date: 2026-11-17
by: agent
status: to-confirm
area: console
summary: "`sh::Session` in `core/sh/`: the commands, paths and drives, what each built-in does (backlog 13)"
---

# Chosen for the command shell

Heading in the single-file log: "Chosen for the command shell (2026-11-17, to confirm)".


Backlog item 13. Journal: `docs/journal/2026-11-17-command-shell.md`. Chosen
by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- The command shell is `sh::Session` in `core/sh/` (`core/shell/` is the
  desktop). It reads lines from a `tty::Tty` and knows nothing about the
  device. `run()` is the whole session for a task of its own; `greet()` and
  `step()` are the same in pieces for whoever has no task to give.
- Commands are built in, found in any case, each with its Linux name and its
  DOS names: `ls dir`, `cd chdir`, `pwd`, `cat type`, `cp copy`,
  `mv move ren rename`, `rm del erase`, `mkdir md`, `rmdir rd`, `touch`,
  `df`, `echo`, `ps tasklist`, `kill taskkill`, `free mem`, `uptime`,
  `ver uname`, `cls clear`, `help ?`, `exit logout`. `X:` changes drive.
- Options are written with `-` only (`ls -l`, `rm -r`, `kill -9`). A `/` is
  always part of a path, because paths take both slashes. DOS's `/w`, `/s`
  are not understood. The other options were DOS switches only, or guessing.
- One folder the session is in, not one per drive as DOS keeps. A path with
  a drive letter counts from that drive's root.
- `*` and `?` in the last name of a path, for `ls`, `cat`, `cp`, `mv`, `rm`,
  `rmdir`. Each command expands them itself (the DOS way); there is no heap
  to hold a list. A star does not match names that start with a dot, so
  `del *` leaves `C:\.config` and `C:\.trash` alone.
- `rm` deletes for good; it does not use the Trash (**confirmed by the
  owner, 2026-11-17**: "el comando RM no debe utilizar la papelera, borra
  el archivo directamente"). `rm -r` and `rmdir`
  refuse the folder the session is in and a drive's root.
- `mv` onto an existing file replaces it in one step. Between drives it
  copies and deletes, files only. `cp` writes the safe way (temporary file,
  then rename).
- `ps` and `kill` run on the UI task through a `sh::UiCall` (a message into
  the UI queue and an `Event` to wait on), because the process table is the
  UI task's. A session on the UI task itself calls straight in.
- Ctrl+C is the TTY's flag; the commands that loop look at it between
  pieces. It is not yet a signal of the process table, since built-in
  commands are not processes.
- `type` shows a file as Latin-1, as the Editor reads it.
- `date` came with item 24; redirection, pipes and `more` came with item 23
  ("Chosen for pipes and redirection" below); `open`,
  `run` and `edit` came with item 16, `top` with item 3; `dmesg` is still
  to come.
- `build/host/chinchilla-console [FOLDER]` is the shell in a terminal of the
  PC, for trying it without a board.
