---
title: "init starts again what ends"
date: 2026-11-17
by: owner
status: decided
area: system
summary: "init, not the process table, starts again what ends, as `respawn` in an inittab (backlog 39)"
---

# init starts again what ends

Heading in the single-file log: "init starts again what ends (the owner's request, 2026-11-17)".


Backlog item 39. The owner asked for the restart rule to move from the
process table into `init`, as `respawn` in a Unix `/etc/inittab`.
[Journal](journal/2026-11-17-init-respawn.md).

- The process table starts nothing again. It tells a supervisor
  (`proc::Supervisor`, which is init) of every ending. The brake (ended
  within 2 s, 5 times in a row: not started again) is init's.
- `startup.txt` says what comes back, in inittab's fields (the owner's
  choice, over a new verb `respawn NAME` and over a verb that also takes
  programs): `WHAT:ACTION`, the action `once`, `respawn` or `off`, after
  the last colon so a path keeps its own. `desktop:once`,
  `taskbar:respawn`, `inetd:once`, `console:off`. A WHAT that is not a
  part of the system is a program or a file. The lines of the first form
  (`start NAME`, `run WHAT`) are not read: the agent had kept them
  understood so the board's file went on working, and the owner had that
  taken out the same day (see "No backward compatibility").
- The agent's, to confirm: two fields and not inittab's four (there are
  no runlevels, and an id beside the name would say the same thing
  twice); `off` is new with the fields; the desktop cannot be `off`; a
  program cannot be `respawn` yet.
- The agent's, to confirm: a thing that comes back is watched by its
  process, which has the thing's name; the taskbar is needed and comes
  back unless the file says `taskbar:once` (or `taskbar:off`), so a file
  from before still works; the taskbar is init's child; `respawn` on the
  network services or the console starts them once and says so in the log.
- Not done: init on a task of its own that waits for endings. That is
  item 26.
