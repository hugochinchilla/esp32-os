# Programs on tasks of their own

2026-11-17. Backlog item 26, first step. Written while the owner's answer
to the card below is awaited; what was built is the part that the first
two options share.

## What was asked

Item 26: "Apps on their own tasks, one at a time". The aim since the
architecture review is AmigaOS's model: real tasks, one memory space. Up
to now every program's code ran on the UI task, so a program that hangs
freezes the desktop until the watchdog restarts the machine.

Item 37 made this possible: all 17 programs reach the system through
calls and numbers, and the system reaches a program in six places only
(an event for its window, its timer, its menu's action, a message from
another task, the result of its job, its launch).

## The question put to the owner

How does a program paint, and hear of things, when it runs on another
task? The card (2026-11-17, not answered when this was written):

1. **The server waits for the program** (recommended). Each call into
   the program is handed to its task, and the UI task waits until it is
   done, as Win16's `SendMessage` did. One of the two runs at a time:
   windows, timers and the process table still need no locks, and
   nothing else touches the screen. With a limit on the wait, a program
   that does not come back is painted grey, marked as not responding and
   can be ended; the desktop goes on.
2. The program paints when it wants, holding a lock on the screen.
   Faster for a game, but a program that hangs with the lock freezes
   the desktop, which is what the item is there to end.
3. Each window keeps a picture of itself that the program paints into
   and the server copies. Never waits; costs RAM per window and takes
   back the decision that windows keep no picture.
4. Not doing item 26.

**The owner chose the first** (2026-11-17, after this was built). Before
choosing he asked whether, whatever was decided, a full-screen game
could have a way of its own to paint, faster than the window system: he
would like to bring Doom over later. It can, and it does not depend on
this choice: the card is about painting inside a window, where the
screen is shared; a game with the whole screen shares nothing, so the
server can stand aside and hand it the frame buffer (640x480, one byte
per pixel, RRRGGGBB, no palette: Doom's 256 colours would go through a
table, its 320x200 doubled), with the keys sent raw and one combination
kept by the system to come back.

## What was built (the agent's, on the recommended option)

- `core/prog` (`prog::Runner`): a program can be given a task of its
  own. The task sleeps until it is handed a call, makes it as the
  process, says so and sleeps again. When its process ends, the task is
  given back: at once if the program was doing nothing, or when the call
  it was in the middle of has returned (a program that closes its own
  last window).
- `proc::Table::call(pid, fn, context)`: the one way into a program's
  code. The six places above use it. Without tasks in the machine (the
  browser build, the plain PC desktop, most tests) it makes the call
  where it is, as before.
- Calls inside calls: a program that calls into itself does so where it
  is. Into another program with a task, it waits for that one. If that
  one calls back into the first, which is waiting, the call is made
  there (otherwise the two would wait for each other for ever; Win16
  had the same knot and cut it the same way).
- "One at a time": a program says so in the program table
  (`App::own_task`). The first is **Culebra**: a game, with a timer, a
  menu, a settings file, and a message box over it.
- The stack is 8 KB, from internal memory, counted as the program's.
  The agent's guess, to be measured on the board (`dbg.py tasks` shows
  the unused stack).

## What it does not do yet

(The limit came later the same day: see below.)

- **No limit on the wait.** The UI task waits for as long as the
  program takes, so a program that hangs still freezes the desktop.
  This step costs a stack and two task switches per call and gives
  nothing back by itself: it is there to be measured on the board
  before the rest is built on it.
- The limit needs every OS call to be safe from a program that comes
  back late, while the desktop has gone on: a lock taken for the length
  of each call (about 200 functions of `os` and `os::ui`), given by the
  UI task only to the program it is waiting for, or while it sleeps.
  Then the grey window and "not responding" in the Task Manager.
- The other sixteen programs, and init as a task that waits for
  endings (left for this item by item 39).
