# Does the desktop keep up with the monitor?

2026-11-18. Backlog item 63. Built and tested on the PC; the firmware
compiles and links in the cloud (ESP-IDF v5.5). Not yet run on the board:
the figures that answer the owner's question come from the board turn.

## What was asked

The owner, in the project chat:

> Me gustaría que hagas un benchmark del sistema operativo y te asegures de
> que corre a 60 frames por segundo estable, 59,9 exactamente lo que VGA nos
> permite, mientras se arrastra una ventana, habiendo varias ventanas
> abiertas, mientras algún juego funciona. Supongo que lo que podemos hacer
> es detectar en cada ciclo de pintado si hemos excedido el presupuesto de
> tiempo para generar el frame y loguearlo en un log, que puede ser el mismo
> del de MSG o un log específico del escritorio o proceso gráfico. Y
> entonces simplemente hacemos ciertas acciones y después comprobamos en los
> logs si se han dropeado frames o no. Creo que eso es más fácil que testear
> sacando con el vídeo de la webcam, o muchísimo más fácil que vía un
> dumpeado por screenshots, ya que toman su tiempo.

## What was found

- **The monitor is fed at 59.52 frames a second, not 59.94.** The pixel
  clock is 25 MHz (`PIXEL_CLOCK_HZ` in `firmware/os/main/vga.c`), not the
  standard's 25.175 MHz, and a frame is 800 x 525 pixels with the blanking:
  16 800 µs. That is the "59.5 frames a second" the bench notes in
  `DECISIONS.md` have always shown. The budget is therefore 16 800 µs, taken
  from the pixel clock, not a constant typed in.
- **There are two different things that can go wrong, and only one was
  watched.** The video interrupt hands the monitor ten lines at a time from
  the framebuffer; when it is late, lines are missed and the picture tears
  or shifts. That is counted already (`sys::MissedRefills`, `dbg.py misses`,
  backlog item 38). What nobody counted is the other one: the desktop taking
  longer than a frame to do a piece of work, so that the monitor shows the
  old picture once more. The picture is perfectly stable then; it is just
  late.
- **There is no "paint cycle" tied to the monitor.** The desktop paints
  straight into the one framebuffer, when there is something to paint: after
  each key, each mouse event, each typed text, and on each tick (the timers
  that are due, then the painting). `Wm::update()` already timed itself
  (`last_update_us`, shown by `dbg.py windows`), but only the last one, and
  nothing compared it with anything.
- **A window is not painted while it is dragged.** A drag moves a dotted
  outline (inverting pixels); the window is painted once, where it is
  dropped. So "dragging a window" is two loads: many cheap outline steps,
  and one drop.
- Earlier bench notes: a full repaint 22 to 29 ms in the first milestones
  (over a frame), about 13.9 ms on `main` now; bringing a window to the
  front 16 ms; maximising 28 ms. Those are the candidates.
- A game's step is a timer of its window (Culebra: every 80 to 200 ms), run
  from the tick; Chess thinks in a job on a task of its own at low priority,
  and its answer comes back as a message to the desktop's task.

## The options

What to measure against the budget:

1. **Each `update()` only** (the painting). Simple, but a key that makes a
   program work for 30 ms and then paint for 2 would pass.
2. **Each piece of the desktop's work, painting included**: a key, a mouse
   event, typed text, a tick, the messages from other tasks. While one runs
   nothing else reaches the screen, so this is what the user sees late.
3. **The video frame counter**: note the frame number when a piece starts
   and ends, and count the frames in between. Exact about which frames were
   skipped, but it needs the firmware's counter in the core and says nothing
   more than option 2 does about why.

Where the result goes:

- a. A line in the system log for every slow piece. Easy to read, but a slow
  desktop would push everything else out of the 16 KB log, and a benchmark
  wants totals, not text to count.
- b. Counters only, read by the debug link.
- c. Both: counters and the last slow pieces for the debug link, and a
  rate-limited line in the log (one at once, then at most one every ten
  seconds with what came meanwhile), exactly as the missed refills do.

## What was chosen

Option 2 with c, by the agent, following the owner's outline (detect it in
each cycle, log it, run actions, read it back); to confirm. Option 1 would
miss time spent outside the painting; option 3 can be added later if the
exact frames matter. A log of its own was not made: the system log is the
one `dmesg` already reads, and the line starts with `desktop:`.

A timer that fires a frame or more late is counted as well. It is what a
busy desktop looks like from a game, and it also catches the desktop's task
not being given the processor, which timing its own work cannot see.

