---
title: "Chosen for the system log and dmesg"
date: 2026-11-17
by: agent
status: to-confirm
area: console
summary: "The log is a ring of 16 KB in memory, read with `dmesg` (backlog 16)"
---

# Chosen for the system log and dmesg

Heading in the single-file log: "Chosen for the system log and dmesg (2026-11-17, to confirm)".


The last piece of backlog item 16. Journal:
`docs/journal/2026-11-17-dmesg.md`. Chosen by the agent under the owner's
"dejo a tu criterio" of 2026-11-16.

- **The log is a ring of 16 KB in memory** (external RAM on the board),
  lost at a restart. Rejected: a file on C: (flash wear); a list of lines.
- **It takes what goes to the debug link as log text**, at that one point.
- **A task that finds the log busy drops its text** rather than wait.
- `dmesg` shows it oldest first, without colour codes. No `-c`, no follow.
