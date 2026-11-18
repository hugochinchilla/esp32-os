# The chess window, and a background job a program can start

2026-11-18. Backlog item 50, its second step, and a new OS call. Follows
[the design](2026-11-18-chess-design.md), [the engine](2026-11-18-chess-engine.md)
and [the levels](2026-11-18-chess-levels.md). Built and tested on the PC;
the firmware and the program file were compiled and linked in the cloud.
Nothing here has run on the board.

## What was asked

The owner approved the design ("perfect, go ahead") and set the five levels.
The coordinator's note for this piece of work:

> Build backlog item 50 step two: the chess game window as a loadable
> program (like Culebra and Cactus Patch), on top of the engine [...] The
> engine thread found that programs in files can't start a background job
> (job::jobs() isn't exported and there is no OS call), and that searching
> inside the program's own event drops it to priority 0 (14.5k instead of
> 35k positions per second). So add an OS call for a background job (8 KB
> of stack is plenty) as part of this work.

## What was found

- A program's object is destroyed the moment its window closes
  (`app::Window::events`, on Closed), whether a job of its process is
  running or not. What the window manager holds back for a process with a
  job is the window's memory and the process's end, not the destructor. The
  Editor gets away with it because its window is locked while it saves and
  cannot close. A chess window must be closable while the engine thinks, so
  whatever the job reads cannot live in the window.
- `os::now_us()` is the UI task's: `wm::Wm::now_us()` keeps the last value
  it read to stretch a 32-bit counter to 64, with no lock. A job cannot ask
  it the time.
- A task that computes for ten seconds at any priority above the kernel's
  idle tasks keeps the idle task of its processor from running. ESP-IDF's
  watchdog on the idle tasks prints a warning after five. The engine's
  bench never met it: after two seconds it had been put at the idle
  priority as "not responding".
- A picture that is not made with `kOnce` is painted through the system's
  strip of twelve lines (`docs/journal/2026-11-16-painting-once.md`), so
  the program may paint a square, then the piece, then a dot on top, and
  the screen still gets each pixel once.
- The Start menu's four reference pictures change: one more program.
- In the PC's checked build the chess program in its file form came to
  1.65 MB, over the megabyte a program file may be.

## The options, and what was chosen

Choices marked **to confirm** were made here, on the recommended option,
because the work would have stopped without them. They are the owner's to
change.

### Where the engine thinks

1. **A background job, started by a new OS call** (chosen; asked for by the
   coordinator from the engine's findings). `os::start_job(spec)` is
   `job::jobs().start(spec)` behind the door programs use; the Editor's
   save now goes through it too, and `tools/check_os_calls.py` refuses
   `job::jobs()` in a program.
2. Inside the program's own event. What the bench did: the desktop calls
   the program "not responding" after two seconds and puts it at the idle
   priority, and the window cannot be moved or closed meanwhile.
3. In slices, on a timer. The engine would have to be stopped and resumed
   in the middle of a search; it is not written that way.

### What the job works with

1. **A block of its own, the "Brain"** (chosen): the engine (13 KB), a copy
   of the position, the limits, the result and three flags, asked of the
   system when the computer first has to move. If the window closes while
   the job runs, the destructor marks the block an orphan and tells the
   engine to stop; the job's `done` gives the block back. The table of
   positions (256 KB of external RAM) hangs from it.
2. The window waits in its destructor for the job. It would hold the UI
   task, or the program's, for up to ten seconds.
3. The system keeps the program's object alive while a job runs. A change
   to every program's life for one program's sake.

### How the job learns that its time is up

1. **A timer of the window's sets a flag the job reads** (chosen). The
   engine is given no clock at all; its poll hook, called every 256
   positions, answers "stop" when the flag is up, and the engine returns
   the best move of the deepest look it finished.
2. A clock call for jobs. Needs `Wm::now_us` made safe for two tasks, or a
   second clock; more than this piece of work should change.
3. Count positions instead of time. The count that makes ten seconds on the
   board is a second on the PC.

### How the job gives way (**to confirm**)

Priority Low: below the desktop, the programs' own tasks and the services,
so it takes the processor only when nothing else wants it. And a rest: a
second call, `os::job_rest(ms)`, puts the job's task to sleep for a tick
(`ports::Tasks::rest`, `vTaskDelay` on the board). The engine's poll hook
rests once every 32 polls, about four times a second at the board's speed,
which costs under 5% and lets the idle tasks run. Not chosen: no rest
(the watchdog's warning at level 5), and the idle priority (the search
would stop whenever anything at all runs, and level 5 would lose most of
its ten seconds, as the bench did).

### A machine without tasks

On the PC's plain desktop and in the browser there are no tasks:
`start_job` does the work before it returns, and the window waits. A job
can now ask (`job::Job::own_task()`), and the chess job then stops after
200,000 positions whatever the level: about a second on a PC. Level 5 is
therefore weaker there than on the board. The alternative was a window
frozen for ten seconds.

### Painting

Through the system's strip, not `kOnce`: a piece dragged over other squares
and their pieces is then simply painted last. Only what changed is asked to
be painted: the window keeps one byte for what each square shows (piece,
yellow, dot, lifted) and one number for the two rows of captured pieces. A
move paints its squares and, if something was taken, the rows.

The pieces are kept two bits a pixel, 9 KB for the three sets at both
sizes (`core/src/shell/chess_pieces.hpp`, written by
`tools/mockup/chess_pieces.py packed`), and coloured into a scratch square
when drawn. The program's icon is the Contour knight drawn at 16 and 32 by
the same script.

### Things the design did not say (**to confirm**)

- **A pawn on the last rank** opens a small dialog with four buttons:
  Queen, Rook, Bishop, Knight. Closed without a choice, the move is not
  made. Not always a queen (the rules allow four pieces and the engine
  uses them), and not a menu at the square (a program cannot place a menu
  by screen position without knowing where its window is).
- **A piece that can be taken** is marked by the four corners of its
  square, not by the dot: the dot sat on the piece and hid in it.
- **In a theme with a dark face** (midnight), Black's pieces off the board
  (the captured ones, and those in the dialogs) get their outline in the
  theme's text colour. Black on near-black, they could not be seen. On the
  board nothing changes: the board keeps its colours in every theme.
- **The dialog's five lines** say what the levels now are: 1 to 3 "play at
  once" (with mistakes, fewer mistakes, looks further), 4 "thinks up to
  three seconds", 5 "up to ten seconds". The mock-up's lines were guesses
  made before the board's figures.
- **Take back** undoes the user's last move and the computer's answer; it
  works while the computer thinks (the answer on its way is thrown away)
  and after the end of a game, and it also takes a resignation back.
  **Resign** does not ask first, since Take back undoes it.
- **Help > About Chess** is a message box of three lines, as the mock-up's
  menu had it, not the system's About window.
- **The settings** are written when they are chosen, not when the window
  closes: `chess.ini` has `[game] level` (1 to 5) and `side` (white, black,
  random), `[look] board` (green, brown), `pieces` (contour, symbols,
  classic) and `dots` (true, false).
- **Stacks**: 6 KB for the program's task (the rules list a position's
  moves on the stack, half a kilobyte, twice over), 8 KB for the job (the
  search took 2.3 KB on the board).

