# The Terminal is an emulator; the session is a process of its own

2026-11-17. Backlog item 44; finishes item 20 for the programs.

## What was asked

The owner, after item 39:

> Terminemos la terminal, la terminal debería ser algo independiente del UI,
> la aplicacion UI de terminal sería un terminal emulator que hace
> forwarding hacia un proceso de terminal TTY

So: two things where there was one. A terminal that is not part of the
desktop, and a window that is only a terminal emulator and forwards to it.

## What was found

The Terminal window (`core/src/shell/terminal.cpp`) held everything in one
object and one process:

| In the window object | What it is |
| --- | --- |
| `tty::Screen`, the view, the scroll bar | The emulator: a grid, a font, the keys |
| `tty::Tty` | The terminal as the OS sees it: echo, line editing, Ctrl+C |
| `sh::Session` | The command shell |
| The job `terminal`, 12 KB of stack | The task the shell runs on |
| The memory `ted` asks for | Counted as the Terminal's |

`ps` showed one row, `Terminal`, about 27 KB with a task. The window could
not be destroyed when it closed: it stayed, off the screen, until the
shell's task had returned, because the task was running code inside the
window object. That was the one exception to "a window is destroyed when it
closes". And `terminal.cpp` was the last program not on the OS calls
(15 of 16), because it built the shell's environment from the drives, the
process table and the System port by itself.

The TTY already drew the right line: it has a device end (bytes typed in, a
sink for what is shown) and a program end. The serial console and telnet
are devices of it. The window was a device and a host at once.

## The options

**What the "terminal process" is.**

| Option | What `ps` shows | For | Against |
| --- | --- | --- | --- |
| 1. A session process `sh`, child of the window's (chosen) | `Terminal` and under it `sh` | What Unix shows for xterm and its shell; the task, its stack and what commands use are charged to `sh`; nothing new to invent | One more row and a second block of memory per Terminal |
| 2. A `tty` server process that holds every session | `Terminal` rows and one `tty` | One place for all terminals, like a console driver | A single row would carry three shells' time and memory: the figures say less, not more; and `kill` could not end one session |
| 3. Keep one process, split only the classes | `Terminal` | Least change | Not what was asked: nothing would be independent of the window |

**What closing the window does to the session.** Put to the owner on a
card, with "the session ends, like xterm" recommended:

- The session ends (chosen by the owner). The TTY is hung up, a command
  that is running is stopped as Ctrl+C would, the shell ends and the `sh`
  process goes.
- The session lives on and a new window can attach to it (like `screen`).
  Not chosen: it needs a way to list and reattach sessions, and a place to
  keep what was on the screen, which is the emulator's.

**How the window asks for a session.** A new OS call, `os::open_session`,
filled in by the desktop's side of the system the way `os::shut_down` is.
The other way was for the window to make the session object itself, which
is what it did before and is why it could not move to the OS calls.

## What was chosen, by whom

The owner asked for the split and chose what closing does. The rest is the
agent's, to confirm:

- The session is a process named `sh`, not windowed, child of the process
  that asked for it. Its parent going does not end it by itself: the
  window's `closed()` hangs it up. An orphan would go to init, as on Unix.
- `kill` and End Task work on `sh` too (TERM and KILL are the same to it:
  the terminal is hung up). The window then closes, as it does on `exit`
  and Ctrl+D.
- The shell's task is named `sh` (it was `terminal`).
- The session lends its lock to the device for the grid. The window does
  not have a lock of its own any more; it borrows one for as long as there
  are two tasks.
- A fourth Terminal is refused by the window (three places on the screen);
  "a Terminal is still closing" is said by the system, when the three kits
  are all still held.
- If there is no room in the process table the Terminal does not open.
  Before, its window became the caller's; with messages addressed to the
  window's process that is no longer safe.

## What was built

| File | What |
| --- | --- |
| `core/include/core/sh/terminal_session.hpp`, `core/src/sh/terminal_session.cpp` | `sh::TerminalSession`: a TTY, a shell and its task. No window, no grid, no desktop. The device end is `typed()`, `set_size()`, `hang_up()`, `raw()`, `lock()`, `on_ended()` and a `ports::ByteSink` |
| `core/src/shell/sessions.cpp` | The system's side: makes the `sh` process, picks a kit, says what the shell may reach, takes the process away when the session is over. `set_session_kits` and `session_environment` moved here |
| `core/os/os.hpp` | `os::open_session(device, why)`, `os::caret_moved()` |
| `core/src/shell/terminal.cpp` | Only the emulator: grid, view, scroll bar, keys. No TTY, no shell, no job. Uses only the OS calls and is on the `MOVED` list: 17 of 17 |
| `core/src/init/init.cpp` | The board thread's note: "init: taskbar goes before taskbar" is no longer written when a needed thing is started by its own line |

How an ending goes, each way:

| What happens | Then |
| --- | --- |
| The window closes (button, End Task on `Terminal`) | `hang_up()`: Ctrl+C to the command, the TTY is hung up, and nothing is written to the grid after that call returns. The window is destroyed and `Terminal` leaves the table at once. `sh` goes when its task has returned |
| `exit`, Ctrl+D | The task returns; the session tells the window, which closes; then the system destroys the session and ends `sh` |
| `kill` or End Task on `sh` | The same as the terminal going, then as above |

The window now follows the rule every other window follows: it is destroyed
when it closes. What waits for the shell's task is the session, which is
the thing the task runs in.

## How it was tested

On the PC, 41 tests, with the address and undefined-behaviour checkers:

- `test_terminal`: two rows and two blocks per Terminal; `sh` is the child
  of `Terminal`, not windowed, holds its own memory; the task is `sh`'s job
  and not the window's; `ted`'s 128 KB is `sh`'s; `exit`, Ctrl+D, the close
  button, `kill` on either process from inside, TERM and KILL on `sh` from
  outside, KILL on `Terminal` in the middle of `cat` of a large file: every
  time both rows go, the kit comes back and the memory is as before. With
  tasks, the window's row is gone at the first safe point after closing,
  before the shell has stopped.
- Pictures: `ps` shows `Terminal` (23K) and `sh` (5K) where it showed
  `Terminal` (27K); four pictures redrawn for that line.
- `tools/check_os_calls.py`: 17 of 17.

The firmware compiles and links in the cloud container (0x169610 of
0x1f0000). It has not run. For the board: open, type, `ps` (two rows, `sh`
under `Terminal`), `top` while a command runs (time on `sh`), close in the
middle of `cat` of a large file, three at once and a fourth, `kill` of
`sh`, memory before and after, `dbg.py misses`.

## What was left out

- The serial console and telnet still make their own TTY and shell
  (`sh::Console`, `net::Telnet`). They are devices too and could ask for a
  `sh::TerminalSession`; `ps` would then show an `sh` for each login. Not
  done here: they run under `inetd`'s task, not the UI task, and that
  needs its own look.
- The shell's commands still reach the drives and the process table through
  `sh::Env`, which the system fills in. Item 20's test is about the
  programs, and all of them have moved; whether the commands should ask
  through `os::` as well belongs with item 37 (handles), since `Env` is
  already a list of what a session was handed.

## What was learned

The exception was a sign. "Windows are destroyed when they close, except
the Terminal, which waits for its task" read as a detail of jobs. It was
the window holding something that was not a window.
