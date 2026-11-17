# Backlog

The work that turns the prototype into more of an operating system. It comes
from the architecture review of 2026-11-16
([journal](journal/2026-11-16-architecture-review.md)); the decisions it rests
on are in [`DECISIONS.md`](DECISIONS.md) under "OS architecture".

The order puts the pieces that others depend on first. Change the Status cell
in the same change that does the work, and add a journal entry for it.

Sizes: S is one branch in one sitting. M is a few sittings. L is a milestone of
its own. XL touches most of the code. "Board" marks items that cannot be
trusted from PC tests alone.

Status: `todo`, `doing` (with the branch), `on a branch` (waiting for the
board or the merge), `done`.

| # | Item | Size | Depends on | Board | Status |
| --- | --- | --- | --- | --- | --- |
| | **Phase 1: know what is running** | | | | |
| 1 | FreeRTOS run-time statistics in the System port, shown by `tools/dbg.py` | S | | Yes | done (checked on the board 2026-11-16) |
| 2 | Process table with PIDs. Windows and timers carry their owner. Existing services get rows | M | | | done (checked on the board 2026-11-16). [Journal](journal/2026-11-16-process-table.md) |
| 3 | Per-process processor time and memory. Task Manager shows PID, time and memory columns | M | 1, 2 | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-process-figures.md) |
| 4 | System, protected and respawn flags | S | 2 | | done (checked on the board 2026-11-16). [Journal](journal/2026-11-16-signals-and-flags.md) |
| 5 | TERM and KILL for window programs. End Task asks politely, then offers force | S | 2 | | done (checked on the board 2026-11-16). [Journal](journal/2026-11-16-signals-and-flags.md) |
| 5b | TERM and KILL for services: each gets a stop function, so ending one really stops it. Waits until a service is a real task with a row (their label-only rows were removed, [journal](journal/2026-11-16-only-real-tasks.md)) | S | 5, 18 | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-stopping-services.md) |
| 6 | Atomic saves: temporary file, then rename | S | | Yes | done (checked on the board 2026-11-16) |
| 6b | Hourglass pointer while a program makes the user wait, first of all during a save (own art) | S | | Yes | done (checked on the board 2026-11-16) |
| 7 | Watchdog, crash record, and an error screen on the next start | M | 2 | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-watchdog.md) |
| | **Phase 2: work off the UI task** | | | | |
| 8 | Message queue into the UI task. The loop sleeps until there is work | M | | Yes | done (checked on the board 2026-11-16). [Journal](journal/2026-11-16-message-queue.md) |
| 8b | Services loop: one task for the network services (VNC, the web viewer, later the telnet listener) that sleeps on all their sockets and reaches the desktop only by messages. The network leaves the UI loop | M | 8 | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-services-loop.md) |
| 9 | Lock on the file system | S | | | done (checked on the board 2026-11-16). [Journal](journal/2026-11-16-file-system-lock.md) |
| 10 | Jobs owned by a process, and "cannot exit until jobs finish". The Editor saves through a job, its window locked meanwhile | M | 2, 8, 9 | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-jobs.md) |
| 11 | Three priority classes (Low, Normal, High) for jobs and services | S | 10 | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-priorities.md) |
| | **Phase 3: the console** | | | | |
| 12 | TTY object: echo, line editing, Ctrl+C | M | 8 | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-tty.md) |
| 13 | Shell with the file and process commands (Linux names, DOS aliases, both slashes), tested on the PC | L | 2, 9, 12 | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-command-shell.md) |
| 14 | Serial console sharing the wire with the debug link | M | 12, 13 | Yes, with care | done (checked on the board 2026-11-17), off by default; a known limit is left as 14b. [Journal](journal/2026-11-17-serial-console.md) |
| 14b | Serial console: text lost when the PC tool is busy (a listing streaming while screenshots and a put run; about 270 lines lost). Read the port on a thread in `dbg.py`, or pace on the board | S | 14 | | done (checked on the board 2026-11-17): `dbg.py` reads the port on a thread; the 300 KB listing arrived whole three times out of three. [Journal](journal/2026-11-17-dbg-reader-thread.md) |
| 15 | Terminal window on the desktop | M | 12, 13 | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-terminal-window.md) |
| 16 | `top`, `dmesg`, `edit`, `open`, `run`, and the Run... dialog | S | 3, 13 | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-run.md), [dmesg](journal/2026-11-17-dmesg.md) |
| 17 | Telnet server, no password. Ask the owner first: on by default, or switched on in a settings file | S | 13 | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-telnet.md) |
| | **Phase 4: structure** | | | | |
| 18 | init as PID 1 with a start-up file. Services start after the desktop | M | 2, 8 | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-init.md) |
| 19 | Program lifecycle: created at launch, destroyed at exit, memory returned, more than one instance | L | 2 | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-program-lifecycle.md) |
| 34 | Display and the other built-in dialogs and settings tools (Screen Saver, Network, Shut Down, file dialogs, message box, Task Manager) become programs with a life of their own: created when opened, memory given back when closed. Display first. The owner's rule of 2026-11-17 | M | 19 | Yes | done with item 19 (checked on the board 2026-11-17). [Journal](journal/2026-11-17-program-lifecycle.md) (the rule: [dialogs as programs](journal/2026-11-17-dialogs-as-programs.md)) |
| 20 | A defined set of OS calls, moved to app by app | L | 2 | | done for the programs (17 of 17; the last, the Terminal, on the PC only). Checked on the board 2026-11-17: `core/os/os.hpp`, 11 of 16 programs moved, a test keeps them moved. Display, Screen Saver, Network and Shut Down moved too, 15 of 16, checked on the board 2026-11-17. The Terminal moved when its session left it (item 44). The list: [docs/os-calls.md](os-calls.md). Journals: [the calls](journal/2026-11-17-os-calls.md), [the settings tools](journal/2026-11-17-os-calls-settings-tools.md), [the Terminal](journal/2026-11-17-terminal-split.md) |
| 21 | Split the window manager into window server and desktop shell. The taskbar becomes a process that restarts when killed | L | 18, 20 | Yes | the process: done (checked on the board 2026-11-17). The code cut in two (the owner's decision, 2026-11-17): checked on the board 2026-11-17: the taskbar and the icons are panels drawn by `core/desk`, the server knows only windows. Journals: [the process](journal/2026-11-17-taskbar-process.md), [the cut](journal/2026-11-17-window-server-and-shell.md) |
| | **Later** | | | | |
| 22 | Console text editor | M | 13 | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-console-editor.md) |
| 23 | Pipes and redirection | M | 13 | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-pipes.md) |
| 24 | File dates | S to M | | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-file-dates.md) |
| 25 | Updates over Wi-Fi | M | | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-updates.md) |
| 26 | Apps on their own tasks, one at a time | XL | 8, 20 | Yes | in part. First step built (PC tests pass; not run on the board): a program can have a task of its own that the UI task hands each call to and waits for; Culebra is the first. The owner chose that the server waits for the program. First step checked on the board (works; +7 % processor time playing, 8.5 KB while open; not merged, put to the owner). Second step built, PC only: a two-second limit on the wait, after which the program is "not responding" (grey, dropped calls, can be ended) and the desktop goes on. Both steps merged after the board check (6efd5ff, 4 KB stack; a late program runs at the idle priority); a slow program that answers again checked with `dbg.py fault slow` (PR 42). The ways in between programs go through the process table and the stack is a figure per program; a first tranche has tasks (Editor, File manager, Picture viewer, Task Manager, Defragmenter, Cactus Patch), PC only, not run on the board, which programs is on a card to the owner. Left: Terminal and the three system-kind programs, the queue of events. [Journal](journal/2026-11-17-program-tasks.md) |
| 27 | Loadable or sandboxed apps | XL | 19, 20 | Yes | todo |
| 37 | The OS calls taken to numbered calls with handles (windows, files and timers as numbers, events through a queue, the system's tables of what is whose), as Win16 and Unix had them. To be looked at again at the end of the backlog (the owner's decision, 2026-11-17); it is what item 27 needs | L to XL | 20 | Yes | in part. First step built (PC tests pass, firmware compiles, not run on the board): open files are handles in a table of the system, closed when their process ends. [Journal](journal/2026-11-17-file-handles.md). The owner chose handles now and the queue with item 26. Another program's window is a number too (checked on the board 2026-11-17). The owner chose windows and widgets wholly by number: `os::ui` and the thin classes (`core/app/app.hpp`), drawing by calls too (the owner's choice); all 17 programs moved (checked on the board 2026-11-17, in two turns: memory all back on close, speed the same as before within noise). Left: events through a queue, with item 26. [Journal](journal/2026-11-17-ui-handles.md). [The comparison](journal/2026-11-17-os-calls.md) |
| 39 | Restarting what ends moves to init, as `respawn` in a Unix `/etc/inittab`: the restart mark, the count of quick deaths and the give-up rule leave the process table (`proc::Table::set_restart`, `may_restart`) for `init`, and `startup.txt` can say which programs come back (the taskbar is the first) | M | 18, 21 | Yes | done (checked on the board 2026-11-17): `startup.txt` in inittab's fields, `WHAT:ACTION` with `once`, `respawn`, `off`. After that, on a branch (`claude/project-thread-8q7ksu`): the old lines (`start`, `run`) are not read any more (the owner's rule, no backward compatibility); PC tests pass, firmware compiles, not run on the board. [Journal](journal/2026-11-17-init-respawn.md) |