For each slow piece the desktop keeps where the time went: how much was
painting and for how many pixels, how much was inside timer callbacks, and
the longest single thing in it (a timer callback, a window's painting or a
panel's) with the PID it belongs to. The last 16 are kept.

## What was built

- `core/sys/frames.hpp`, `sys::Frames`: `begin`/`end` around a piece of work,
  `painted`, `ran` (a timer callback or a window's painting, with its PID),
  `late`; totals, eight counts by length, the last 16 slow pieces, and the
  log line. Pieces nest (a message that carries a mouse event is one piece,
  named by what it carried).
- `wm::Wm`: a `Work` guard in `key`, `mouse`, `text` and `tick`; `update()`
  and the drag outline report what they painted; `paint_window` and the two
  panels report their time and owner; the timer loop reports each callback
  and how late it fired. The guard writes the log line. `Wm::frames()`.
- The loops (`firmware/os/main/main.cpp`, `host/sim.cpp`) put a `Work`
  around the delivery of messages. The firmware sets the budget from the
  pixel clock.
- The debug link: request 0x17, Frames (`docs/debug-protocol.md`), with a
  flag that starts the count again. `tools/dbg.py frames [--reset] [--json]`
  prints it and exits 1 if any piece was over budget.
- `tools/bench/desktop_frames.py`: opens the Editor, the File manager, a
  Terminal, Culebra and Chess (Expert, up to ten seconds a move, set in
  `chess.ini`), checks that Chess answers a move, then measures nine parts
  one after the other: idle; the pointer going round; the Editor dragged
  while Chess thinks; ten drops of the Editor while Chess thinks; Culebra's
  own window dragged while the snake is steered; Culebra played with the
  pointer going round; every window clicked to the front in turn while
  Chess thinks; typing into the Editor while Chess thinks; maximising the
  Editor and back. Mouse events are sent 125 times a second, as a USB mouse
  does. For each part: frames drawn, frames over budget, late timers, the
  longest piece, the desktop's share of the processor, and new
  missed-refill lines; at the end the rate the monitor was fed at, from the
  board's own count of frames shown. No screenshot is taken while a part is
  measured (two are taken before, to see that Chess answers).

Tested on the PC: `host/tests/test_frames.cpp` (what counts, what is kept,
the text of the lines, a storm gives one line in ten seconds, the clock
wrapping); `tools/test_dbg_sim.py` reads the request back through `dbg.py`;
the benchmark runs to the end against the simulator. All 51 PC tests pass.

What the simulator's run does not say: the PC is far faster than the board,
and it has no task for Chess's job, so the engine thinks inside the click
that made the move (a 200 to 350 ms piece, reported as over budget in every
part with Chess). On the board the job has its own task and that piece
should not be there.

## What was learned

- The refresh rate is 59.52 Hz. "60 frames a second" on this board means
  16.8 ms, a little more room than the standard's 16.68.
- A drag was invisible to the first version of the count: the outline does
  not go through `update()`. "Frames drawn" said 10 for four seconds of
  dragging until the outline reported itself.
- A slow piece is followed by a late timer (the timer was due while the
  piece ran), so the two counts overlap: one cause can show in both.

## What is left

- The board turn: the benchmark's figures on the real machine, with
  `dbg.py misses`. If frames are over budget there, the slow pieces say
  where the time went; what to do about it is a separate piece of work for
  the owner to decide.
- The debug link's own work (a screenshot takes 0.3 s on the desktop's
  task) is not counted as a piece: it would only measure the measuring.

## The first board turn (2026-11-18, commit 6f101d2 with main merged in)

The standard checks passed and `dbg.py frames` answered. The benchmark did
not run, for two reasons that are the script's, both found by the board
thread:

- The board starts with no window open (the owner's choice), and the
  script waited for one, as `dbg.wait_for_desktop` does. It now waits for
  the desktop's loop instead: a mouse event that is counted as a piece of
  work.
- On the board the games are files in `C:\Programs` (backlog item 27), and
  Start > Run does not know "Culebra" or "Chess" by name there. The script
  now gives Run the file (`C:\Programs\culebra.app`) when the name is
  refused, and waits for the program's own window instead of counting
  windows.

Neither showed on the PC, where the simulator opens two demo windows and
has the games built in.

What `dbg.py frames` said on the idle desktop was a finding of its own,
before any benchmark: 23 pieces over budget out of 574, every one a tick of
about 84 ms, all of it in a timer of PID 3, the taskbar, coming round every
two seconds; each followed by a timer of the desktop's 36.7 ms late. That
timer is `desktop_files_tick` in `core/src/shell/desktop.cpp`: every two
seconds it lists `C:\Documents\Desktop` again to see whether the icons
changed, on the desktop's task, and on the board's flash drive that takes
five video frames. So an idle desktop already holds everything still for
84 ms every two seconds; a drag would stutter there, and a game's step
would come late. `dbg.py misses` stayed at 0: the picture itself is not
disturbed, it is only late. What to do about it (list on a job's task,
list only when something says the folder changed, list less often) is the
owner's to choose and a separate piece of work.
