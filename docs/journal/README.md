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
| 2026-11-17 | [The desktop program](2026-11-17-desktop-program.md) | Backlog item 32: Chinchilla in an SDL2 window on Linux. How the picture is scaled, whose pointer is shown, which keys the PC keeps, where C: lives, why there is no sound, and what is ready for the browser |
| 2026-11-17 | [A folder name copied into itself](2026-11-17-folder-copy-overlap.md) | The snprintf overlap GCC found in the Release build, and the fix |
| 2026-11-17 | [The watchdog, the crash record and the error screen](2026-11-17-watchdog.md) | Backlog item 7: a supervisor that notices a stuck desktop, what is written down in the machine's last moment and where it waits, ESP-IDF's panic handler wrapped, and the screen at the next start that names the program |
