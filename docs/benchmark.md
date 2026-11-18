# The 60 fps benchmark: how to run it again

Does the desktop keep up with the monitor while windows are dragged, several
are open and a game runs? This is how to repeat the measurement of backlog
item 63, on the board or on the PC. The story of how it came to be is in
[the journal](journal/2026-11-18-frame-budget.md).

## What you need

- The board connected to the laptop (`/dev/ttyACM0`, or set `DBG_PORT`),
  running a firmware that has the frame counter: this branch or any `main`
  after it was merged. `python3 tools/dbg.py frames` answers on such a
  firmware and says "unknown command" on an older one.
- Python with pyserial, which the ESP-IDF environment already has
  (`. ~/esp/esp-idf/export.sh`).
- The games in `C:\Programs` (`culebra.app`, `chess.app`). The system puts
  them there by itself a few seconds after it starts.
- The monitor, if it is to be filmed. Nothing has to be touched while it
  runs: the script moves the mouse and types. Keep the real mouse and
  keyboard still, or the two will fight.

## Run it

From the repository's folder:

```
python3 tools/dbg.py reboot --fast
python3 tools/bench/desktop_frames.py --json frames.json
```

The restart makes every run begin the same way, with no window open. The
script waits for the desktop by itself, so the second command can be typed
as soon as the first returns. A run takes about two minutes.

Useful variations:

```
python3 tools/bench/desktop_frames.py --seconds 10      # each timed part lasts 10 s instead of 6
python3 tools/bench/desktop_frames.py --port /dev/ttyACM1
python3 tools/bench/desktop_frames.py --sim build/host/sim   # no board: the PC simulator
```

What each kind of drawing and each window costs, instead of the nine parts
(the paint bench of backlog item 65; `python3 tools/dbg.py paintbench`
prints one run by hand):

```
python3 tools/bench/desktop_frames.py --paint
```

For the simulator, build it first (`cmake -S host -B build/host && cmake
--build build/host`). It shows that the script works, not how fast the board
is: the PC is far faster, and there Chess thinks inside the click that makes
the move, which the simulator reports as one long piece in every part with
Chess.

## What happens on the screen

First the preparation, which is not measured (about 40 seconds):

1. Start > Run opens, one after the other, the Editor, the File manager, a
   Terminal, Culebra and Chess. Chess asks for a new game; Enter takes
   Expert, where the computer thinks up to ten seconds a move.
2. White's pawn goes from a2 to a3 and the script waits for Black's answer,
   to be sure the engine thinks when asked.

Then nine parts, each about six seconds, one line of results after each:

| Part | On the screen | What it loads |
| --- | --- | --- |
| idle | Nothing moves | Only what the system does by itself: the clock, the caret, its own timers |
| pointer | The pointer goes round, no button held | Mouse events with nothing to paint |
| drag + chess | A pawn moves, then the Editor's outline is dragged round in circles while the hourglass shows over Chess | 125 outline steps a second while the engine thinks on its own task |
| drops + chess | A pawn moves, then the Editor jumps ten times between two places | Each drop paints the window in its new place and what it uncovered |
| drag + culebra | The snake runs in a small loop while its own window's outline is dragged round | A game's timer, its keys and a drag at once |
| culebra | The snake runs in its loop, the pointer goes round over the windows | The game alone |
| to front + chess | A pawn moves, then each of the five windows comes to the front in turn, twice | Whole windows painted one after another while the engine thinks |
| typing + chess | A pawn moves, then "the quick brown fox..." is typed into the Editor, twenty letters a second | Keys, text painting, the engine |
| maximise | The Editor fills the screen and goes back, three times | The largest repaint there is |

A window is not painted while it is dragged: a dotted outline moves, and the
window is painted once where it is dropped. That is why dragging and
dropping are two parts.

## How to read the results

One line for each part (the figures in this example are made up, to show
the shape):

```
drag + chess       6.4 s  frames drawn   742  over budget   0  late timers   0  longest   3.10 ms  desktop busy   8.2 %  missed-refill lines 0
```

- **frames drawn**: how many times the desktop changed the picture.
- **over budget**: pieces of the desktop's work (a key, a mouse event, a
  tick with its painting) that took longer than one video frame. Each one
  is a moment the monitor showed the old picture again. This is the number
  to look at; 0 is a steady desktop.
- **late timers**: timers that fired a frame or more after they were due,
  which is how a game sees a busy desktop. One slow piece usually gives one
  of these too, so the two counts overlap.
- **longest**: the longest piece of work in the part. The budget is 16.80
  ms.
- **desktop busy**: the share of the time the desktop's task was working.
- **missed-refill lines**: new lines in the system log about video lines
  handed over late (`tools/dbg.py misses`). That is a different fault: a
  torn or shifted picture, not a late one.

Under a part that was not steady, one line for each slow piece (the last 16
are kept) says where its time went:

```
    a tick took 84.03 ms: painting 0.00 ms for 0 pixels, timers 84.01 ms; longest timer 84.01 ms, PID 3
```

Read it as: what kind of work it was, how much of it was painting and for
how many pixels, how much was inside timers, and the single longest thing in
it with the PID it belongs to. The script prints each window's PID once, on
the line that starts with `windows:`; `tools/dbg.py tasks` and the Task
Manager name the others (PID 1 is init, 2 the desktop, 3 the taskbar on a
normal start).

Three lines close the run: the rate the monitor is fed at, the totals, and
either `steady: every piece of work fitted in one frame` or `not steady in:`
with the parts that were not. The script exits 1 in the second case.

The budget is 16 800 µs, 59.52 frames a second, not 59.94: the board's pixel
clock is 25 MHz where the VGA standard says 25.175.

`frames.json` holds every figure of every part, to keep or to draw.

## The same figures by hand

The script only reads a counter the system keeps all the time. To watch
anything else (a real mouse, a program of your own):

```
python3 tools/dbg.py frames --reset     # read and start counting again
# ... do something on the board ...
python3 tools/dbg.py frames             # what happened since
python3 tools/dbg.py dmesg              # lines that start with "desktop:" are frames over budget
python3 tools/dbg.py misses             # the video signal itself
```

## If it stops

- `the desktop did not start`: the board does not answer, or its firmware
  has no frame counter.
- `Culebra did not open` or `Chess did not open`: the games are not in
  `C:\Programs` (`python3 tools/dbg.py ls 'C:\Programs'`).
- `Chess did not answer the first move`: Chess's window is laid out
  differently from what the script expects (`CHESS_BOARD` at the top of
  `tools/bench/desktop_frames.py` says where the board is in the window).
- A run stopped half way leaves its windows open and `C:\.config\chess.ini`
  set to Expert; `reboot --fast` and start again.
