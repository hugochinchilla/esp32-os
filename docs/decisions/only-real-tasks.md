---
title: "Only real tasks are listed as processes"
date: 2026-11-16
by: owner
status: decided
area: system
summary: "A row in the process table stands for something that really runs; polled services get none"
---

# Only real tasks are listed as processes

The owner's rule, 2026-11-16: "don't advertise non real tasks, it's
confusing". The story is in `docs/journal/2026-11-16-only-real-tasks.md`.

- A row in the process table stands for something that runs: a window
  program, or a task of its own. A service that is polled from the UI loop
  gets no row.
- The rows `dbgd`, `netd`, `vncd` and `httpd` are removed. The Wi-Fi
  manager's timer is the desktop's. Before any program starts the table is
  `desktop` 1.
- A service gets its row, with the name planned for it, in the change that
  makes it a task (items 14, 18 and 26). The first: the network services
  became one task with one row, `inetd`, in item 8b (2026-11-17).
- There is no `init` row either, until init is a real program (item 18).
  **The owner's decision, 2026-11-16**: "make desktop pid 1 for now". The
  first process made is the root of the tree, PID 1, and today that is the
  desktop. Orphans go to the root and respawned processes are its children.
  When init exists it takes PID 1 and the desktop becomes its child. This
  replaces "PID 1 is a row called `init` from the first moment" in "Chosen
  for the process table", and "init is PID 1" under "OS architecture" holds
  again from item 18.
- The other option was to keep the rows marked "(part of the desktop)".
