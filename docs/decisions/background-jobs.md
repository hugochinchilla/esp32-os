---
title: "Chosen for background jobs"
date: 2026-11-17
by: agent
status: to-confirm
area: system
summary: "A job is a task of its own for as long as its work takes, `core/job/` (backlog 10)"
---

# Chosen for background jobs

Heading in the single-file log: "Chosen for background jobs (2026-11-17, to confirm)".


Backlog item 10. Journal: `docs/journal/2026-11-17-jobs.md`. Chosen by the
agent under the owner's "dejo a tu criterio" of 2026-11-16, within what "OS
architecture" above already says about saves and signals.

- **A job is a task of its own for as long as its work takes**
  (`core/job/`, `ports::Tasks`): made when the job starts, gone when the
  work returns. No worker task waits for work and no stack is held between
  jobs. Eight jobs at most in the machine. Rejected: one worker with a queue
  (holds its stack for ever; a long job blocks the saves behind it), a pool.
- The work runs on the job's task and may read the program's memory; it may
  not touch windows, timers or the process table. Its result comes back
  through the UI message queue and `done` runs on the UI task as the owner.
- **A process cannot end while it has a job**: the count is on its row
  (`proc::Process::jobs`). A KILL stays waiting on the row, the job is told
  to stop, and the KILL takes effect once the job has returned. A window
  program that has closed its last window stays in the table, and its
  closed windows are not destroyed, until then. Rejected: deleting the
  job's task on KILL (it may hold the file system's lock).
- **A locked window** (`Window::set_locked`) is enforced by the window
  manager, not by each program: no keys, text, clicks in the client area or
  menu bar, dropped files or requests to close (close button, Alt+F4, TERM,
  Shut Down, which is called off). It can still be moved, sized and
  minimised. The pointer is the hourglass over it and the arrow elsewhere.
  Nothing else shows the lock. Rejected: a "saving" flag checked by the
  program in each action; a modal "Saving..." dialog.
- **The Editor saves through a job** with its window locked. The save does
  not stop early on a KILL: the file is finished first.
- On a machine without tasks (the browser, the PC desktop program, most
  tests) there is no `ports::Tasks` and the caller does the work inside
  `start()`, under the whole-desktop hourglass, as before.
- On the board a job's task has priority 4, one below the UI task: the
  Normal class of item 11 ("Chosen for the priority classes").
