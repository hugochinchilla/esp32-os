---
title: "Programs on tasks of their own"
date: 2026-11-17
by: owner+agent
status: decided
area: system
summary: "Each program runs on its own task and the server waits for it, with a limit; \"not responding\" (backlog 26)"
---

# Programs on tasks of their own

Backlog item 26, first step. [Journal](journal/2026-11-17-program-tasks.md).

- **The server waits for the program** (the owner's choice on a card,
  2026-11-17): each call into a program is handed to its task and the UI
  task waits for it, with a limit; a program that does not come back is
  painted grey and marked as not responding. Rejected: the program
  painting under a lock on the screen (a hung program holding it freezes
  the desktop), a picture kept per window (RAM, and windows keep no
  picture), not doing the item.
- A game that takes the whole screen may later draw straight into the
  frame buffer whatever was chosen here (the owner's question, with Doom
  in mind): the server stands aside, as DirectDraw's exclusive mode. Not
  built; not in the backlog yet.

The rest is the agent's, to confirm:

- A program may have a task of its own (`core/prog`). Every call into its
  code goes through `proc::Table::call` and is handed to that task while
  the caller waits: one of the two runs at a time, so windows, timers and
  the process table still have no locks.
- A call back into a program that is itself waiting is made where the
  caller is. Rejected: letting the two wait for each other.
- Which programs: a mark in the program table, one program at a time.
  Culebra first.
- **4 KB of stack** from internal memory, counted as the program's, and
  **the cost is accepted** (the owner's choice on the board thread's
  card, 2026-11-17: "Merge with a 4 KB stack"; merging as it was with
  8 KB, and not yet, were the others). Measured on the board: about 7 %
  more processor time while playing, and at most about 2.8 KB of stack
  used.
- The limit is two seconds. A program that has not answered by then is
  late: calls for it are dropped until it has finished, what it paints
  is grey, the Task Manager says "Not responding", and closing its
  window ends it without asking.
- No lock around the OS calls: each begins with `enter()` and ends with
  `leave()` (a count). A late program sleeps in `enter()` and is let in
  for 10 ms a round when the UI task has nothing else under way.
  Rejected: a mutex held by the UI task whenever it runs.
- What goes to a program goes by copy, never as a pointer into the
  caller's stack.
- A program's code is entered from outside only through `in_program`
  (or `launch`): one program opening another, the desktop opening a
  folder, files changing under a File manager window, the answer to a
  question. Rejected for now: posting a message and returning (it comes
  with the queue of events).
- **The programs have tasks, the dialogs and settings tools do not** (the
  owner's choice on a card, 2026-11-17: "Los programas, no los
  diálogos"; every one with dialogs included, only those that can take
  long, and leaving it at Culebra were the others).
- Stack per program in the program table (the figures are the agent's,
  to confirm with the board): Cactus
  Patch at 4 KB; Editor, File manager, Picture viewer, Task Manager and
  Defragmenter at 8 KB; About, Welcome and Widget gallery at 4 KB, now
  written on the OS calls like the rest. The Terminal's window stays on
  the UI task (its shell already has a task: the session). The settings
  tools and dialogs stay on the UI task.
- A late program's task runs at the idle priority until it answers: it
  may spin for ever, and the kernel's idle tasks must still get their
  turn (ESP-IDF's task watchdog watches them, 5 s).
- A late program's task is taken away when it is ended; it is outside
  every OS call by then, so it holds no lock.
- The queue of events was decided against: "No queue of events" below.
  Still not done: programs that call each other's code directly.
