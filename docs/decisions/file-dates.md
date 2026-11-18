---
title: "Chosen for file dates"
date: 2026-11-17
by: agent
status: to-confirm
area: storage
summary: "One date per file, when it was last written, in UTC seconds (backlog 24)"
---

# Chosen for file dates

Heading in the single-file log: "Chosen for file dates (2026-11-17, to confirm)".


Backlog item 24. Journal: `docs/journal/2026-11-17-file-dates.md`. Chosen by
the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **One date per file: when it was last written**, kept as seconds since
  1970 in UTC (`ports::DirEntry::modified`) and turned into local time when
  shown. On the board it is the flash file system's own date.
- **A file written while the machine does not know the time has no date**
  (shown blank). The board learns the time from the network; a date before
  2000 is taken as not known. Rejected: showing 1970; stamping later. Open
  for the owner: starting from the last time kept in flash, or a clock chip.
- **Shown as `2026-11-17 14:05`**: a first column in `dir` and `ls -l`, and
  in the File manager's status bar for the chosen file. `date` prints the
  day and time. Left for later: a Details view in the File manager.
