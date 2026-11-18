---
title: "Chosen for per-process figures and top"
date: 2026-11-17
by: agent
status: to-confirm
area: system
summary: "Processor time and memory per process, and `top` (backlog 3, 16)"
---

# Chosen for per-process figures and top

Heading in the single-file log: "Chosen for per-process figures and top (2026-11-17, to confirm)".


Backlog item 3, and `top` from item 16. Journal:
`docs/journal/2026-11-17-process-figures.md`. Chosen by the agent under the
owner's "dejo a tu criterio" of 2026-11-16.

- **Processor time** is the UI task's time while the process was current
  (the table has a clock and charges each change of the current process),
  plus the run time of its jobs' tasks, added whenever the figures are
  shown (item 11) and when each job ends. Time with
  no process current is the OS's and is in no row. Rejected: a task per
  program (a different architecture), and sampling (coarse).
- **Memory** is what the process's windows asked the Memory port for and
  have not given back, plus the stacks of its running jobs. A 16-byte note
  in front of each block says whose it is. Memory given out at start, and
  what the services take from the heap, is in no row.
- **Shown** as two columns, "Time" and "Mem", in the Task Manager (64 pixels
  wider) and in `ps`. Time as `4.2s`, `3:07`, `1:02:45`; memory as `640K`,
  `1.2M`, `-`.
- **`top`** sorts by share of one core since its last look, every two
  seconds, until `q` or Ctrl+C; `-n N` for N screens. Without a key to wait
  for, or into a file or a pipe, it prints one table of totals and returns.