## What was built

- `os::start_job` and `os::job_rest` (`core/os/os.hpp`, `docs/os-calls.md`);
  `job::Job::own_task()`, `job::Jobs::rest()`; `ports::Tasks::rest()` with
  its adapters for FreeRTOS and for the PC's threads. The Editor's save
  uses `os::start_job`.
- `core/src/shell/chess.cpp`: the board window, the "New game" dialog and
  the promotion dialog, as one program. `programs/chess/chess.cpp` builds
  it with the rules and the engine into `chess.app` (50,688 bytes with the
  ESP32-S3 compiler); on the PC it is also built into the system, as the
  other two games are.
- The window: squares of 40 pixels with letters and numbers inside, the
  user's side at the bottom, the last move and the piece picked in yellow,
  dots where it may go (Options), moves by a drag or by two clicks, the two
  rows of captured pieces with the lead in pawns, a status line, the
  hourglass over the window while the computer thinks, F2 and Ctrl+Z.
- `host/tests/test_chess_window.cpp` and eleven reference pictures
  (`host/tests/pictures/55-chess-*` to `59e-*`), with the window in each of
  the four themes.

## How it was tested

On the PC, the whole suite (47 tests).

- `chess_window`, first with no tasks: the dialog at opening, Cancel and
  Escape, a move by two clicks and by a drag, a tremble that is not a drag,
  a piece dropped where it cannot go, the computer's pieces not to be
  picked, take back by key and by menu, each screen pixel painted once on a
  full repaint and nothing stale after any step, the rows of captured
  pieces, both ways of promoting, mate by and against the computer,
  resigning, playing Black on the turned board, every option kept in
  `chess.ini`, every theme, and all memory back after closing.
- Then with the job on a real thread held back by the test: the window
  answers and shows the hourglass while the computer "thinks", a take back
  and a new game throw the answer on its way away, level 5 goes on until
  the window's timer says ten seconds have passed, and the window closed
  while the job runs leaves the process alive until the job returns and
  then gives every byte back. The same test passes under the thread
  checker (`-DHOST_TSAN=ON`).
- `job`: the two new calls, with and without tasks. `loaded`: `chess.app`
  in its file form is read, loaded, started and unloaded. `lifecycle`: the
  program leaves nothing behind.
- In the cloud, with ESP-IDF v5.5: the firmware compiled and linked, the
  four program files were built, and every name they ask the system for is
  in the firmware's table of exports (328 names, none missing).

## What was not tested

Nothing ran on the board. To be seen there: that the program loads and
plays; positions a second with the job at Low and its rests (the bench had
35,000 at the program's own priority); that level 5 answers in ten seconds
and level 4 within three; no missed refill and no watchdog line while it
thinks; the stack the program's task and the job really use; memory back
after closing, also when closed while thinking.

## What was learned

- The PC's checked build of the chess file is left unchecked and
  optimised (`-O2 -fno-sanitize=all` for that one target): with the
  checkers in it the file was larger than a program may be. The same
  source is checked where it is built into the system.
- `test_loaded` lost its output when a failed check was followed by a
  crash, because the output was still in a buffer. It writes unbuffered
  now.
- `test_job` reports a race under the thread checker in
  `host::ThreadEvent`'s destructor, with or without this change. Not
  chased here.

## What is left

- The board check, and the owner's word on the choices marked above.
- Not built, and not in the approved design: a list of the moves played,
  a clock, saving a game, sounds.
