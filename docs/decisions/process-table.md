---
title: "Chosen for the process table"
date: 2026-11-16
by: agent
status: to-confirm
area: system
summary: "`core/proc`: 32 rows, PIDs, parents, states, who owns which window (backlog 2)"
---

# Chosen for the process table

Heading in the single-file log: "Chosen for the process table (2026-11-16, to confirm)".


Backlog item 2. The options and the reasons are in
`docs/journal/2026-11-16-process-table.md`. Each is easy to change. Checked on
the board and merged 2026-11-16: the PIDs there are init 1, dbgd 2, vncd 3,
httpd 4, desktop 5, netd 6, and the change costs 3.2 KB of internal RAM (1.2
for the table, the rest for Task Manager's rows).

- The table is `core/proc`: 32 rows, PIDs from 1 to 32767 that are not given
  out again until the count has gone round, a parent, a name, a state
  (running, exiting) and flags (system, protected, respawn, and "windowed":
  ends with its last window). The flags are stored; what protected and respawn
  do is backlog item 4.
- How the OS knows who is asking: the table holds the current process. The
  window manager sets it around every call into a program (keys, mouse, paint,
  timers, menu actions, close), and a window or timer made meanwhile belongs
  to it. Programs do not pass a PID. The other options were a PID in every
  call, or programs registering what they own.
- PID 1 is a row called `init` from the first moment, standing for the
  start-up code until init is a real program (item 18).
- One process per program, not per window: the File manager's folder windows
  and the Trash window are one process. The parent is whoever launched it
  (the desktop, or the File manager for a file it opens); when a parent ends,
  init adopts its children. Shared dialogs belong to whoever asked. The Screen
  Saver and Network windows are processes of their own; the Shut Down dialog
  is the desktop's.
- A window program ends when it has no window left at the end of the event
  that closed the last one, not at once (the Picture viewer's Open dialog
  closes before its picture window opens). Timers it left are stopped.
- **Reversed 2026-11-16 by the owner** (see "Only real tasks are listed"
  below): the services had rows with Unix-style names, `dbgd`, `netd`, `vncd`
  and `httpd`, that were names only, all running on the UI task. Those rows
  are gone. `desktop` (protected) stays: it is the UI task. The names are
  kept for the day each service is a task.
- Task Manager lists processes: PID, name and the main window's title. By
  default the programs with windows, without itself, as before; "Show all
  processes" shows the whole table. This replaces "every window that has a
  taskbar button" in the section above. End Task closes all the windows of
  the process; on a process without windows it says it cannot yet (signals,
  item 5). The other options were always the whole table, or two tabs. **Confirmed by
  the owner, 2026-11-16**, on a card with those three options.
- The table has no lock: one task uses it. The first item that starts a
  process from another task has to add one.
