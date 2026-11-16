# Task statistics: asking the kernel who used the processor

2026-11-16. Backlog item 1. Branch `claude/project-thread-d77dzk`.

## 1. What was asked

The architecture review of the same day
([journal](2026-11-16-architecture-review.md)) ended with a backlog, and the
owner approved it whole:

> de la lista final de tareas que sugieres, en las que me das opciones y tú
> respondes ya con una opción sugerida, apruebo todas tal y como están, con sus
> sugerencias. [...] Empieza poquito a poco, no intentes hacer todo a la vez,
> paraleliza algunas cosas si se pueden, pero prioriza hacer las cosas dejando
> un buen detalle del trabajo hecho y de las decisiones tomadas

Item 1 is the first piece of "know what is running": switch on FreeRTOS's
run-time statistics, hand out a list of tasks (name, state, priority, core,
processor time, free stack) through the System port, and give `tools/dbg.py` a
command that shows it. Nothing on the screen changes yet. Task Manager gets its
columns in item 3, which needs this and the process table (item 2).

## 2. What was found

**What we had.** Task Manager's two figures are stopwatches we hold ourselves:
`Wm::busy_us` adds up the time the UI loop spends on events, timers and
painting, and `scanout_us` does the same inside the video interrupt. Anything
that is not one of those two is invisible: Wi-Fi, the network stack, the idle
time of each core.

`tools/dbg.py memory` already asked the kernel for its task list
(`uxTaskGetSystemState`), but only printed each task's unused stack, and only
as text. The setting behind it, `CONFIG_FREERTOS_USE_TRACE_FACILITY`, was on.

**What the kernel can do.** FreeRTOS can keep a running total of processor
time per task. It is off by default because it costs a little at every task
switch: the kernel reads a clock, subtracts the reading it took when the task
came in, and adds the difference to that task's total. In the kernel source
(`tasks.c`, in the task-switch routine) it is these lines:

```c
ulTotalRunTime = portGET_RUN_TIME_COUNTER_VALUE();
if( ulTotalRunTime > ulTaskSwitchedInTime[ xCurCoreID ] )
{
    pxCurrentTCBs[ xCurCoreID ]->ulRunTimeCounter += ( ulTotalRunTime - ulTaskSwitchedInTime[ xCurCoreID ] );
}
ulTaskSwitchedInTime[ xCurCoreID ] = ulTotalRunTime;
```

That is the whole mechanism. There is one "switched in at" time per core, and
one total per task. Nothing samples, nothing estimates: the totals are exact
to the resolution of the clock.

**The settings in ESP-IDF v5.5**, read from `components/freertos/Kconfig`:

| Setting | What it does | Default |
| --- | --- | --- |
| `FREERTOS_GENERATE_RUN_TIME_STATS` | Keeps the totals. Also pulls in the trace facility and the text-formatting functions | off |
| `FREERTOS_RUN_TIME_STATS_CLK` | Which clock: the ESP timer (microseconds) or the processor's cycle counter | ESP timer |
| `FREERTOS_RUN_TIME_COUNTER_TYPE` | 32-bit or 64-bit totals | 32-bit |
| `FREERTOS_VTASKLIST_INCLUDE_COREID` | Adds the core a task is tied to to each row of the list | off |

## 3. The options considered

