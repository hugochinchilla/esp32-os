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
| 3 | Per-process processor time and memory. Task Manager shows PID, time and memory columns | M | 1, 2 | Yes | todo |
| 4 | System, protected and respawn flags | S | 2 | | todo |
| 5 | TERM and KILL for window programs. End Task asks politely, then offers force | S | 2 | | todo |
| 6 | Atomic saves: temporary file, then rename | S | | Yes | done (checked on the board 2026-11-16) |
| 6b | Hourglass pointer while a program makes the user wait, first of all during a save (own art) | S | | Yes | done (checked on the board 2026-11-16) |
| 7 | Watchdog, crash record, and an error screen on the next start | M | 2 | Yes | todo |
| | **Phase 2: work off the UI task** | | | | |
| 8 | Message queue into the UI task. The loop sleeps until there is work | M | | Yes | on a branch (`claude/project-thread-2jwyaw`): PC tests pass, firmware compiled, waiting for the board. [Journal](journal/2026-11-16-message-queue.md) |
| 8b | Services loop: one task for the network services (VNC, the web viewer, later the telnet listener) that sleeps on all their sockets and reaches the desktop only by messages. The network leaves the UI loop | M | 8 | Yes | todo |
| 9 | Lock on the file system | S | | | todo |
| 10 | Jobs owned by a process, and "cannot exit until jobs finish". The Editor saves through a job, its window locked meanwhile | M | 2, 8, 9 | Yes | todo |
| 11 | Three priority classes (Low, Normal, High) for jobs and services | S | 10 | Yes | todo |
| | **Phase 3: the console** | | | | |
| 12 | TTY object: echo, line editing, Ctrl+C | M | 8 | | todo |
| 13 | Shell with the file and process commands (Linux names, DOS aliases, both slashes), tested on the PC | L | 2, 9, 12 | | todo |
| 14 | Serial console sharing the wire with the debug link | M | 12, 13 | Yes, with care | todo |
| 15 | Terminal window on the desktop | M | 12, 13 | | todo |
| 16 | `top`, `dmesg`, `edit`, `open`, `run`, and the Run... dialog | S | 3, 13 | | todo |
| 17 | Telnet server, no password. Ask the owner first: on by default, or switched on in a settings file | S | 13 | Yes | todo |
| | **Phase 4: structure** | | | | |
| 18 | init as PID 1 with a start-up file. Services start after the desktop | M | 2, 8 | Yes | todo |
| 19 | Program lifecycle: created at launch, destroyed at exit, memory returned, more than one instance | L | 2 | Yes | todo |
| 20 | A defined set of OS calls, moved to app by app | L | 2 | | todo |
| 21 | Split the window manager into window server and desktop shell. The taskbar becomes a process that restarts when killed | L | 18, 20 | Yes | todo |
| | **Later** | | | | |
| 22 | Console text editor | M | 13 | | todo |
| 23 | Pipes and redirection | M | 13 | | todo |
| 24 | File dates | S to M | | Yes | todo |
| 25 | Updates over Wi-Fi | M | | Yes | todo |
| 26 | Apps on their own tasks, one at a time | XL | 8, 20 | Yes | todo |
| 27 | Loadable or sandboxed apps | XL | 19, 20 | Yes | todo |

Item 14 is the only one that puts the agents' access to the board at risk
while it is being built. It is tried on a branch with the old firmware ready
to flash back, since a mistake there cuts the link used to diagnose it.

Items 1, 2, 6, 9, 12 and 13 touch different files and can be separate branches
at the same time.
