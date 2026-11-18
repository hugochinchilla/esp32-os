---
title: "Chosen for init"
date: 2026-11-17
by: agent
status: to-confirm
area: system
summary: "`init` is PID 1 and the desktop its child; the start-up file (backlog 18)"
---

# Chosen for init

Heading in the single-file log: "Chosen for init (2026-11-17, to confirm)".


Backlog item 18. Journal: `docs/journal/2026-11-17-init.md`. Chosen by the
agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **`init` is PID 1 and the desktop is its child, PID 2.** This ends "make
  desktop pid 1 for now" (see "Only real tasks are listed as processes"),
  as that note said it would. init has no task of its own: it is the UI
  task's loop, the code that runs before and between programs. Rejected: a
  task that only sleeps; no row at all.
- **The start-up file is `C:\.config\startup.txt`**: `start NAME` for a
  part of the system (`desktop`, `inetd`), `run WHAT` for a program or a
  file as Start > Run... takes it, `#` for a comment, done from the top
  down. Rejected: an .ini file (order is the point); a shell script (maybe
  later); names without verbs. *Changed 2026-11-17 (the owner's choice): the
  lines are fields, `WHAT:ACTION`, as in inittab; see "init starts again
  what ends". The verbs are not read any more.*
- **Without a file the usual lines are used and the file is written.** A
  wrong line is logged and skipped. The desktop is started even if no line
  asks, until there is a console to fall back on (item 14).
- **The network ports open when `inetd` starts**, after the desktop in the
  usual file. Telnet stays a service of `inetd`, switched in `network.ini`
  (until 2026-11-18: now `telnetd` and `vncd` have lines of their own and
  open their ports when init starts them, see "Services are lines of the
  start-up file").
- **The brake**: a respawn program that ends within 2 seconds of starting,
  five times in a row, is not started again. Rejected for now: waiting
  longer each time.