Item 14 is the only one that puts the agents' access to the board at risk
while it is being built. It is tried on a branch with the old firmware ready
to flash back, since a mistake there cuts the link used to diagnose it.

Items 1, 2, 6, 9, 12 and 13 touch different files and can be separate branches
at the same time.
| | **The look** | | | | |
| 28 | Taskbar without the dead quick-launch icons; Back and Up with their own icons in the File manager | S | | | done (checked on the board 2026-11-16). [Journal](journal/2026-11-16-icons.md) |
| 29 | Refined icons for every program, at 32x32 and 16x16. The owner picks from drawn proposals | M | | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-16-icon-proposals.md) |
| 30 | Quick-launch icons by the Start button that launch something | S | 29 | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-quick-launch.md) |
| 31 | Desktop background: 16 colours and 10 patterns, chosen in a Display dialog (Start > Settings, or a right click on the desktop) | S | | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-desktop-background.md) |
| 41 | Second review round: Start button two pixels wider; Display offers all 256 board colours (Other...) and a list of solid, undithered colours; the Terminal resizes, maximises and has a scroll bar | S | 15, 31 | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-review-round-2.md) |
| 42 | Desktop icons stay where they are dragged to; "Snap icons to grid" and "Realign icons" in the desktop's menu; the places kept in `icons.ini` | S | 31 | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-icon-places.md) |
| 43 | The default desktop colour is the solid Steel blue, not the dithered blue (the owner's choice) | S | 41 | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-solid-default.md) |
| 44 | The Terminal split in two (the owner's request): the window is a terminal emulator; the TTY and the shell are a session, a process `sh` of its own, child of the window's. Closing the window ends the session, like xterm | M | 15, 20 | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-terminal-split.md) |
| 40 | Submenus hand over at once: with a submenu open, another item of a parent menu closes it, is highlighted and opens its own in the same move, at every depth | S | | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-submenus-at-once.md) |
| | **Memory** | | | | |
| 35 | Our static objects in external RAM: the core's and the firmware's big ones, about 121 KB out of internal RAM | S | | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-statics-in-psram.md) |
| 36 | The remote screen holds its 183 KB of buffers only while a viewer is connected | S | 8b | Yes | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-remote-screen-memory.md) |
| 45 | The browser viewer can send the keys the viewing computer keeps for itself: buttons for Ctrl+Alt+Del, Alt+Tab, Alt+F4 and the logo key | S | | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-viewer-system-keys.md) |
| 38 | Missed video refills in the system log, told apart from the debug link's; `dbg.py dmesg` and `dbg.py misses` | S | | | done (checked on the board 2026-11-17). [Journal](journal/2026-11-17-missed-refill-log.md) |
| 47 | The PC test `dbg_tool` fails now and then when the tests run in parallel (seen once in the cloud and once on the laptop, 2026-11-17; it passes alone): find what in it depends on the load | S | | | todo |
| | **Chinchilla off the board** | | | | |
| 32 | Chinchilla as a desktop program for Linux: a window (SDL2) on the same `core/`, the real mouse and keyboard, sound, a folder of the PC as C:. Not an emulator of the ESP32: no real Wi-Fi, no memory limits, much faster than the board | M | | | done on the PC (2026-11-17): built, tested with a real mouse and keyboard on a virtual screen; not yet run on the owner's desktop. No sound: the core makes none yet. [The idea](journal/2026-11-17-chinchilla-on-a-pc.md), [the program](journal/2026-11-17-desktop-program.md) |
| 33 | Chinchilla in a web browser (WebAssembly): a link anyone can open, C: kept in the browser, the page published somewhere (GitHub Pages?) | M | 32 | | built and tested in Chromium on the PC (2026-11-17); **not published yet**: the Pages workflow is in `ci/` and has to be turned on by the owner. [Journal](journal/2026-11-17-chinchilla-in-the-browser.md) |
| | **Start-up** | | | | |
| 46 | A BIOS stage before the OS (the owner's idea): its own app partition with the self-test on a text screen (Spleen 8x16, the mascot on top), plain text on the serial port, later a Setup; it restarts into the OS, where init shows the splash | L | 7, 25 | Yes | doing (`claude/project-thread-y7w3uh`): step 1 of 3, the text screen and its self-test, is built, tested on the PC and checked on the board (merged; [journal](journal/2026-11-17-bios-text-screen.md)); the board shows the same screens, the new one is not in the firmware yet. Decided by the owner: own partition, Spleen 8x16, the blank accepted, 1 MB from C: ([discussion](journal/2026-11-17-bios-stage.md), [measurement](journal/2026-11-17-bios-restart-measured.md)). Next: step 2, the partition table, bootloader hook and BIOS firmware (a board turn that wipes C:) |
