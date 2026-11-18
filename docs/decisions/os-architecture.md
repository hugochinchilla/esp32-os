---
title: "OS architecture"
date: 2026-11-16
by: owner
status: decided
area: system
summary: "The AmigaOS model: real tasks with priorities, one memory space, no protection; processes, signals, messages, shell, TTY, init"
---

# OS architecture

Decided by the owner after the architecture review; the full story, with the
options and why each was or was not taken, is in
`docs/journal/2026-11-16-architecture-review.md`. The work is listed in
`docs/BACKLOG.md`.

- **The model to aim for is AmigaOS's**: real tasks with priorities, one
  shared memory space, no protection between programs. The lack of memory
  isolation is accepted on purpose.
- **A process** is a row in a process table kept by the portable core: PID
  (counting up from 1), parent, name, state, flags, and what it owns (windows,
  timers, jobs, large memory). Window code stays on the UI task; a process may
  also own real FreeRTOS tasks (background jobs, shell sessions, services).
  Apps can move to their own task later, one at a time.
- **Figures in Task Manager**: FreeRTOS run-time statistics for the task view,
  and the window manager's own timing charged to the PID it is calling for
  programs that share the UI task.
  - The run-time statistics are switched on (backlog item 1,
    `docs/journal/2026-11-16-task-statistics.md`): the kernel reads the
    microsecond clock (`esp_timer`, the same for both cores) at every task
    switch and keeps a 64-bit total per task. The System port hands them out
    as a list of tasks with raw counters (`ports::System::tasks`); whoever
    shows them takes two readings and divides. `tools/dbg.py tasks` is the
    first reader. Time spent in interrupts is counted as the interrupted
    task's, so the video's share still comes from its own stopwatch
    (`scanout_us`).
- **Signals**: TERM (asks, like the close button), KILL (the OS removes what
  the process owns; it takes effect at a safe point, never while the task
  holds an OS lock) and INT (Ctrl+C). End Task asks politely first and offers
  force if the program has not closed after a few seconds.
- **Flags**: system (hidden in Task Manager's default view), protected (the
  user cannot signal it), respawn (started again if it ends). Protected: the
  serial console, the debug service, init and the window server. The taskbar
  is not protected; it restarts.
- **Priorities**: three classes, Low, Normal and High, in a narrow band below
  the video, Wi-Fi and the network stack. The FreeRTOS scheduler is not
  changed.
- **Saves**: written by a job on a worker task that belongs to the program;
  the program cannot end until its jobs have. The window is locked while it
  saves and the pointer shows an hourglass (asked for by the owner; own art).
  A save writes a temporary file and renames it over the original.
- **Shell**: in the portable core, commands built in, Linux names with DOS
  aliases (`dir`, `type`, `del`); paths keep drive letters and accept both
  `\` and `/`. One session is one task. It runs on a TTY object whose other
  end is the serial port, a telnet connection or a terminal window.
- **Serial port**: the console and the debug protocol are both live on it at
  all times. Bytes between two zero bytes that pass the frame check go to the
  debug service, everything else to the console, so `tools/dbg.py` keeps
  working whatever state the shell is in.
- **Start-up**: init is PID 1 and starts the rest from a text file. First the
  services move to after the first paint; splitting the wallpaper (window
  server) from the taskbar and icons (desktop shell) comes in a later phase.
  No artificial delays.
- **After a crash**: the machine restarts and shows an error screen naming
  the program.
- **No users and no passwords.** Security is not a goal: the OS is for
  recreation, not real use. This covers telnet; whether telnet starts by
  default or is switched on in a settings file is asked before it is built.
- **A written record**: every piece of work adds an entry to `docs/journal/`
  (what was asked, options, choice and reasons, what was built and tested).
