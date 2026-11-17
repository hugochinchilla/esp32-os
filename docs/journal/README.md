# Journal

How Chinchilla came to be the way it is, told as it happened. The owner is
recording the project, so this is the written memory behind it: what was asked,
what was found, which options were on the table, which was chosen and why, and
why the others were not.

It complements two other files:

- [`docs/DECISIONS.md`](../DECISIONS.md) is the reference: the current state of
  every decision, short, kept up to date.
- [`docs/BACKLOG.md`](../BACKLOG.md) is the list of work, with its status.

The journal is the story. Entries are not rewritten later; a correction is a new
entry that says what changed.

## Writing an entry

One file per piece of work or conversation, named `YYYY-MM-DD-topic.md`, added
in the same change as the work it describes. An entry has, as far as they
apply:

1. **What was asked**, in the owner's own words where there is a message
   (in the language he wrote it; the rest of the entry is in English).
2. **What was found**: how things worked before, with file names and figures.
3. **The options considered**, each with what it would cost.
4. **What was chosen, by whom, and why**, and why each other option was not.
5. **What was built**, how it was tested, and what was not tested (PC only,
   compiled only, run on the board).
6. **What was learned**: surprises, mistakes, things worth telling.
7. **What is left**, with pointers to the backlog.

Plain words, real numbers, no summaries of summaries.

## Entries

