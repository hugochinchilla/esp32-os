# Missed video refills go to the system log

2026-11-17, the board thread, on `main` (backlog item 38).

## What was asked

After the refill investigation (`2026-11-17-refills.md`) the owner decided to
leave `dbg.py tasks` as it is, and asked: "I would like to make each missed
update be logged on the system log so the agents can check nothing causes
this misses, and something should let us know when misses were caused by the
dbg.py invocation (maybe just another set of log lines)". The coordinator
added: the interrupt must not log, the log must not be flooded, the logging
must not cause misses itself, a `dbg.py` command for agents that exits with
an error when there are misses to chase, and a rule in CLAUDE.md.

## Options

- *Who writes the line.* The interrupt cannot (no logging there). Choices:
  the UI loop, the services loop, a new task, or the scanlog task that
  already samples the counters for the first 20 s. **Chosen: the scanlog
  task**, kept alive after its 20 s and woken once a second. It costs no new
  stack and no RAM, and does not depend on the UI or on inetd, which can be
  ended. The UI loop sleeps for as long as nothing happens; the services
  loop goes away with End Task on inetd.
- *What is counted.* The interrupt's `missed` counter is reset by the
  start-up timeline every 100 ms. **Chosen:** a second counter in `vga.c`
  that is never reset, plus where on the screen the latest miss was and the
  worst lateness since the last look.
- *How often.* One line per miss would let a storm fill the 16 KB log in a
  minute. **Chosen:** a line at once for the first miss of a burst, then at
  most one every 10 s with the sum. A miss every second for ten minutes
  writes 60 lines, about 5 KB (PC test).
- *Late refills.* Late but finished in time does no harm on screen. **Not
  logged**; they stay in the memory report's counters.
- *Telling the debug link apart.* Options: a log line for every task-list
  request (twenty a second when polled, a flood of its own), or counting the
  misses that happen during the walk. **Chosen: counting.** The firmware
  reads the miss counter before and after `uxTaskGetSystemState()` (50 µs
  after, so a refill held off on the other core is counted too), and the
  miss line names that share: "(all during a debug task list)" or
  "(2 during a debug task list)".
- *How agents read it.* Telnet's `dmesg` works but depends on telnet. **Chosen:
  a debug request, Log read (0x16)**, which gives the log a piece at a time.
  `tools/dbg.py dmesg` prints it all; `tools/dbg.py misses` prints only the
  lines with misses not caused by the debug link and exits 1 if there are
  any (`--all` shows the debug link's too).

## What was built

- `core/include/core/sys/missed_refills.hpp`, `core/src/sys/missed_refills.cpp`:
  `sys::MissedRefills::check()`, which decides when to write a line and what
  it says.
- `firmware/os/main/vga.c`, `vga.h`: `vga_missed_total()`, the counter that
  is never reset, the line of the latest miss and the worst lateness.
- `firmware/os/main/main.cpp`: the scanlog task calls `log_missed_refills()`
  every second (every tenth sample during the start-up timeline, then a
  1 s wait for ever); `tasks()` counts the misses during its walk.
- `core/src/dbg/protocol.cpp`: Log read (0x16).
- `tools/dbg.py`: `log_text()`, `missed_refills()`, the `dmesg` and
  `misses` commands.
- `docs/debug-protocol.md` (the request and the line formats),
  `docs/DECISIONS.md`, `CLAUDE.md` (the rule for agents), `docs/BACKLOG.md`.

The lines:

    W (2000) os: video: 3 refills missed, worst 640 us late, last at line 240
    W (3000) os: video: 2 refills missed (all during a debug task list), worst 812 us late, last at line 96
    W (4000) os: video: 5 refills missed (2 during a debug task list), worst 640 us late, last at line 30

## How it was tested

PC tests 41 of 41: `missed_refills` (new: when a line is written, the three
texts, the debug share never more than the misses, the 10 s quiet time, a
storm of ten minutes giving 60 lines, a short buffer), `dbg` (Log read: from
the first whole line after the ring has gone round, the next position, an
empty reply at the end, bad arguments), `dbg_tool` (how `dbg.py` tells the
lines apart). `dbg_tool` failed once in the first full run, before the
simulator check was corrected for the simulator keeping a log.

On the board, after a flash of this build:

| run | late | missed | new log lines |
|---|---|---|---|
| an idle minute at the desktop | 0 | 0 | none |
| `dbg.py tasks` every 50 ms for 20 s | 38 | 33 | three, all "(all during a debug task list)": 2, 15 and 16 |
| ten VNC rounds (connect, full screen, disconnect) | 0 | 0 | none |
| a 300 KB file put, then copied, typed and deleted over telnet | 0 | 0 | none |

- The three lines add up to the 33 misses the counters saw, all counted as
  the debug link's. `dbg.py misses` then exits 0 and says "0 missed-refill
  line(s) not caused by the debug link; 3 caused only by it"; with `--all`
  it prints the three lines.
- The cost: the scanlog task had run for 10 ms in total after about two and a
  half minutes, and 972 of its 3 KB of stack were never used, after writing
  the lines. The idle minute had no late or missed refill.
- A Firefox tab on the owner's laptop had the web page viewer open; it took
  the screen back once during the VNC rounds, and the round was repeated.