The owner had approved the item with its recommendation, so the big choice
(use the kernel's statistics at all) was made. What was left were seven small
ones. None changes what the owner sees or where the project goes, so they were
made here and are written down for him to overrule.

### 3.1 Which clock

| Option | For | Against |
| --- | --- | --- |
| **ESP timer** (chosen) | One clock for both cores. Microseconds, so the numbers mean something without conversion. It is the same clock `uptime_ms` and all our log lines use | 1 µs resolution. A function call at every task switch instead of reading one register |
| Processor cycle counter | 240 ticks per microsecond. Reading it is one instruction | Each core has its own counter and the two are not in step. ESP-IDF's own Kconfig carries a note that this should be disabled on two cores. At 240 MHz a 32-bit count wraps every 17.9 seconds |

The cycle counter is the better stopwatch on a one-core chip. On ours a task
that moves from core 0 to core 1 would be timed with two different clocks.

### 3.2 How wide the totals are

| Option | For | Against |
| --- | --- | --- |
| 32-bit (ESP-IDF's default) | Cheaper sums on a 32-bit processor | 2³² µs is 71 minutes 35 seconds. After that the totals wrap, each task at a different moment, and "share since start-up" turns into nonsense. Every reader would have to know and work around it |
| **64-bit** (chosen) | Never wraps (584,000 years). Readers subtract two numbers and are done | One 64-bit add instead of a 32-bit one per task switch, and 4 more bytes per task |

A machine left on overnight with its screen saver is exactly the case where
somebody opens Task Manager after more than 71 minutes.

### 3.3 Text or numbers from the port

`dbg.py memory` set a precedent: the firmware formats a text report and the
tool prints it. FreeRTOS even has a ready-made function for this,
`vTaskGetRunTimeStats`, which fills a buffer with a table.

| Option | For | Against |
| --- | --- | --- |
| Text, formatted on the board | Ten lines of code. Nothing to parse | Only a person can read it. Task Manager (item 3) and `top` (item 16) need the numbers, so the port would be written twice. `vTaskGetRunTimeStats` also truncates to whole per cent and has no state, priority or stack columns |
| **A list of records** (chosen) | One port function serves `dbg.py` now and Task Manager and `top` later. The PC tests can check every field | A wire format to define and a parser in the tool |

So `ports::System` gained `tasks()`, which fills an array of `TaskInfo` (name,
id, state, priority, core, where the stack is, unused stack, run time) and a
`TaskTotals` (the clock now, the number of cores, how many tasks did not fit).
The text report of `memory` stays as it is.

### 3.4 Counters or percentages

| Option | For | Against |
| --- | --- | --- |
| The board works out percentages | The reader gets the figure it wants | Per cent of what, over which period? Since start-up hides what is happening now. "Over the last second" means the board has to remember the previous reading for each reader, and two readers (Task Manager and `dbg.py`) would disturb each other |
| **Raw totals, the reader subtracts** (chosen) | No state on the board. Every reader picks its own period. It is how Unix does it: `/proc` holds totals and `top` subtracts | Two requests for a "now" figure |

`dbg.py tasks` takes two readings a second apart and prints both figures:
`%START` since start-up and `%NOW` between the readings. Tasks are matched by
the kernel's task number, which a task keeps for life, not by name: two tasks
can have the same name. A task that appeared between the readings counts from
zero.

Shares are of one core, so the column adds up to 200 % on our two cores. That
is the convention Task Manager already uses ("the share of one core the UI
task spent"), and the one `top` uses on Linux.

### 3.5 A new protocol command or more in an old one

The Stats reply (0x06) is a row of 4-byte numbers that older tools read by
position; a list of variable length does not fit there. The Memory report
(0x13) is text. So Tasks is a new command, 0x14, the next free number. The
format is in [`docs/debug-protocol.md`](../debug-protocol.md).

One reply holds at most 1,023 bytes. A task takes 19 bytes plus its name (at
most 15 letters in FreeRTOS), so 29 tasks always fit and more when names are
short. How many the board has today is one of the things the board check
will show (the Wi-Fi and network stacks bring most of them). If they ever do not fit, the reply says
how many were left out and the tool prints it. Splitting the list across
several replies was the other option; it was not worth the code until the
machine has that many tasks.

### 3.6 Which core

`TaskStatus_t` can carry `xCoreID`, but it is the core the task is *tied to*
(0, 1 or "either"), not the core it happens to be on. FreeRTOS does not record
where a task last ran in that list. The column is still worth having: it shows
at a glance that Wi-Fi and the network stack are tied to core 0 and that `ui`
is free to move, which is the arrangement that keeps the picture steady
(`docs/DECISIONS.md`). It needs the fourth setting in the table above.

### 3.7 Where the kernel's list is kept while it is read

`uxTaskGetSystemState` wants an array big enough for every task, and returns
nothing at all if it is too small. `dbg.py memory` uses a fixed array of 24.

| Option | For | Against |
| --- | --- | --- |
| A fixed array | No allocation | Internal RAM for every slot, held for ever, for something used now and then. Too small one day and the list silently comes back empty |
| **Allocated for the call, in external RAM** (chosen) | Sized from the number of tasks at that moment plus four. Costs no internal RAM, of which only about 142 KB is free | An allocation per request. If it fails there is no list |

## 4. What was built

- `firmware/os/sdkconfig.defaults`: the four settings, with a comment.
  **The `sdkconfig` file has to be deleted before the next build**, as with
  any change to the defaults, or the old settings stay.
- `core/include/core/ports.hpp`: `TaskState`, `TaskInfo`, `TaskTotals` and
  `System::tasks()`. The default returns 0 ("not available"), so adapters that
  have no kernel need no change.
- `firmware/os/main/main.cpp`: `EspSystem::tasks()`, which calls
  `uxTaskGetSystemState` and copies the fields over. Compiled out, returning
  0, if the settings are off.
- `core/src/dbg/protocol.cpp`: command 0x14.
- `tools/dbg.py`: `Board.tasks()`, `task_table()` and the `tasks` command
  with `--interval` and `--json`.
- `host/host_adapters.hpp`: the PC's System adapter can be given a list by a
  test, or make one up that moves with the clock. The simulator does the
  latter, so `dbg.py tasks` has something to show without a board:

```
  ID  NAME             STATE      PRI CORE  %START    %NOW         TIME   STACK IN
   2  IDLE0            ready        0    0    93.0    93.0     0:04.674     700 internal
   3  IDLE1            ready        0    1    89.0    89.0     0:04.473     700 internal
   1  ui               running      5    -     6.0     6.0     0:00.302    9200 internal
   4  wifi             blocked     23    0     1.5     1.5     0:00.075    3600 internal
   ...
8 tasks on 2 cores, up 5.0 s, idle 182.0 % of 200 % since start, 182.0 % in the last 1.00 s
```

These figures are invented by the simulator. The real ones come from the
board check.

**Tested on the PC.** `host/tests/test_dbg.cpp` checks the reply byte by byte:
no list gives "unknown command"; every field of two tasks, with a run time
above 32 bits; 40 tasks with the longest possible names give whole records
only and the right "left out" count. `tools/test_dbg_sim.py` runs the real
tool against the simulator and checks the table, the two-reading arithmetic
and a task that starts between readings. All 20 PC tests pass.

**Compiled, not run.** The firmware was built in the cloud container with
ESP-IDF v5.5 (`tools/setup-cloud-idf.sh`, compile and link only). It builds
without warnings in our files, and the generated `sdkconfig` shows the four
settings took: run-time statistics on, the ESP timer as the clock, the 64-bit
counter, and the core column.

**Not tested: anything on the board.** That includes whether the list is
right, what the statistics cost, and whether reading the list disturbs the
picture (section 5). The board check asked for:

1. `dbg.py tasks` lists the tasks, `ui`, `IDLE0`, `IDLE1` and the Wi-Fi tasks
   among them, and `%NOW` adds up to about 200.
2. `dbg.py memory` before and after twenty runs of `dbg.py tasks`: the "late"
   and "missed" video counters should not move.
3. `dbg.py repaint` against `main` (29 ms there), for the cost per task
   switch.
4. `dbg.py stats` against `main`, for free internal RAM.
5. The picture on the monitor stays steady while `tasks` runs in a loop.

## 5. What was learned

**Interrupts are nobody's time, so they become somebody's.** The kernel only
looks at the clock when it switches tasks. An interrupt is not a task: it
borrows whatever task was running, and its time lands in that task's total.
Our video interrupt runs on core 1 thousands of times a second. Whatever task
is on core 1 pays for it, and most of the time that is `IDLE1`. So `IDLE1`
will look more idle than core 1 really is, and the difference is exactly the
"Video out" figure Task Manager already measures with its own stopwatch
(`scanout_us`). The two sources are not rivals; the honest idle figure for
core 1 is the kernel's `IDLE1` minus our stopwatch. Item 3 has to do that
subtraction.

**Reading the list has a price.** While the kernel copies its list it holds a
critical section: interrupts on that core are off. It also walks each task's
stack to find how much was never used. How long that takes on the board has
not been measured. The video interrupt cannot wait long before a line shows
stale pixels (Wi-Fi delaying it by 0.7 ms was enough, which is why the video
moved to core 1), and `ui` can be on core 1. `dbg.py memory` has done the
same thing since it was written and nobody saw a glitch, which is encouraging
and not a measurement. Hence check 2 above, and the note in the protocol
document that this is not for polling many times a second. If Task Manager's
once-a-second reading turns out to disturb the picture, the fix is to do the
reading from a task tied to core 0.

**All or nothing.** `uxTaskGetSystemState` with an array one slot too small
returns zero tasks, not the ones that fit. The fixed array of 24 in the
memory report has that trap in it. It is left alone here because it is not
part of this item; it is worth moving to the new function when somebody is
next in there.

**"Core" is not where a task is.** See 3.6. It is where it is allowed to be.

**The default would have bitten in 71 minutes.** A 32-bit microsecond counter
is ESP-IDF's default and looks fine in every short test.

## 6. What is left

- The board check, then the merge by the board thread. The backlog row says
  "on a branch" until then.
- Item 3 uses `System::tasks()` for Task Manager's task view, and subtracts
  the video stopwatch from core 1's idle time.
- Memory per task (ESP-IDF's heap task tracking) was mentioned in the review
  as the same kind of switch. It is not part of this item: it costs a few
  bytes on every allocation, and item 3 should decide whether it wants it or
  the PID tag on the Memory port.
- `dbg.py memory`'s stack list could be replaced by `dbg.py tasks`, which
  shows the same column and more.

## 7. On the board (added by the board thread, 2026-11-16)

Merged with main (one-shot fast start, journal entries), built from a fresh
sdkconfig (`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`, the clock
`CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER`, a 64-bit counter), flashed,
and checked:

1. `dbg.py tasks` 6.6 s after a (fast) start:

```
  ID  NAME             STATE      PRI CORE  %START    %NOW         TIME   STACK IN
   6  IDLE1            ready        0    1    99.7   100.0     0:06.610     780 internal
   5  IDLE0            ready        0    0    92.1    96.0     0:06.106     772 internal
  10  ui               running      5    -     5.1     3.8     0:00.336   10956 internal
  15  wifi             blocked     23    0     1.7     0.1     0:00.116    3636 internal
  13  tiT              blocked     18    0     0.3     0.0     0:00.017    1768 internal
   3  esp_timer        suspended   22    0     0.0     0.0     0:00.003    3164 internal
  11  scanlog          blocked      1    -     0.0     0.0     0:00.001    2380 internal
  16  mdns             blocked      1    0     0.0     0.0     0:00.003    2392 internal
   7  Tmr Svc          blocked      1    -     0.0     0.0     0:00.000    1412 internal
   1  ipc0             suspended   24    0     0.1     0.0     0:00.005     588 internal
   2  ipc1             suspended   24    1     0.1     0.0     0:00.005     580 internal
  14  sys_evt          blocked     20    0     0.0     0.0     0:00.001     772 internal
12 tasks on 2 cores, up 6.6 s, idle 191.8 % of 200 % since start, 196.0 % in the last 1.01 s
```

   ui, IDLE0, IDLE1 and the Wi-Fi tasks are there; %NOW sums to 199.9.
   IDLE1 shows 100 % although the video interrupt takes about 5 % of core 1
   (`scanout_us`): interrupt time is charged to the task it interrupts, as
   section 5 expected, so the video does not appear in the list.
2. Across 20 runs of `dbg.py tasks` (45,744 refills): 0 late, 0 missed.
3. Repaint of the empty desktop: 14.21 ms on average, against 14.15 ms on
   main (+0.4 %, within the noise).
4. Free internal RAM: 105,443 bytes, against 107,479 on main (2.0 KB less).
5. The picture on the webcam while `dbg.py tasks` ran in a loop: 720 frames,
   the largest change between frames 0.11 (camera noise; a shaken frame
   measured 38 before). Steady.

The run-time clock reads esp_timer at every task switch; no change in the UI
or scanout timing could be measured.

