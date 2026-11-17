# Programs on tasks of their own

2026-11-17. Backlog item 26, first step. Written while the owner's answer
to the card below is awaited; what was built is the part that the first
two options share.

## What was asked

Item 26: "Apps on their own tasks, one at a time". The aim since the
architecture review is AmigaOS's model: real tasks, one memory space. Up
to now every program's code ran on the UI task, so a program that hangs
freezes the desktop until the watchdog restarts the machine.

Item 37 made this possible: all 17 programs reach the system through
calls and numbers, and the system reaches a program in six places only
(an event for its window, its timer, its menu's action, a message from
another task, the result of its job, its launch).

## The question put to the owner

How does a program paint, and hear of things, when it runs on another
task? The card (2026-11-17, not answered when this was written):

1. **The server waits for the program** (recommended). Each call into
   the program is handed to its task, and the UI task waits until it is
   done, as Win16's `SendMessage` did. One of the two runs at a time:
   windows, timers and the process table still need no locks, and
   nothing else touches the screen. With a limit on the wait, a program
   that does not come back is painted grey, marked as not responding and
   can be ended; the desktop goes on.
2. The program paints when it wants, holding a lock on the screen.
   Faster for a game, but a program that hangs with the lock freezes
   the desktop, which is what the item is there to end.
3. Each window keeps a picture of itself that the program paints into
   and the server copies. Never waits; costs RAM per window and takes
   back the decision that windows keep no picture.
4. Not doing item 26.

**The owner chose the first** (2026-11-17, after this was built). Before
choosing he asked whether, whatever was decided, a full-screen game
could have a way of its own to paint, faster than the window system: he
would like to bring Doom over later. It can, and it does not depend on
this choice: the card is about painting inside a window, where the
screen is shared; a game with the whole screen shares nothing, so the
server can stand aside and hand it the frame buffer (640x480, one byte
per pixel, RRRGGGBB, no palette: Doom's 256 colours would go through a
table, its 320x200 doubled), with the keys sent raw and one combination
kept by the system to come back.

## What was built (the agent's, on the recommended option)

- `core/prog` (`prog::Runner`): a program can be given a task of its
  own. The task sleeps until it is handed a call, makes it as the
  process, says so and sleeps again. When its process ends, the task is
  given back: at once if the program was doing nothing, or when the call
  it was in the middle of has returned (a program that closes its own
  last window).
- `proc::Table::call(pid, fn, context)`: the one way into a program's
  code. The six places above use it. Without tasks in the machine (the
  browser build, the plain PC desktop, most tests) it makes the call
  where it is, as before.
- Calls inside calls: a program that calls into itself does so where it
  is. Into another program with a task, it waits for that one. If that
  one calls back into the first, which is waiting, the call is made
  there (otherwise the two would wait for each other for ever; Win16
  had the same knot and cut it the same way).
- "One at a time": a program says so in the program table
  (`App::own_task`). The first is **Culebra**: a game, with a timer, a
  menu, a settings file, and a message box over it.
- The stack is 8 KB, from internal memory, counted as the program's.
  The agent's guess, to be measured on the board (`dbg.py tasks` shows
  the unused stack).

## What it does not do yet

- **No limit on the wait.** The UI task waits for as long as the
  program takes, so a program that hangs still freezes the desktop.
  This step costs a stack and two task switches per call and gives
  nothing back by itself: it is there to be measured on the board
  before the rest is built on it.
- The limit needs every OS call to be safe from a program that comes
  back late, while the desktop has gone on: a lock taken for the length
  of each call (about 200 functions of `os` and `os::ui`), given by the
  UI task only to the program it is waiting for, or while it sleeps.
  Then the grey window and "not responding" in the Task Manager.
- The other sixteen programs, and init as a task that waits for
  endings (left for this item by item 39).
- Events through a queue (item 37's last part): with a wait on every
  call there is no queue yet. It comes with the limit: what a late
  program has missed must wait for it somewhere.

## Options not taken, in what was built

- Marshalling at each of the forty places where the window manager
  makes a program current: all windows of programs are the system's
  `Behind` now, so one place (`tell`) covers them.
- A queue of events per program from the start: the tests would stop
  being exact (a key sent, the game checked on the next line), and
  nothing can use the queue until the lock above exists.

## Tested

- `test_prog`: the runner alone, with real threads: where each call
  runs, calls inside calls, a process that ends idle and one that ends
  in its own call, no kit left, no memory for the stack.
- `test_snake`: the whole Culebra scenario (keys, the timer, the menu,
  the message box, closing, opening again) is run twice, the second
  time with the program on a thread of its own. Same checks, same
  pictures, the task given back at each close.
- The simulator gives Culebra a task as the board does.
- PC gate passes (42 of 42 tests, the pictures, the OS-call check).