| Date | Entry | About |
| --- | --- | --- |
| 2026-11-16 | [Architecture review](2026-11-16-architecture-review.md) | How the OS works today, the owner's ideas for processes, signals, a shell, telnet, a TTY and init, twelve design choices and what was decided |
| 2026-11-16 | [Safe saves and the hourglass](2026-11-16-safe-saves-and-hourglass.md) | Saves write a temporary file and swap it in; the hourglass pointer, chosen from four drawings; why a frozen desktop can still show it |
| 2026-11-16 | [Task statistics](2026-11-16-task-statistics.md) | Backlog item 1: the kernel counts processor time per task, the System port lists the tasks, `tools/dbg.py tasks` shows them. Seven small choices and what interrupts do to the figures |
| 2026-11-16 | [The flash-write shake](2026-11-16-flash-write-shake.md) | The picture shook while LittleFS wrote; the cache, PSRAM and the fix |
| 2026-11-16 | [The flash drive and settings](2026-11-16-littlefs-and-settings.md) | C: on LittleFS, and one plain INI file per program in C:\.config |
| 2026-11-16 | [Wi-Fi scanlines](2026-11-16-wifi-scanlines.md) | Lines while the radio connects, measured in the video interrupt; 2 dBm by default |
| 2026-11-16 | [Start-up screens](2026-11-16-boot-screens.md) | Waiting for the monitor, real self-test values, the countdown, the splash hidden |
| 2026-11-16 | [Files on the desktop](2026-11-16-desktop-files.md) | C:\Documents\Desktop as icons, welcome.txt, C:\docs moved into C:\Documents |
| 2026-11-16 | [The Trash](2026-11-16-trash.md) | Deletes to C:\.trash, Restore, Empty Trash, drag and drop |
| 2026-11-16 | [Painting each pixel once](2026-11-16-painting-once.md) | Overdraw counted, a 12-line scratch band, before and after |
| 2026-11-16 | [Slow-pointer submenus](2026-11-16-start-menu-submenus.md) | Why a slow pointer closed Start > Programs, and the fix |
| 2026-11-16 | [Internal RAM](2026-11-16-internal-ram.md) | Where the 512 KB of internal SRAM goes, what Wi-Fi costs, wired alternatives and cheap wins (report, nothing applied) |
| 2026-11-16 | [One-shot fast start](2026-11-16-one-shot-fast-boot.md) | `dbg.py reboot --fast` and `boot fast-next`; why NVS and not RTC memory |
| 2026-11-16 | [Key repeat after a long save](2026-11-16-key-repeat.md) | Why a letter appeared in the Editor after File > Save, and the fix |
| 2026-11-16 | [The process table](2026-11-16-process-table.md) | PIDs for programs and services, windows and timers that know their owner, how the OS knows who is asking, when a process ends, and Task Manager's new list |
| 2026-11-16 | [The message queue](2026-11-16-message-queue.md) | Backlog item 8: how another task gets work done on the desktop, what a message is, and a UI loop that sleeps until a message, serial bytes or a timer instead of looking 100 times a second |
| 2026-11-16 | [TERM, KILL and protected processes](2026-11-16-signals-and-flags.md) | Signals that wait on the process's row until a safe point, End Task that asks first and offers force after 5 seconds, a greyed End Task for protected processes, and respawn |
| 2026-11-16 | [A lock on the file system](2026-11-16-file-system-lock.md) | Backlog item 9: one lock around `fs::Storage` so a second task can use files; where it could have gone instead, how long it is held, and a two-thread test that proves itself |
| 2026-11-16 | [Only real tasks are listed](2026-11-16-only-real-tasks.md) | The owner's rule: no rows for services that are only names. `dbgd`, `netd`, `vncd` and `httpd` leave the process table until each is a task, and so does `init`: the desktop is PID 1 for now |
| 2026-11-16 | [VNC connect times](2026-11-16-vnc-connect.md) | Five connects on main, the network spread, and a correction to the PR 18 transfer figure |
| 2026-11-16 | [Icons: the inventory, the taskbar and the folder toolbar](2026-11-16-icons.md) | Which icons exist and at what sizes, the quick-launch icons taken out, Back and Up told apart |
| 2026-11-16 | [Icon proposals, and the Editor's new icon](2026-11-16-icon-proposals.md) | Three ideas for the Editor, one for each other program; the owner chose the notepad and pencil |
| 2026-11-17 | [The desktop's colour and pattern](2026-11-17-desktop-background.md) | 16 colours and 10 patterns, the Display dialog, a right click on the desktop |
| 2026-11-17 | [Chinchilla on a PC](2026-11-17-chinchilla-on-a-pc.md) | Where the screenshots come from, and the owner's idea of a desktop program and a web page (backlog 32, 33) |
| 2026-11-17 | [Dialogs as programs](2026-11-17-dialogs-as-programs.md) | The owner's rule that new dialogs are programs of their own; how programs really run today; what frees memory and what does not |
| 2026-11-17 | [Our statics in external RAM](2026-11-17-statics-in-psram.md) | 121 KB of internal RAM freed, what stayed internal and why, before and after |
| 2026-11-17 | [The services loop](2026-11-17-services-loop.md) | Backlog item 8b: one task, `inetd`, for the remote screen and its web page, asleep in `select()` on all their sockets; a viewer's keys as messages; what two tasks share and how; a two-thread test |
| 2026-11-17 | [The desktop program](2026-11-17-desktop-program.md) | Backlog item 32: Chinchilla in an SDL2 window on Linux. How the picture is scaled, whose pointer is shown, which keys the PC keeps, where C: lives, why there is no sound, and what is ready for the browser |
| 2026-11-17 | [A folder name copied into itself](2026-11-17-folder-copy-overlap.md) | The snprintf overlap GCC found in the Release build, and the fix |
| 2026-11-17 | [The watchdog, the crash record and the error screen](2026-11-17-watchdog.md) | Backlog item 7: a supervisor that notices a stuck desktop, what is written down in the machine's last moment and where it waits, ESP-IDF's panic handler wrapped, and the screen at the next start that names the program |
| 2026-11-17 | [Chinchilla in the browser](2026-11-17-chinchilla-in-the-browser.md) | Backlog item 33: the desktop program built to WebAssembly. How a program that waits lives in a browser, where C: is kept, one file of 1.7 MB, and what publishing it on GitHub Pages still needs |
| 2026-11-17 | [Programs made when opened, gone when closed](2026-11-17-program-lifecycle.md) | Backlog 19 and 34: every window is created on open and destroyed on close, where the memory comes from, who destroys it and when, several Editors at once, and what a program now has to save |
| 2026-11-17 | [The remote screen gives its memory back](2026-11-17-remote-screen-memory.md) | The 183 KB a VNC viewer needs are held only while one is connected: when to give them back, and why at once |
| 2026-11-17 | [The TTY](2026-11-17-tty.md) | Backlog item 12: the text terminal between a device and a program. Who edits the line, how much editing, how it is redrawn with only spaces and backspaces, how a session sleeps until Enter, and what Ctrl+C does |
| 2026-11-17 | [The command shell](2026-11-17-command-shell.md) | Backlog item 13: a prompt and twenty commands with Linux and DOS names. Slashes against switches, wildcards without a list, who may read the process table, and the shell in a terminal of the PC |
| 2026-11-17 | [The Terminal window](2026-11-17-terminal-window.md) | Backlog item 15: the shell in a window. Why its session runs on the UI task for now and what that costs, how much of a terminal it is, 175 lines kept above the screen, and the keys |
| 2026-11-17 | [Background jobs](2026-11-17-jobs.md) | Backlog item 10: work on a task of the program's own. A task per job or a worker, why a KILL waits for a job instead of deleting it, the locked window and its hourglass, the Editor's save as the first job, and what a machine without tasks does |
| 2026-11-17 | [The Terminal's session gets a task of its own](2026-11-17-terminal-task.md) | The session as a job of the Terminal: the desktop goes on while a command works and Ctrl+C stops it. One lock for the TTY and the grid, one message until the UI task has looked, and where a portable program gets a lock from |
| 2026-11-17 | [Pipes and redirection](2026-11-17-pipes.md) | Backlog item 23: the pipe sign, `<`, `>` and `>>` at the prompt, and `more`, `grep`, `head`, `tail`, `wc` to use them with. A pipe as a hidden file, the DOS way, against real pipes; where errors go; Latin-1 in files and UTF-8 on the screen |
| 2026-11-17 | [Run..., and open, run and edit at the prompt](2026-11-17-run.md) | Backlog item 16 in part: the Run dialog as a program of its own, what it accepts and what it remembers, how a shell that knows nothing of the desktop opens windows on it, and why `top` and `dmesg` wait |
| 2026-11-17 | [Processor time and memory for each process, and top](2026-11-17-process-figures.md) | Backlog item 3 and the `top` of item 16: how time is told apart when the programs share one task, a note in front of each block for memory, what is not counted, the two new columns in the Task Manager and `ps`, and a `top` that also works into a file |
| 2026-11-17 | [Three priority classes, and the time of a job that is still running](2026-11-17-priorities.md) | Backlog item 11: Low, Normal and High as 3, 4 and 5 beside the desktop's 5, why High is not above it, a class per job and not per process, and a task that waits to be given back so its time can be read while it runs |
| 2026-11-17 | [File dates](2026-11-17-file-dates.md) | Backlog item 24: one date per file, kept in UTC; what to show for a file written before the board knows the time; the date in `dir`, in the File manager's status bar, and a `date` command |
| 2026-11-17 | [Quick-launch icons that launch something](2026-11-17-quick-launch.md) | Backlog item 30: which three programs, how the icons behave, and why the window manager draws them while the desktop says what they are |
| 2026-11-17 | [ted, a text editor for the console](2026-11-17-console-editor.md) | Backlog item 22: a full-screen editor without modes, its name and keys, sending only the rows that changed, and where a session gets memory for a whole file |
| 2026-11-17 | [Telnet](2026-11-17-telnet.md) | Backlog item 17: the shell over the network with no password, off until a settings file says otherwise, one session at a time, and how a program waits when the network is slower than it writes |
| 2026-11-17 | [init](2026-11-17-init.md) | Backlog item 18: what the first process is on a machine with one UI task, the start-up file and its two words, what happens without a file, and the brake on programs that keep ending |
| 2026-11-17 | [The serial console](2026-11-17-serial-console.md) | Backlog item 14: the shell on the debug link's wire. How typing is told from frames without waiting for a zero, what a stray zero must not do, plain text between frames, and why it is off until asked for |
| 2026-11-17 | [dmesg](2026-11-17-dmesg.md) | The last piece of backlog item 16: the log lines kept in a ring in memory, why not a file, what happens when two tasks log at once, and the command that shows them |
| 2026-11-17 | [Updates over Wi-Fi](2026-11-17-updates.md) | Backlog item 25: the PC sends the new version, one line of text each way, two app slots in the flash, a version on trial for ten seconds, and off until a settings file says otherwise |
| 2026-11-17 | [Ending a service](2026-11-17-stopping-services.md) | Backlog item 5b: a stop function on the row of a process without windows, how a task asleep on its sockets hears it must end, and what ending inetd gives back |
| 2026-11-17 | [The OS calls](2026-11-17-os-calls.md) | Backlog item 20, first part: one door between the programs and the system, why a thin layer and not numbered calls yet, a test that keeps a moved program moved, and seven programs through it |
| 2026-11-17 | [The taskbar is a process](2026-11-17-taskbar-process.md) | Backlog item 21, first part: the taskbar, the Start menu and the icons leave the protected desktop for a process that can be ended and comes back; what goes with it, how the eye gets to see it go, and what happens if it is ended too often |
| 2026-11-17 | [The start-up screens sleep while they wait](2026-11-17-self-test-sleeps.md) | The task watchdog warned about IDLE0 during the start-up screens: every wait was a busy loop. A nap while more than 20 ms of a wait is left, why not one tick everywhere, and three unused names gone |
| 2026-11-17 | [The missed refills were our own measuring](2026-11-17-refills.md) | Missed video refills at VNC disconnects and with dbg.py tasks: the task list holds the kernel lock 0.8 ms; the memory report no longer lists stacks, and the VNC misses vanish |
| 2026-11-17 | [The window server and the desktop shell](2026-11-17-window-server-and-shell.md) | Backlog item 21, second part: the three depths of the cut as the owner was told them, with Windows 95, Mac OS and X11 beside them; why panels and not special windows; the taskbar and the icons out of the window manager, the screen the same to the pixel |
| 2026-11-17 | [The OS calls: Display, Screen Saver, Network and Shut Down](2026-11-17-os-calls-settings-tools.md) | Backlog item 20, second part: four programs that were half window and half something the system keeps going, cut along that line; six new calls, why not one general setting call, and 15 of 16 programs moved |
