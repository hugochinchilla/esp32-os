# The hang with a brick desktop and screenshots

2026-11-19. Backlog item 75. Tested on the PC; the firmware is compiled
(see "Tested") and has not run on the board.

## What was asked

The coordinator's note, after board batches 22 and 23 (the start-up fade,
item 74): the desktop is restarted by the watchdog ("hang, no sign of life
for 15-16 s, Task: ui, waiting (on no core)", uptime about 16 s) when the
capture tool asks for screenshots from the restart on and the desktop has
the Bricks pattern. With Bricks and the tool: 6 hangs in 8 runs (1 of 2 on
a fast start with no fade, 3 of 3 with the fade). With the plain desktop
and the tool: 0 of 6. With Bricks and no tool: 0 of 3. With Bricks and the
tool on a desktop that had been up for a while: 0 hangs in 25 screenshots,
each about 0.8 s against 0.2 s on the plain desktop. The debug link stops
answering from the start. `C:\crash.txt` says no more than the line above.
The PC simulator does not show it (193 screenshots). "Find the cause and
fix it; add diagnostics if needed."

The entry that looked for it first and did not find it is
`2026-11-19-no-splash.md`, "The hang: not found".

## What was found

Nothing waits for ever. The UI task was busy sending screenshots nobody
was waiting for any more, for longer than the watchdog allows, in one round
of its loop.

Three facts together:

1. **The capture tool gives a request up after 0.6 s and sends a new one.**
   `capture84.py` sets `Board.timeout = 0.6`; `Board.request` tries three
   times, each with a new sequence number, and the script then asks again
   after 0.05 s. While the board restarts, nothing answers, so the tool
   sends a request about every 0.6 s. Answers that carry an older sequence
   number are thrown away by the tool.
2. **A screenshot is answered on the UI task, inside the read of the
   serial bytes, and takes as long as the serial line takes its bytes**:
   0.2 s for the plain desktop, about 0.8 s for the brick one (the
   encoding copies from 1, 2 or 4 pixels back and 1, 2 or 4 rows up;
   bricks repeat every 8, so two rows of every eight go out nearly
   unpacked). The task sleeps in the serial driver while the bytes leave,
   which is why the record said "waiting (on no core)".
3. **The loop read the serial port until it was empty, up to 16 reads a
   round, and answered every request it read before it came round again**
   (`firmware/os/main/main.cpp`, `kSerialReads`). The earlier entry read
   this as "a round with one screenshot in it ends". But while one
   screenshot of 0.8 s is sent, the tool's next request arrives (0.6 s),
   so the next read is never empty: the round goes on for all 16 reads,
   with one or two requests in each.

So with an answer slower than the tool's patience the board is behind from
its first read and never catches up: 16 reads of one or two screenshots of
0.8 s are 13 to 26 s without a sign of life, and the watchdog's limit is
15 s. With an answer of 0.2 s the first read's answer is out before the
next request is sent, the tool gets it in time and from then on asks only
after each answer: no hang. On a desktop that has been up, the tool is
never behind (the reply's head comes at once, and each piece within
0.6 s), so no hang there either, whatever the pattern.

A model of the loop and the tool's timing
(`python3`, 20 lines, not kept) gives the longest round for a screenshot
of S seconds, with the loop starting 1 s after the restart:

| S | longest round |
| --- | --- |
| 0.2 s | 0.4 s |
| 0.5 s | 2.0 s |
| 0.7 s | 22.4 s |
| 0.8 s | 25.6 s (16 s when the tool stops at 20 s) |
| 1.0 s | 32 s |

The edge is at the tool's 0.6 s. The board's 0.8 s was measured by the
tool and includes its own decoding, so the board's own time for a brick
screenshot is somewhere above 0.6 s and close to it: that fits "6 of 8"
and not "8 of 8", and "3 of 3" with the fade, whose half-risen pictures
pack worse still. The uptime of 16 s in every record fits too: the last
sign of life was the first round's, about 1 s after the start.

Why the PC did not show it: the simulator's serial line is a pseudo
terminal that takes a screenshot in a few milliseconds, so the tool is
never behind.

The remote screen (VNC and the web page) is not touched by this: its
frames are made on the services task (`inetd`), not on the UI task.

This is a deduction from the code, the tool and the batches' figures. It
explains every one of them; the board turn asked for below is what
confirms it.

## The options considered

1. **Answer only the last screenshot request that waited** (chosen). A
   screenshot request is kept, not answered, while serial bytes are read;
   after the read the loop sends one screenshot, for the newest request.
   A request with another behind it was given up by the computer (there is
   one tool on the wire, and it asks again only then). Costs: three small
   fields in `dbg::Protocol`, one more call in each loop that feeds it. A
   round holds one screenshot at most, so the watchdog and the desktop's
   timers get their turn between two of them, and the tool is in step again
   after one screenshot.
2. **Stop reading after one request per round.** Bounds the round, and
   the desktop would run between screenshots, but the board stays behind
   for good: every answer is for a request already given up, and the
   requests pile up in the driver's 4 KB until bytes are lost.
3. **Give a sign of life from inside the screenshot.** Hides it from the
   watchdog; the desktop is still frozen for as long as the tool goes on.
4. **Send the screenshot from another task**, from a copy of the picture
   (307 KB of external RAM, or 480 KB at 800x600). The desktop would not
   stop for 0.2 to 0.8 s per screenshot at all. A real improvement, and
   more than this fault needs: a copy to make without missing refills, a
   task, a lock around the link's sending. Left as an idea.
5. **A longer wait in the tool.** `tools/dbg.py` waits 2 s by default; the
   0.6 s was the capture script's own. A board that a debug tool's timing
   can restart is the fault, so the fix is on the board.
6. **A better encoding for 8-pixel patterns** (a copy from 8 rows up).
   Would bring the brick desktop near the plain one; changes the protocol
   and the tool, and does not remove the fault for the next slow picture
   (a photograph in the viewer takes over 3 s).

## What was chosen, and why

Option 1, by the agent (to confirm: `docs/decisions/screenshot-requests.md`).
It removes the cause, the unbounded round, and also what made the link
useless from the start, the answers nobody waited for. The requests given
up get no reply at all: the tool has already dropped their sequence
numbers.

The diagnostics, as asked, so that the next hang says more than "waiting":

- **What the task was doing.** The loop says what it is about to do
  (`watchdog_doing`, one store of a pointer): the debug link's requests, a
  screenshot for the debug link, messages from other tasks, the desktop's
  timers and painting, asleep. A hang's record carries it as a new line,
  "Doing   : ...". With it, this fault's record would have read "Doing   :
  the debug link's requests".
- **Where the task waited.** When the stuck task is waiting, not running,
  the watchdog reads its chain of calls from what the kernel saved on its
  stack (as ESP-IDF's `esp_backtrace_print_all_tasks` does) and the record
  gets the "Trace   :" lines a panic has. The record is sealed before the
  stack is read, so if reading it goes wrong, the record without the trace
  is what remains. A task that spins on a processor gets no trace: its
  saved frame is an old one, and stopping the other processor to read a
  fresh one is more than a watchdog should do.
- Both go to the serial log too, in the watchdog's last line.

The record's layout is unchanged (`reason` was unused for a hang, `trace`
was there), so no version change.

## What was built

- `core/dbg/protocol`: `handle_frame` keeps a Screenshot request
  (`shot_waiting_`, sequence number, flags; a newer one replaces it and is
  counted in `shots_given_up()`); `answer_waiting()` sends it.
- `firmware/os/main/main.cpp`: the loop calls `answer_waiting()` after the
  serial reads, and does not sleep after a screenshot (bytes arrived
  meanwhile); `boot_idle` and `boot_nap` call it after their read. The
  loop's parts are named with `watchdog_doing`.
- `host/sim.cpp`: the same two calls.
- `core/sys/watchdog.hpp`: `doing()`. `core/src/sys/crash.cpp`: the
  "Doing" line, and the trace lines for a hang that has them.
- `firmware/os/main/crash.cpp`: `watchdog_doing`, `waiting_trace`.
- `docs/debug-protocol.md`, "Screenshots": only the last waiting request
  is answered.

## Tested

- PC: `host/tests/test_dbg.cpp`, `test_screenshot_waits` (three requests
  and a ping in one piece: the ping answered at once, one picture, for the
  last; a request split over two feeds); `test_crash.cpp` (the note, the
  new lines). All PC tests pass.
- Firmware: compiles and links in the cloud (ESP-IDF v5.5, `idf.py build`,
  no warning from the changed files). `waiting_trace` follows what ESP-IDF
  does for a task that is not running; whether the addresses it gives are
  right can only be seen on the board (`dbg.py fault wait`).
- Not run on the board. Asked of the board turn: the batch-23 runs again
  (Bricks, the capture tool from the restart, fast and through the BIOS:
  no hang, and the tool gets screenshots within a few seconds), `fault
  hang` and a made-up waiting hang for the new lines of the record, and
  `misses`.

## What was learned

- "The loop gives its sign of life each round, and a round with one
  screenshot in it ends" was true and beside the point: nothing bounded
  how many screenshots a round holds. A loop that drains its input before
  it comes round is only bounded if answering is faster than asking.
- A record that says "waiting" sent the search after locks. The task was
  waiting, for the serial driver, a few milliseconds at a time, thousands
  of times. "Doing" would have said so.

## What is left

- The desktop still stops for the length of each screenshot (0.2 to
  0.8 s; option 4). Dense captures of an animation, like the fade's,
  cannot be had over the serial link for that reason.
- Brick-like patterns pack poorly (option 6).
