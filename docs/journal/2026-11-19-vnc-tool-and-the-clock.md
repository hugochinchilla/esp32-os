# The remote screen was sent the clock again, one second after the start

2026-11-19. Backlog item 73. A PC test, and six lines of `core/src/wm/wm.cpp`
that the test led to. Not run on the board, and the firmware was not compiled
here.

## What was asked

The owner, in the project chat, with two other pieces of work:

> vamos con 60, y los refrescos a 800x600, vnc_tool tambien

`vnc_tool` is the PC test `tools/test_vnc_sim.py`: a small VNC viewer written
in Python against the simulated board, whose picture must equal the
screenshot from the debug link. The entry on `dbg_tool`
([the test that failed when the minute changed](2026-11-19-dbg-tool-and-the-clock.md))
left it as the next one to look at: twelve runs at once with 32 busy loops
all failed at `an update came with nothing changed`, and the clock was the
first suspect. The coordinator added that `serial_console` had failed once
in a parallel run and passed alone.

## What was found

Twelve runs at once with 32 busy loops on the 4 processors: 12 of 12 failed,
all at the same check. That check is the test's second step: the viewer has
the whole picture, asks for what changes from now on, and nothing must come
for a third of a second.

A print of the update that came gave the same answer in all twelve: one
rectangle, 64 by 32 at (576, 448), and not one pixel in it different from
what the viewer had. That corner is the taskbar's clock. It was painted
again, and it said what it said before.

It was not the minute. The simulator in this test has no network, so its
clock starts at 0:00 when the desktop does, and the update came about one
second later, at 0:00:01. The cause is in the window manager:
`Wm::clock_tick` runs once a second and tells the taskbar when the minute it
last announced differs from the minute now, and the minute it last announced
began as -1, "none". So the first tick, one second after `Wm::start`, always
announced the clock, and the taskbar painted 60 by 22 pixels that were
already right. The same happened after `Wm::set_time`, which announces the
clock itself and left the tick to announce it a second time: twice for the
network time when it first arrives, and for each hourly correction.

On the board this is one small repaint too many at the start and one an
hour, which nobody sees. With a viewer connected it is an update sent for
nothing. In the test it is a failure when that first second ends between
the viewer's first picture and the quiet third of a second after it. Alone,
the test gets there about 0.15 s after the desktop appears and has stopped
listening by 0.5 s; the tick comes at 1.0 s and goes out with the next
step's update, unnoticed. Beside other tests the viewer's first picture
alone takes more than a second to decode in Python, the tick falls before
the quiet check, and the answer to the viewer's request is waiting when the
check starts. That is why it failed every time under load and never alone.

With that fixed, 8 of 12 runs under the same load failed further on, at the
third step: the viewer clicks OK in the About window, reads one update and
compares its picture with a screenshot. The pictures differed in some
52,000 pixels, the area of the two windows. The server sends an update as
soon as something has changed, and under load that was the button pressed
in, before the window was closed and what was under it painted. The fourth
step already knew this ("the first update can come before the key's
painting is finished") and asks again until the viewer has caught up; the
third did not.

Then the cause `dbg_tool` had. This test also compares pictures taken at
different moments: the screenshot taken after the photograph is compared
with the full picture of six viewers that connect after it, one after
another, and with two more through the WebSocket. Alone, the whole test is
some 16 s of the simulator's clock, so a minute never ends in it. Under
load a run has taken 46 s; past 60 s the clock would say 0:01 in a viewer's
picture and 0:00 in the screenshot. It was not seen to fail this way, so it
was provoked: a simulator built with a clock sixty times too fast, a minute
every second, failed the old test 3 times of 3 at those comparisons.

So three things, two of them the clock:

1. the clock announced one second after the start with nothing new to show
   (what failed 12 of 12);
2. one update read after a click and taken for the whole change (what
   failed 8 of 12 once the first was out of the way);
3. a minute ending between a screenshot and the picture it is compared
   with (not seen, provoked).

`serial_console` (`tools/test_serial_console.py`) has no picture in it: it
takes one screenshot and checks only its size, and everything else is text
from the shell and whether a port answers. The clock cannot fail it. It did
not fail here in 80 runs (16 at once with 48 busy loops, then 32 at once
with 64, twice), nor in four runs of the whole suite sixteen at once
with 16 busy loops. Whatever failed it once is something else and was not
found.

## The options considered

For the clock announced at the start:

1. **Fix it where it is**: `Wm::start` notes the minute the clock shows
   before the first tick, and `set_time` notes the one it has just
   announced. Six lines in `core/`, so it reaches the firmware.
2. **Leave `core/` alone and have the test wait** more than a second after
   the desktop appears before it connects. Nothing for the board to check,
   but the update for nothing stays, and a test that sleeps to step around
   something it found is the kind that fails again later.

For the minute that ends during the test:

1. **Leave the clock out of the comparisons**, as `dbg_tool` does, and let
   the quiet check accept an update that changes the clock and nothing
   else.
2. **Set the board's clock from the test** (`dbg.py`'s time request) to the
   start of a minute. A minute still ends 60 s later, and under enough
   load the test is longer than that.

