# The Terminal's session gets a task of its own

A follow-up to backlog item 15, made possible by item 10. Branch
`claude/project-thread-8q7ksu`.

## 1. What was asked

Nobody asked for this by name. The architecture review says "one session is
one task", and the Terminal window was built a step short of it: its session
ran on the UI task, a line at a time, because the portable code had no way
to start a task (`docs/journal/2026-11-17-terminal-window.md`, "Where the
session runs"). That entry listed what it cost and said the session would
move "with the port that items 10 or 26 bring". Item 10 brought it
(`ports::Tasks`, `job::Jobs`), so this is the move. The owner's instruction
for this run of work is "ve haciendo, no pares hasta terminar el backlog,
dejo a tu criterio", and the choices are marked "to confirm".

## 2. What was wrong before

- The desktop waited while a command worked. `cat` of a large file, or a
  `cp -r`, froze every window until it ended.
- Ctrl+C could not stop a command that was running, because the key was
  only read when the command had returned.
- Nothing that reads the keyboard itself could ever run in the window: a
  `more`, a `top`, an editor. The session could not wait for a key without
  stopping the desktop.

## 3. The options, and what was chosen

### The session as a job of the Terminal's process

- **A job (chosen).** The session's loop (`sh::Session::run()`) is the work
  of a job that lasts as long as the Terminal. The rule item 10 made for
  saves then does everything: the process cannot end, and the window object
  with the grid, the TTY and the session in it is not destroyed, until the
  session's task has returned. Closing the window hangs up the TTY (and
  sends a Ctrl+C for a command that is running); the session sees its
  terminal gone and ends; the job finishes; the process goes. End Task with
  force takes the same road through the job's `stop`.
- A task started by the Terminal directly through `ports::Tasks`. It would
  need its own version of "do not destroy me while my task runs", which is
  exactly what a job is.

### Who may touch the grid

Two tasks now use the grid of characters: the session writes into it
(through the TTY), the UI task paints it and writes the echo of each key.

- **The TTY's lock covers the grid too (chosen).** The TTY already holds its
  lock whenever it writes to its device. The window takes the same lock to
  paint, to look back in the history, and to note where the cursor is. One
  lock, held for a row's worth of work at a time.
- A second lock for the grid. Two locks taken in two orders by two tasks is
  how deadlocks are made.
- The session's task posts every piece of output to the UI task, which
  alone writes the grid. No lock at all, but a `cat` of 300 KB becomes
  thousands of messages through a queue of 32, and the session would have
  to wait for the UI task at every line.

### How the window learns there is something to paint

- **One message until the UI task has looked (chosen).** The first write
  after the window last looked posts a message to the UI task; later writes
  post nothing until that message has been delivered. However fast a
  command prints, the queue holds at most one message for a Terminal, and
  the UI task paints the rows that changed since it last looked. The message
  is posted while the TTY's lock is held, so it must not wait: if the queue
  is full the next write tries again, and the window also looks once a
  second by a timer, so a lost message costs a second at worst.
- The window looks at the grid thirty times a second by a timer. Simple,
  and it keeps the UI task from ever sleeping while a Terminal is open.

### Where the lock and the events come from

The portable code cannot make a lock or an event; the board's code can. A
session needs one lock and two events (a line was typed; the UI task has
answered).

- **A small pool handed in by the machine (chosen)**:
  `shell::set_session_kits()`, three kits for the three Terminals that can
  be open. No heap, nothing made or destroyed while the machine runs.
- A port that makes locks and events on request. More general, and it
  brings their lifetime with it; nothing else needs it yet. The serial
  console and telnet (items 14 and 17) are made by the board's code and can
  make their own.

### The machine without tasks

The browser and the PC desktop program have no tasks. There the Terminal
works as it did: the session on the UI task, a line at a time. Both ways
are in `terminal.cpp`; the difference is whether the window was given a kit.
Keeping the old way costs about forty lines and keeps the Terminal in the
browser.

## 4. What was built

- `core/src/shell/terminal.cpp`: `begin()` starts the session as a job when
  the window has a kit; `Shown` (the TTY's device) writes the grid and posts
  the "look" message; painting, the history keys and the cursor take the
  lock; `closed()` and the job's `stop` hang up; the job's `done` closes the
  window after `exit`.
- `sh::QueueCall` gives the session its way to the process table (`ps`,
  `kill`) from its own task, as the shell's tests already did.
- `job::Jobs::threaded()`: whether a job would get a task. A session must
  not be started as a job where it would not, since its work only returns
  when the Terminal closes.
- `shell::SessionKit` and `set_session_kits()`; on the board `RtosMutex`
  and `RtosEvent` in `firmware/os/main/rtos_tasks.hpp`, three kits made in
  `main.cpp`.
- The session's task asks for a 12 KB stack. **To be measured on the
  board** (`tools/dbg.py tasks` with a Terminal open, after `cp -r` of
  nested folders).
- Test, in `host/tests/test_terminal.cpp`, with real threads: the greeting
  arrives from the session's task; a key is echoed by the UI task at once;
  `ps` through the UI task; a 300 KB `cat` stopped by Ctrl+C with the UI
  task taking keys meanwhile; `exit`; the close button; End Task with force
  in the middle of a command; three Terminals at once and a fourth refused;
  the memory and the kits back after each. It ran 80 times in a row. The
  old way is still tested by the test that was there.
- 29 PC tests pass. The firmware compiles and links. **Not run on the
  board.**

## 5. What was learned

- What is typed while a command runs is echoed where the cursor is, in the
  middle of the command's output, and then run when the prompt is back. Any
  terminal does this; it only looked wrong in the test, which typed faster
  than a person and expected tidy lines.
- A test that calls "repaint everything and compare" inside a retry loop
  proves nothing: the first try repairs the screen. The check is now made
  once, after the session is known to be asleep at its prompt.

## 6. What is left

- Programs that read keys themselves (`more`, `top`) can now be written;
  none exists yet.
- The bell is still silent.
- On the board: the stack figure above, and how the desktop feels while a
  long `cat` scrolls (the session's task runs one priority below the UI
  task).
