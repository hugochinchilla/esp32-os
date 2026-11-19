# The test that failed when the minute changed

2026-11-19. Backlog item 69 (numbered 47 until this morning). A PC test
only: nothing in `core/` or the firmware changed.

## What was asked

The owner, in the project chat, with two other pieces of work:

> actualiza el backlog primero, despues arranca con 51, 56, y el fallo de
> dbg_tool.

The backlog row said: the PC test `dbg_tool` fails now and then when the
tests run in parallel, seen once in the cloud and once on the laptop on
2026-11-17, and it passes alone; find what in it depends on the load. Six
journal entries of 2026-11-17 mention it failing once in a full run and
passing the next time.

## What was found

`dbg_tool` is `tools/test_dbg_sim.py`: it starts the simulated board
(`build/host/sim`) and drives it with `tools/dbg.py` as the board is driven.
Alone it takes 12.8 s here.

It did not fail in 24 runs, six at once, with 16 busy loops on the 4
processors. With 32 runs, sixteen at once, and 48 busy loops, 16 of 32
failed, all 16 of the second batch and none of the first. That was the
clue: runs that started together failed together, so it was something
about the moment, not about how slow each run was.

The failures were two checks of the same kind, both in the part added for
the second screen size (item 68):

- 11 runs: `nothing stale after the change` (a screenshot at 800x600,
  a full repaint, a second screenshot: the two must be equal).
- 5 runs: `and back, the picture is what it was` (a screenshot before the
  change to 800x600 and one after coming back to 640x480).

A print of where the two pictures differed gave the same answer in all
five: 22 pixels, between (605, 462) and (609, 469). That is the last digit
of the taskbar's clock. One picture said 1:23 AM and the other 1:24 AM.

So the test compares two screenshots taken at different moments, and the
clock is in both. The simulator's clock is the PC's own (the pretend Wi-Fi
sets it from this computer's time, as the real one sets it from the
network), so the minute changes once every 60 s whatever the test is
doing. A run fails when a minute's end falls between a picture and the one
it is compared with. Alone those gaps are short, a second or two each, so
it fails perhaps once in twenty or thirty runs; beside other tests every
step takes longer, the gaps grow, and it fails more often. The load made
it likely; it was never the cause. That is also why it "passed alone" on
the next try: the next try was at another second of the minute.

Checked apart: an idle simulated desktop, one screenshot before the end of
a minute and one 6 s after it, differ in those same pixels and nowhere
else.

## The options considered

1. **Leave the clock out of the comparison.** The test blanks the clock's
   rectangle in both pictures before comparing. Costs a rectangle written
   in the test that must follow `Taskbar::clock_rect`.
2. **A debug command that sets the board's clock**, so the test can put it
   at the start of a minute before each pair. A new command in the
   protocol and in the firmware for the sake of one test, and a minute
   still ends if a slow run takes more than 60 s between the two pictures.
3. **A simulator whose clock stands still.** The window manager's clock
   runs on the same time source as its timers, so it cannot be stopped
   alone without a switch in `core/` that exists only for tests.
4. **Take the pair again when it differs.** Hides a real stale picture one
   time in two, and the pair around the change of screen size cannot be
   taken again: the first picture is from before.

## What was chosen, by whom, and why

Option 1, chosen by the agent: it is the only one that cannot fail however
long a run takes, and it changes nothing outside the test. What the test
is checking in those places (nothing stale after a drag, a repaint, a
crash screen, a change of size) does not depend on the clock. The price is
that this test would no longer notice a clock painted wrongly; it never
checked that on purpose.

## What was built

`tools/test_dbg_sim.py`: `steady(picture, width, height)` blanks the 60 by
22 pixels of the clock (2 pixels in from the right edge, from 24 above the
bottom), and `shot(board)` is a screenshot without the pointer passed
through it. All nine comparisons of one picture with another go through
them, the earlier ones too (the drag, the paint bench, the crash screen),
which could fail the same way and by luck had not been seen to.

Tested on the PC only, in the cloud container:

- The same load that failed 16 of 32: 48 runs, sixteen at once, 48 busy
  loops, 48 passed.
- The whole suite, `ctest -j16`, three times: 54 of 54 each time.

Nothing here runs on the board.

## What was learned

- "Fails under load" described when it was seen, not why. The first
  suspects were the time limits (a ping under 1 s, replies within 2 s,
  commands sent again after no answer); none of them failed even with 48
  busy loops on 4 processors.
- A batch of runs that fail together is worth more than the count: it
  pointed at the wall clock at once.

## What is left

- `vnc_tool` (`tools/test_vnc_sim.py`) compares the viewer's picture with
  screenshots and, in one place, expects no update at all while "nothing
  changed". Twelve runs at once with 32 busy loops all failed there
  (`an update came with nothing changed`). The clock is the first suspect
  but it was not looked into, and twelve simulators at once may be the
  cause by themselves; it passed in the three full runs of the suite. Not
  in the backlog yet.
