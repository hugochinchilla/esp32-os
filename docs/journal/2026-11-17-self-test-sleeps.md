# The start-up screens sleep while they wait

2026-11-17, the board thread, on `main`.

## What was asked

The coordinator queued two pieces of laptop work. The first: the start-up
screens should stop making ESP-IDF's task watchdog warn that IDLE0 did not
run ("Task watchdog got triggered ... IDLE0"). It was found during the PR 13
board turn and confirmed on old main 098e92f: it is not new. The pace of the
self-test, the wait for the monitor, the 5 s countdown, skipping with a key,
and the memory test's timing had to stay as they were. The watchdog from
PR 23 had to stay fed, and `dbg.py` had to keep answering during the screens.
Also asked: clear three things Clang reported as unused.

## What was found

Every wait on the start-up screens and on the error screen goes through one
function, `wait_until()` in `core/src/shell/boot.cpp`. It loops until the
clock reaches the time, calling `io.idle()` each time round to answer the
debug link. On the board `idle` is `boot_idle()`, a `uart_read_bytes()` with
no timeout. So the UI task never slept: a wait of several seconds kept a core
busy, the idle task on that core never ran, and after 5 s the task watchdog
(which watches both idle tasks) warned.

## Options

- *Let `boot_idle()` wait one tick for bytes.* One line. The task sleeps up
  to 10 ms per look (FreeRTOS runs at 100 Hz here), the idle tasks run, and
  a byte from the PC is answered at the end of the tick. Tried first: the
  warnings went, but each wait now ran up to 10 ms long. The self-test is
  made of many short pace waits, so it took 11,783 ms instead of 11,254 (the
  desktop came 0.4 to 0.5 s later).
- *Sleep only while a good part of the wait is left (chosen).* `BootIo` gets
  an optional `nap`, like `idle` but allowed to sleep briefly. `wait_until()`
  calls it while more than 20 ms of the wait remain and calls `idle` for the
  rest, so the end of a wait is still found to the microsecond. On the board
  `nap` is `boot_nap()`, the same read with a timeout of one tick. Other
  builds (the PC programs, the browser) leave `nap` empty and nothing changes
  for them.
- *Turn off the idle-task check of the task watchdog.* Rejected: it hides the
  next real case of a starved core too.
- *Raise FreeRTOS to 1000 Hz so a tick is 1 ms.* Rejected: it changes the
  whole system's timing for the sake of a start-up screen.

## What was built

- `core/include/core/shell/boot.hpp`: `BootIo::nap` (may be empty) and
  `BootIo::kNapMarginUs` (20 ms).
- `core/src/shell/boot.cpp`: `wait_until()` naps while more than the margin
  is left, then looks without sleeping.
- `firmware/os/main/main.cpp`: `boot_nap()`, set as `boot_io.nap`.
- The three Clang findings removed: the unused `kLines` in `boot.cpp`, the
  `g_file_timer` that was set but never read in `desktop.cpp` (the timer is
  still started), and the unused `held_` of `RadioButton` in
  `core/include/core/wm/widgets.hpp`.

## How it was tested

PC tests 40 of 40 (`dbg_tool` failed once in a first full run, then passed
alone and in a second full run; nothing in it touches this change). On the
board, after a flash of this build:

| | before | one-tick read | chosen |
|---|---|---|---|
| task watchdog lines in a normal start | IDLE0 warnings | 0 | 0 |
| self-test, ms | 11,254 | 11,783 | 11,272 |
| boot to desktop, ms | 11,541 to 11,688 | 12,050 | 11,543 |

- The error screen (`dbg.py boot crash`) shown for 12 s: no task watchdog
  line. Before, the rollback test of the updates turn showed several during
  that screen.
- `dbg.py ping` during the self-test screen: 105 answered in 6 s, median
  9.3 ms, the slowest 29.9 ms. It used to answer in under a millisecond; a
  few milliseconds more while the start-up screens show is the price of
  letting the core sleep.
- A key pressed 4 s into a normal start skipped the rest: the self-test line
  says 3,595 ms and the desktop came at 3,860 ms.
- Our own watchdog (PR 23) did not trip in any of these starts.