## What was chosen, by whom, and why

By the agent, all three; none changes what the user sees.

- The first cause is fixed in `core/` (option 1). It is a fault, small as
  it is, and the test was right to catch it: "nothing changed, so nothing
  is sent" is what the check is for. Waiting in the test would have kept
  the check and thrown away what it found.
- The third step asks again until the viewer has caught up, as the fourth
  does, and then still checks that fewer rectangles came than for a whole
  screen.
- The comparisons leave the clock out (option 1), for the reason given in
  the `dbg_tool` entry: it is the only way that holds however long a run
  takes. The price is the same: this test would not notice a clock sent
  wrongly to a viewer. The quiet check is kept strict otherwise: an update
  there must change the picture, and only the clock.

## What was built

- `core/src/wm/wm.cpp`: `Wm::start` sets `shown_minute_` from the time
  before it starts the one-second timer; `Wm::set_time` sets it to the
  minute it announces.
- `host/tests/test_wm.cpp`, in `test_panels`: after `set_time`, 59 s pass
  with no news for the bar and the sixtieth brings one; a window manager
  just started says nothing about the clock for 59 s and then once. With
  the fix taken out, four of these checks fail.
- `tools/test_vnc_sim.py`: `steady` blanks the clock's 60 by 22 pixels in a
  screenshot or in a viewer's pixels, `shot_of(board)` is a screenshot
  through it, and the viewer has `steady()` and `picture()`. Every
  comparison of a viewer's picture with a screenshot goes through them. The quiet check reads an update if one
  comes and fails unless it changed the clock and only the clock. The
  third step uses `caught_up`.

Tested on the PC only, in the cloud container:

- The load that failed 12 of 12 (twelve runs at once, 32 busy loops): 12 of
  12 pass.
- With the simulator's clock sixty times too fast (a change made for the
  check and taken out again): the old test fails 3 of 3, the new one passes
  6 of 6.
- The whole suite, `ctest -j16` with 16 busy loops, four times: 54 of 54
  each time.

The change in `core/` is in the firmware too. It was not compiled for the
board here and has not run on it. What to look at there is small: the clock
shows the time, and changes when the minute does.

## What was learned

- The same words as last time, "fails under load, passes alone", and the
  same corner of the screen, but not the same cause: here the load moved
  the test onto a moment that is always there, one second after the start.
- A check that fails for a reason nobody expected may still be right. The
  first thought was to make the test ignore the clock everywhere; the
  update with nothing in it was the window manager's fault, not the
  test's.
- Fixing the first failure showed the second, which the first had hidden
  in every loaded run.

## What is left

- `serial_console` failed once in a parallel run and was not reproduced.
  If it does again, the message of the assertion that failed is what is
  needed: its checks are time limits (5 s for a service to stop or start,
  10 s for the shell's answer, 2 s for a port).
- A board check of the clock when this branch has its turn, with the rest
  of a batch: it does not need one of its own.

## On the board (2026-11-19, d3c32c8)

App-flashed on the 3 MB layout. Free RAM 189,107 internal and 5,955,748
external as on main; no missed refill. Once Wi-Fi had joined, the taskbar clock
showed the laptop's time to the minute (12:18 PM at 12:18 on the laptop). The
board's own time read 12:18:24; after waiting for the minute to pass,
`dbg.py time now` read 12:19:02 and a screenshot showed 12:19 PM, the next
minute, so the clock still follows the minute.
