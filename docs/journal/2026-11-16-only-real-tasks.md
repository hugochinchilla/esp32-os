# 2026-11-16: Only real tasks are listed as processes

A follow-up to the [process table](2026-11-16-process-table.md) and to
[signals and flags](2026-11-16-signals-and-flags.md). Built on the branch
`claude/project-thread-564n5a`, tested on the PC only. The firmware has not
been compiled with it and it has not run on the board.

## 1. What was asked

The owner asked, in the thread about the message queue, who owns the code that
watches the network sockets. The answer was that nobody does: the UI task
checks the network itself in its loop, and `vncd` and `httpd` in the process
table "are only labels for now". He answered:

> Ok, buy don't advertirse non real tasks then, its confusing

## 2. What was found

The process table had six rows on the board before any program started:

| PID | Name | What stood behind the row |
| --- | --- | --- |
| 1 | `init` | The start-up code. Not a task. The root of the tree |
| 2 | `dbgd` | Nothing of its own: the debug link is fed from the UI loop |
| 3 | `vncd` | Nothing of its own: `remote.poll()` in the UI loop |
| 4 | `httpd` | Nothing of its own: polled by the VNC server |
| 5 | `desktop` | The UI task itself: the window manager and the desktop shell |
| 6 | `netd` | A 250 ms timer on the UI task |

The process table entry had said so ("These rows are honest about what they
are [...] a row is a name and a number, not a FreeRTOS task"), but only in the
journal. On screen, Task Manager with "Show all processes" showed four
services that looked like four things running. The signals work then had to
add a message, "Services cannot be ended yet", for rows that had nothing
behind them to end. `tools/dbg.py tasks` (the real FreeRTOS tasks) and Task
Manager (the rows) told two different stories about the same machine.

## 3. The options, and what was chosen

| Option | What you see | Why / why not |
| --- | --- | --- |
| **A. Remove the four rows** | `init`, `desktop`, then your programs. A service appears on the day it becomes a task | Nothing is listed that is not there. The names and the plan are kept in the documents, not on screen |
| B. Keep the rows, marked "(part of the desktop)" | Six rows as before, four of them with a note | Still four names for one task, and the note needs explaining every time it is filmed |
| C. Keep them only in a debug view | Hidden from Task Manager, shown by a `dbg.py` command | A second list to keep in step, for no user |

**Chosen: A**, by the owner's rule. B was offered to him as the alternative.

Two rows stay, and one of them needs a word:

- `desktop` is the UI task. It is real.
- `init` is still not a task. It stays because it is the root of the tree:
  every process needs a parent, PID 1 was decided to be init, and removing it
  would move every number on the day init becomes a real program (item 18).
  It is the one row left that stands for something that is not a task. If the
  owner wants the rule applied to it too, the desktop becomes PID 1 for now.

## 4. What was built

- `firmware/os/main/main.cpp` and `host/sim.cpp`: the three `spawn_service`
  calls for `dbgd`, `vncd` and `httpd` are gone.
- `core/src/shell/network.cpp`: no `netd` row. The Wi-Fi manager's timer is
  now the desktop's, like the caret and the clock.
- `core/include/core/proc/table.hpp`: the comment on `spawn_service` now says
  it is only for what is a task of its own.
- Nothing else changed. `signal()` still answers NotSupported for a process
  without windows, and Task Manager still has its message for that answer.
  Today no row can produce it: `init` and `desktop` are protected, and
  everything else has windows. It is kept for the first real service.

On the board the table before any program is now `init` 1 and `desktop` 2,
and the first program started is PID 3. The table itself is the same size (32
rows, fixed), so no memory is saved.

## 5. How it was tested, and what was not

On the PC: `test_proc` now checks that after start-up the table holds exactly
`init` and `desktop`, that none of the four names is there, and that the
Wi-Fi manager's timer belongs to the desktop. The Task Manager checks follow
(two protected rows instead of three, then the programs). All 21 tests pass.
No reference picture changed: the picture scenes never had a network or a
debug link.

Not tested: the firmware was not compiled (three lines removed in
`main.cpp`), and nothing ran on the board.

What the board check should look at: Ctrl+Alt+Del, Alt+A shows `init` 1,
`desktop` 2 and the Task Manager 3; Wi-Fi still joins and the tray icon still
follows it (the timer changed owner); the remote screen and the browser page
still answer; `tools/dbg.py` still talks.

## 6. What was learned

- **A list that the user can see is a promise.** The rows were added so that
  later work had somewhere to hang, which is a builder's reason. To the
  person looking at Task Manager they said "four services are running", and
  the first question about one of them ("who owns the watcher?") had the
  answer "nobody, it is a label".
- **The board had already hinted at it.** The item 5 work could not let
  anyone end `vncd`, because removing the row would have left the remote
  screen running. A row that cannot be ended because there is nothing to end
  was the same problem seen from the other side.
- Unix has the same honesty rule in `ps`: a kernel feature that is not a
  thread does not get a line. Kernel threads do, in square brackets, because
  they really are scheduled.

## 7. What is left

- The board check and the merge, by the board thread.
- A service gets its row back in the change that makes it a task: the debug
  link and the console with item 14, the network services with items 18 and
  26. The names from the review (`dbgd`, `netd`, `vncd`, `httpd`) are kept
  for that day. Backlog item 5b (signals for services) now waits for it.
- The owner's word on `init` (see 3).
