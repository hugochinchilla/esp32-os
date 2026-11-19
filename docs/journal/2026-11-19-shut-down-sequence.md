# The Shut Down sequence

2026-11-19. Backlog item 60. Built and tested on the PC. The firmware was
compiled and linked in the cloud container (ESP-IDF v5.5); nothing here
has run on the board.

## 1. What was asked

The owner, on 2026-11-18, for the backlog:

> on shutdown, in order, close all apps (let them wait, force kill after
> timeout of 20 seconds), fade desktop to background color (icons and
> taskbar disappear), then the "safe to turn down" shows, replace that
> screen background color with full black

and on 2026-11-19, to start it:

> vamos con 60

## 2. What was found

- Shut Down was one function, `shell::power_down()` in
  `core/src/shell/power.cpp`, that did everything before it returned. It
  called `close_requested()` on every window with a taskbar button. If one
  had a dialog open, or the Editor asked "Save changes?", it returned
  false and that was the end of it: after answering, Shut Down had to be
  chosen again. If everything closed, `Wm::take_over()` handed the screen
  to the last screen at once, and the closing melody (item 59) started.
- The last screen was drawn on `rgb(0, 0, 1)`, the darkest blue the board
  has, as a night sky.
- There was no notion of time in it: no waiting, no limit, no fade.
- The picture has 256 fixed colours, 3 bits of red, 3 of green and 2 of
  blue, in one copy in memory, with no palette to turn down and no second
  picture to mix from. A fade has to be made of the pixels themselves.
- `Wm::update()` did not tell the remote screen about what a taken-over
  screen painted, so a VNC viewer kept the desktop's picture after Shut
  Down until something else made it ask again.

## 3. The options considered

**Who waits.** The old function ran inside whoever called it (the Shut Down
dialog's own program). A sequence that lasts 20 seconds cannot: it is now
a small state machine on a timer of the desktop's, and `power_down()` only
starts it.

**What the 20 seconds cover.** (a) Everything: 20 seconds from the moment
Shut Down is chosen, and whatever is still open then is ended, a question
nobody answered included. (b) Only programs that do not answer; a question
waits for ever, as on Windows 98. The owner's sentence has one limit and
says "let them wait", so (a) was built. It costs unsaved text if nobody is
there to answer; (b) costs a machine that never shuts down by itself.

**Cancel.** A program that is asked and stays, with no question open, has
said no. (a) That calls Shut Down off, as on 98. (b) It is ended at 20
seconds all the same. (a) was built: with (b) the Cancel button of "Save
changes?" would mean "lose it in a few seconds".

**How to end a program.** `Wm::end()` at once, or the KILL signal that End
Task sends. KILL waits for a program's running jobs, so a file being saved
is finished; that was taken.

**The fade.** Three ways were looked at:

- A dissolve: more and more pixels, picked by a dither pattern, are
  replaced by the target. The cheapest, 1/16 of the screen per step. It
  reads as a wipe through a screen door, not as a fade.
- Mixing each pixel between the picture as it was and the target. Needs
  the picture as it was, a second 307 KB (480 KB at 800 x 600), and still
  lands on 8 levels per channel.
- Stepping in place: each pixel moves one level of each channel towards
  its target per step. With 8 levels of red and green that is 7 steps at
  the most, and the picture needs no copy. Blue has 4 levels, so it moves
  on every second step and arrives with the others. This is what a VGA
  palette fade looked like, and it was built.

**What "the background colour" is.** The desktop may have a pattern and a
colour the board can only dither, so the target is not one colour: it is
whatever the plain desktop shows at that pixel. The pattern is 8 by 8 and
the dither 4 by 4, both laid from the screen's corner, so one 8 by 8 tile
painted by the same function that paints the desktop says what belongs
anywhere; 64 bytes instead of a second picture.

**From the desktop's colour to the black screen.** A cut, or a second fade.
A second fade was built: the plain desktop rests for 300 ms so that it is
seen, fades to black in the same steps, and the last screen comes up out
of the black in seven levels of brightness (it is repainted, each colour
scaled).

**What the last screen keeps.** Everything: stars, the mascot asleep, both
lines. Only the sky's blue became black.

**The melody.** With the last screen, as before, or with the start of the
fade. The fades take 1.56 s and the melody 2.93 s, so starting it with the
fade puts the picture and the sound together; that was built.

None of these were put to the owner before building: he was away, and the
thread was told to follow the backlog's description. They are recorded as
`to-confirm` in `docs/decisions/shut-down-sequence.md` and listed for him
with a recording of the fade (project files, `look/shut-down/`).

## 4. What was built

- `core/src/shell/power.cpp`: the stages Closing, Desktop, Black, Rising
  and Down. Closing looks at the programs every 100 ms: one that can be
  asked is asked (oldest first, brought to the front first so that its
  question is its own window's); one with a dialog open or a save running
  is waited for; one that was asked, has no question open and is still
  there calls the whole thing off. At 20 seconds (`shell::kCloseWaitUs`)
  every window's program is sent KILL; a window nobody may signal is
  closed. Two seconds later the fade begins whatever is left.
  `shell::shutting_down()` is new; `powered_down()` is true only once the
  last screen is whole.
- `core/wm`: `Wm::hold_screen()` (the first half of `take_over()`: the
  screen is taken, and what it shows is kept) and `Wm::fade_step()` (one
  step of every pixel towards the plain desktop or towards one colour,
  telling the remote screen which lines changed). `update()` now tells the
  remote screen what a taken-over screen painted.
- The PC program (`host/desktop/main.cpp`): closing its window starts the
  same sequence and it leaves when the last screen shows and the melody
  has ended; closing the window again leaves at once. The "close again
  within three seconds" rule is gone with the refusal it answered.
- `os::shut_down()` now means "start it"; `docs/os-calls.md` says so.

## 5. How it was tested

On the PC: 54 of 54 tests, and the gate (`tools/gate.sh`).

- `test_system`: the question makes Shut Down wait; Cancel calls it off and
  nothing is ended later; No lets it go on by itself; then the fade step by
  step (the taskbar's grey gone after one step, the plain desktop within
  seven, a rest, all black, the last screen never darker from one step to
  the next), keys and Ctrl+Alt+Del ignored meanwhile, full black in the
  corners and no pixel of the old blue.
- `test_proc`: with a question nobody answers, nothing happens for 19
  seconds; at 20 the Editor's process is gone, its text not saved, and the
  fade begins.
- `test_sound`: the melody is asked for once, when the fade begins.
- Pictures `38a` to `38d` and `38-power-down`.

Not tested: the firmware on the board (it compiles and links). One step of the fade reads and
writes the whole picture (307 200 pixels, 480 000 at 800 x 600) on the
desktop's task; how long that takes on the board, and whether a step every
60 ms is kept, is for the board turn to measure. The PC program was not
compiled here (the container has no SDL).

## 6. What was learned

- Moving one level per step in place needs no memory of where a pixel
  started, at the price that pixels near their target arrive early: the
  icons' dark outlines join the desktop two steps before the taskbar's
  white edges. On the screen it looks like a fade all the same.
- On the default desktop the fade to the desktop takes five steps, not
  seven: nothing on it is seven levels from Steel blue.
- The taskbar's grey turns faintly violet for a step on its way to blue:
  with four levels of blue to eight of the others, "one level each" cannot
  keep a grey grey.

## 7. What is left

- The owner's word on the choices of section 3.
- The board check, and the step time measured there.
- Restart still asks nobody and closes nothing; not part of this item.