- Events through a queue (item 37's last part): with a wait on every
  call there is no queue yet. It comes with the limit: what a late
  program has missed must wait for it somewhere.

## Options not taken, in what was built

- Marshalling at each of the forty places where the window manager
  makes a program current: all windows of programs are the system's
  `Behind` now, so one place (`tell`) covers them.
- A queue of events per program from the start: the tests would stop
  being exact (a key sent, the game checked on the next line), and
  nothing can use the queue until the lock above exists.

## Tested

- `test_prog`: the runner alone, with real threads: where each call
  runs, calls inside calls, a process that ends idle and one that ends
  in its own call, no kit left, no memory for the stack.
- `test_snake`: the whole Culebra scenario (keys, the timer, the menu,
  the message box, closing, opening again) is run twice, the second
  time with the program on a thread of its own. Same checks, same
  pictures, the task given back at each close.
- The simulator gives Culebra a task as the board does.
- PC gate passes (42 of 42 tests, the pictures, the OS-call check).

## On the board, first step (figures from the board thread, 2026-11-17)

It works and nothing hangs (menus, the box over it, key repeat, drag,
minimise, the screen saver, End Task, `kill`, `kill -9`; no missed
refills). What it costs: 8.5 KB of internal memory while Culebra is
open, all back on close; the stack was used up to about 2.8 KB of the
8 KB. Processor time for 10 s of play, UI task plus Culebra's task:
about 194 ms against 182 ms on `main`, +7 %. A repaint with Culebra
open 25.6 to 26.0 ms against 25.2 to 25.3. By the rule this thread gave
("measurably worse: do not merge"), it was not merged, and the board
thread put the figures to the owner on a card.

## The same day: the limit on the wait

The owner chose the first option. This is its second half: what makes
a program that hangs stop freezing the desktop.

**The limit.** Whoever hands a call to a program waits two seconds for
it (`prog::Runner::kLimitMs`; the agent's figure, to confirm). After
that the program is *late*: the caller goes on as if the call had not
been used, and no call is made for it until it has finished the one it
is in. Its keys, clicks and timer ticks in between are dropped, not
kept.

**What had to be true first.**

- Nothing handed to a program may point into the caller's stack, since
  the caller may have gone. A call's context is handed over by copy
  and copied back only if it finished in time (`proc::call_as`). An
  event goes whole: the key, the mouse message and the text (a path at
  most) are copied with it.
- A late program must not touch the desktop's things while the desktop
  is using them. Every OS call now begins with `enter()` and ends with
  `leave()` (`prog::InCall`, first line of each of the 168 functions of
  `os` and `os::ui`, and of the eight helpers of the shell that
  programs call). For the UI task and for a program being waited for,
  that is a count going up and down, no lock. A late program sleeps in
  `enter()`.
- When the caller gives up it first waits for the program to leave the
  OS call it may be inside (calls are short), so it never goes on with
  a call half done.
- Once a round, with nothing else under way (`Wm::tick`), the UI task
  lets each late program that is waiting in `enter()` run for up to
  10 ms, and takes back those that have finished. A program that hangs
  in its own code asks for nothing and costs nothing; one that is only
  slow gets its calls done a slice at a time and then answers again.

**What is seen.** What the program paints itself is grey while it is
late; the system's own controls in its window still paint. The Task
Manager says "Not responding" in place of the window's title. Closing
the window (or End Task, or `kill`) ends it without asking it: the
close question cannot be put to a program that does not answer.

**Ending it.** A late program is outside every OS call, so it holds
nothing of the system's, and its task can be taken away
(`ports::Tasks::kill`: `vTaskDelete` on the board). On the PC a thread
cannot be taken away: the process ends at once and the thread is left
to return by itself, kept out of the OS for good.

**Options not taken.**

- A lock around the OS calls (a mutex taken by the UI task whenever it
  runs): two lock operations on every call, for every program, and
  every loop of every machine and test would have to take and give it.
  The count costs nothing where nothing is late.
- Keeping what a late program missed in a queue for it: the queue of
  events is still to come; for now a late program has missed what it
  missed and is painted again whole when it is back. A job's result
  that arrives while its program is late is lost with the rest: to be
  put right with the queue.
- Suspending a late program: it may be inside an OS call.

**Not done.** Only Culebra has a task. Before the others get one:
programs that call each other's code directly (the File manager's
windows told that files changed, the dialogs' answers) must go through
`proc::Table::call` too. The stack size (8 KB; the board used 2.8).

**Tested.** `test_prog`: the limit with real threads: a call that does
not come back, calls refused meanwhile, the late program kept out of an
OS call until `serve()`, taken back, called again; ended while it
hangs. `test_os`: a program with a picture on a thread of its own hangs
on a key: the key comes back, the picture goes grey, the desktop paints
and takes keys, the program wakes and paints again; it hangs again and
closing its window ends it, with all its memory back. PC gate passes
(42 of 42, pictures, OS-call check). The limit is switched on in the
firmware and the simulator; the tests that do not ask for it wait for
ever, as before.

## The owner's answer on the cost (2026-11-17)

On the board thread's card (merge as is; merge with a 4 KB stack; not
yet) the owner chose **"Merge with a 4 KB stack"**, as that thread
reported it here. The 7 % while playing is accepted. `kStackBytes` is
4 KB: the board saw at most about 2.8 KB used, which leaves about
1.2 KB; the board thread checks the headroom in the deepest paths (the
hang and the three ways of ending it) before merging. One size for
every program still: a program that needs more will need its own.

## On the board, second step: three failures (2026-11-17)

The board thread ran the second step (2c3341d) and did not merge it.
What worked: a hang made on purpose in Culebra froze the desktop once,
for 2.00 s, and then the desktop answered in 1 to 3 ms; the board went
grey; the Task Manager said "Not responding"; the watchdog did not
trip; the close button, End Task and `kill` each ended it with the RAM
back; a hung Editor (no task of its own) still got the watchdog. The
stack: about 2.7 KB used in every path. What failed:

1. **Cost.** Playing Culebra took 344 ms of processor time per 10 s
   against 169 with the first step and 164 on `main`; a repaint with
   Culebra open 30.9 ms against 25.9. Other programs were unchanged.
   The cause (reasoned, not measured here): `enter()` and `leave()`
   asked the kernel which task was running and looked it up among the
   slots, on every OS call, and a paint makes hundreds of them. Now a
   program's task keeps its slot in a thread-local pointer
   (`prog::t_slot`), so on any other task an OS call tests one pointer,
   and on a program's task it adds one to a count and reads another
   word.
2. **Missed video refills while Culebra hung**: about one every 5 s,
   1.7 ms late, none once it was closed. A task that spins at the
   desktop's priority breaks the refill rule. Now a program that goes
   late is put at Low priority (`ports::Tasks::set_priority`) and back
   at High when it answers. Whether that is enough is for the board to
   say; if not, the next thing to try is keeping program tasks on the
   core the video is not on. The cause is not understood: the video
   task has a higher priority than either.
3. **Tasks left behind.** Closing Culebra sometimes left its task in
   the kernel with its stack, about 2 times in 15, never with the first
   step. The cause (reasoned from FreeRTOS's code, to be confirmed on
   the board): a task that has finished puts itself to sleep, and its
   starter then deletes it. If the delete comes while the task is still
   on its way to sleep on the other core, the kernel only marks it, and
   the task's own `vTaskSuspend()` takes it off the list of tasks to
   clear away. The second step made this likely: on the board a wait of
   5 ms was a wait of no ticks at all (the tick is 10 ms), so the loop
   that gives a task back spun and deleted it at once. Two changes in
   the board's adapter: `end()` waits until the task is asleep before
   deleting it (jobs' tasks had the same flaw, unseen), and a wait
   shorter than a tick is one tick.

PC tests could not have shown any of the three: none exists without
FreeRTOS, two cores and a real paint's worth of calls. A loop of 400
launches and closes of Culebra on threads leaked nothing.

## On the board again: the leak fixed, the refills not (2026-11-17)

fa0e885 on the board. The leak is gone (15 rounds, nothing left; the
guess about the delete was right). The three ways of ending a hung
Culebra work; a 300 KB Editor save while Culebra plays is right; at
4 KB the stack keeps 1.4 to 1.5 KB unused. Play costs 91 ms per 10 s
over an idle desktop, against 246 before the fix, 70 with the first
step and about 61 on `main`; a repaint with Culebra open 27.0 ms
against 25.9 and 25.2.

Still failing: a refill missed about every 5 s while Culebra hangs,
though its task had dropped to priority 3. The board thread's reading:
the misses come from the hung state, not from the spinner's time.

The cause now believed (from the firmware's configuration; to be
confirmed on the board): ESP-IDF's own task watchdog watches the idle
task of each core with a timeout of **5 s**
(`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0/1`, `TIMEOUT_S=5`, no
panic). A task that spins at any priority above idle keeps one core's
idle task from ever running; every 5 s the watchdog fires and prints
its report from an interrupt, and that is the refill. Priority 3 could
not help. A late program now goes to the idle priority itself
(`ports::Priority::Idle`), where the kernel gives it and the idle task
turns, so the idle task is fed. A program that is only slow still gets
about half of an idle core.

The same would happen to a job that keeps a core busy for more than
5 s at Low priority (none does today; the longest save takes 4 s and
sleeps on the disk). Noted, not changed.

The cost again: an OS call made inside another (a draw call that asks
for a text's width) no longer goes through `enter()` and `leave()`:
the task counts its depth in a thread-local, and only the outermost
call stores the flag the UI task looks at. Whether what is left over
the first step (it was 21 ms per 10 s of play) is acceptable is the
owner's to say once measured.

## On the board, both steps (the board thread, 2026-11-17)

Three pins were checked before this one was merged.

- **2c3341d** (the limit, 8 KB stack). The hang itself worked, but it was
  not merged. Play cost twice the first step (344 against 169 ms of
  processor time per 10 s; repaint with Culebra open 30.9 against 25.9 ms).
  Refills were missed while Culebra hung (about one every 5 s, 1.7 to 1.8 ms
  late). Closing Culebra sometimes left its task behind (2 in 15 rounds,
  about 8.4 KB each).
- **fa0e885** (thread-local slot, priority 3 while late, `end()` waits).
  The leak was gone and play cost less (91 ms over idle per 10 s), but
  refills were still missed during a hang. On that build the serial port
  showed why: "Task watchdog got triggered ... IDLE1 (CPU 1)" every 5 s,
  each followed by a miss in the log 20 ms later. The thread's theory was
  confirmed.
- **6efd5ff** (a late program at the idle priority; nested calls cost
  nothing), merged with main f8ae1d7:
  - **A hang.** `dbg.py fault hang --pid` froze the desktop once for
    1.99 to 2.00 s, then it answered in 1 ms. Culebra was grey and at
    priority 0. In 30 s quiet and 15 s of VNC while it spun there was no
    watchdog line on the serial port and no missed refill.
  - **Ending it.** Its window's close button, End Task and `kill` from
    telnet each ended it. The task was gone, internal RAM +0 with the
    largest block unchanged, and Culebra opened and played again.
  - **The leak.** 15 rounds of open, start and Alt+F4 left 0 tasks and
    internal RAM +0.
  - **Cost.** Four rounds in one boot. 10 s of play costs 70.5 ms over idle
    (185.4 against 114.9; the first step 70, `main` about 60). A repaint
    with Culebra open takes 26.3 ms (25.97 to 26.38; the first step 25.9,
    `main` 25.1).
  - **Stack.** At 4 KB, never fewer than 1,288 bytes unused (play, the
    hang, the three endings, a 300 KB save).
  - **Saves.** A 2 KB Editor save took 0.5 s; a 300 KB save while Culebra
    played took 4.1 s; both had the right size.
  - **Misses.** `dbg.py misses` found none not caused by the debug link.
  - **Gate.** The PC gate passed 44 of 44 on a second run; on the first,
    `dbg_tool` failed once under parallel load, then passed three times
    alone.
  - **Not checked.** A program that is slow but alive (late for more than
    2 s, then answering): nothing outside the board can make one yet. A
    `dbg.py fault` kind that sleeps for a while would let the next board
    turn check it.

Merged on the owner's earlier acceptance of the first step's cost; what is
left over beyond it is the 0.4 ms of repaint with Culebra open.

## A fault that is only slow (2026-11-17)

Both steps are in `main` (the board thread's results are above). One
check could not be made from outside: a program that is late and then
answers again. `dbg.py fault slow --pid N` makes one: the program's
task is busy for five seconds and then carries on, so with the
two-second limit it is "not responding" for three seconds and must
come back by itself, at its own priority, painted again. Without a PID
(or for a program without a task) the desktop itself is busy for five
seconds, under the watchdog's fifteen.

## On the board: `dbg.py fault slow` (PR 42, f2de5df; the board thread, 2026-11-17)

Merged with `main` 2d7ad53; PC gate 44 of 44; the app alone flashed.

- `fault slow --pid` on Culebra while it played: the desktop answered
  again after 1.99 s; Culebra's board was grey, the Task Manager said "Not
  responding" and its task ran at priority 0. At about 7 s it had answered
  by itself: the title back in the Task Manager, the task back at
  priority 5, the board painted again, and the game went on and took keys
  (1,166 board pixels changed after six keys).
- `fault slow` without `--pid`: the desktop was busy for 5.00 s and went
  on; the 15 s watchdog did not trip, no restart.
- No task watchdog line on the serial port and no missed refill in either;
  `dbg.py misses` none.

The slow-but-alive case left open in the previous entry is now checked.

## More programs on tasks: the ways in (2026-11-17)

**Asked.** "Left: the other programs." Which ones is the owner's choice
and was put to him on a card (the programs but not the dialogs,
recommended; every one, dialogs included; only those that can take long;
leave it at Culebra). This section is the work done on the recommended
answer while the card is open, and all of it is needed by the first three
answers alike.

**What stood in the way.** Culebra was easy: nothing but `launch` calls
into it. The other programs are called by each other, and those calls
were plain function calls, so the callee's code ran on the caller's task:

| Call | From | Into |
|---|---|---|
| `open_folder`, `open_trash` | the desktop's icons, Run, the tests | File manager |
| `open_in_editor`, `open_picture` | File manager (a double click), Run, the desktop | Editor, Picture viewer |
| `open_task_manager` | Ctrl+Alt+Del (the power code) | Task Manager |
| `refresh_folder_windows` (in `files_changed`) | the Trash, the desktop, anything that moves a file | File manager |
| the answer to `ask` | the desktop, which owns the box | whoever asked |

The Open, Save As, text and colour dialogs were looked at and are not in
the table: their windows belong to the program that opened them, so their
events, and with them the callback, already arrive on that program's task.

**Options for the calls.**

| Option | Chosen | Why |
|---|---|---|
| One helper, `in_program(name, callable)`: finds or makes the process, starts its task if the program table says it has one, and hands the callable over through `proc::Table::call` | Yes | One place decides where a program's code runs; a way in reads as before with its body inside a lambda; without tasks (the PC tests, a full pool) it is the same direct call as before |
| Post a message to the program ("open this path") and return | No, not yet | It is what the queue of events will do (the last part of this item), but today the callers use the result at once: the tests and Run expect the window to be there on return |
| Leave them and take the big lock by hand in each | No | The callee's code would still run on the caller's stack and count against the caller's two seconds |

What is handed over is copied (`PathArg` holds a path by value), as
everywhere in this item: a caller that gives up on a late program leaves
nothing behind for it to read.

**One program calling another.** The File manager's task, inside an OS
call, asks for the Editor: the Editor's task takes the turn and the File
manager's task waits for it, as the UI task would, with the same two
seconds. If the Editor calls back into the File manager meanwhile (a save
that makes folder windows read again), that runs where the caller is,
because the File manager's task is waiting: nobody waits for each other.
The worst case for the desktop is two limits in a row (the File manager
late because the Editor was).

**The answer to a question.** `ask` now remembers who asked. The answer
is called as that process, on its task; if the asker has ended
meanwhile the answer is dropped (before, it was called anyway, with a
context that could be gone).

**Stack size per program.** The program table has a `stack` figure next
to `own_task`; `prog::Runner::start` takes it and counts it as the
program's memory. The OS's own code runs on the caller's task, so a
program that lists a folder needs room for the file system too. The UI
task, which ran every program until now, has 16 KB.

**The first tranche** (the agent's choice, to confirm with the card and
with the board's figures):

| Program | Task | Stack |
|---|---|---|
| Culebra, Cactus Patch | yes | 4 KB |
| Editor (each copy), File manager, Picture viewer, Task Manager, Defragmenter | yes | 8 KB, a first figure to trim |
| About, Welcome, Widget gallery | yes | 4 KB (see "The last three" below) |
| Terminal | no | see below |
| Display, Network, Run, Screen Saver, Shut Down, the dialogs | no | the recommended answer leaves them on the UI task |

Eight tasks at most at a time (`kMaxTasks`); a program started beyond
that runs on its caller's task, as before.

**Tested.** On the PC, `test_os` has a round with real threads: the
desktop opens a folder (File manager's task), a key in that window opens
a file (the Editor's task, started from the File manager's, its parent),
Delete asks and the answer comes back to the File manager, which moves
the file and re-reads its windows; the viewer and the Task Manager open
on tasks of their own; closing everything gives the four tasks back.
Thirty runs in a row passed. The gate passes (43 tests). The firmware
compiles and links; **it has not run on the board**, and the stack
figures are guesses until it has.

**For the board.** Unused stack of each program's task after using it
hard (`dbg.py tasks`): the Editor loading and saving a large file, the
File manager in a folder of many files and on the SD card, the viewer
with a PNG and a JPEG, the Task Manager with everything open, a
defragmentation run. Free internal RAM with all of them open. Processor
time and repaint time against `main`. `dbg.py fault hang` and `slow` on
the Editor and the File manager. `dbg.py misses`.

## The last three programs, and the Terminal (2026-11-17)

**About, Welcome and the Widget gallery** were the last windows of the
system's kind outside the desktop itself: three classes in `apps.cpp`
that inherited from the window class and held their buttons as objects.
A program like that cannot have a task (its events do not pass through
the one way in) and could not be loaded from a file either (item 27). They
are now three files (`about.cpp`, `welcome.cpp`, `gallery.cpp`) on the
thin classes, on the `MOVED` list of `tools/check_os_calls.py`, and
`apps.cpp` is only the program table. The reference pictures did not
change by a pixel (the bold name in About is now text the program
paints, since a label has no bold). Each has a task, 4 KB. About is
opened from other programs' Help menus, so its way in goes through
`in_program` like the others; the PC round with threads checks it (the
gallery's Help menu opens About, on a task of its own, as the gallery's
child).

**The Terminal stays on the UI task** (the agent's choice, to confirm).

| Option | Chosen | Why |
|---|---|---|
| Leave the window on the UI task | Yes | The window is a device and nothing else: the shell and every command already run in the session, which has a task of its own on the board. What is left on the UI task is painting a grid and passing keys on |
| A task for the window too | No | A third task on the grid's lock (the UI task, the session, the window) for no work taken off the UI task; and the session's "ended" call comes into the window from the system, one more border to cross |

**Tested.** PC gate (43 tests), the round with threads ten times in a
row. The firmware compiles and links; not run on the board.

**The owner's answer** (the card, 2026-11-17): "Los programas, no los
diálogos", the recommended option, which is what was built above. The
stack figures and the Terminal's exception are still the agent's, to
confirm with the board's measurements.
