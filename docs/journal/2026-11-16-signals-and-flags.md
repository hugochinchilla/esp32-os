# 2026-11-16: TERM and KILL, protected processes, and the ones that come back

Backlog items 4 and 5. Built on the branch `claude/project-thread-564n5a`,
tested on the PC only. It has not run on the board, and the firmware has not
been compiled with these changes (only portable code in `core/` changed).

## 1. What was asked

From the owner's backlog-grooming message (the full text is in the
[architecture review](2026-11-16-architecture-review.md)):

> Me gustaría que los procesos tuvieran un PID de proceso como en Linux, de
> forma que yo pudiera hacer un kill de los procesos, con la opción de si es un
> kill forzado o sugerir a la aplicación que se [cierre]

> el kill en Linux, y que haya diferentes señales, ¿no? Terminación forzosa,
> sugerir un acabado, algo tipo un SIGTERM.

The review turned that into three signals (TERM, KILL, INT), three flags
(system, protected, respawn) and one rule for End Task: ask politely first,
offer force if the program is still there a few seconds later. The owner
approved the list as it stood:

> apruebo todas tal y como están, con sus sugerencias. [...] Adelante con el
> plan.

The task for this branch: make the flags do what they say, add TERM and KILL
"delivered by PID through one function", make End Task use them, grey End Task
out on protected rows, and test it on the PC. INT (Ctrl+C) waits for the
terminal (item 12).

## 2. What was found

- The [process table](2026-11-16-process-table.md) already stored the three
  flags. Nothing read "protected" or "respawn". "System" was already used: Task
  Manager hides those rows until "Show all processes" is ticked.
- **End Task was already a kill.** It closed every window of the process
  without asking, and unsaved text was lost. The polite half existed too, in
  another place: the close button calls the window's `close_requested()`, and
  the Editor answers by putting up "save changes?" and staying open.
- End Task on a process without windows said "It cannot be ended from here
  yet".
- The window manager already had a **safe point**: at the end of every event
  and every tick, when no program's code is running, it removes the window
  programs that have no window left (`Wm::reap`).
- There is **one message box** for the whole desktop. A new message replaces
  the one on screen. It makes itself modal to "the window in use", meaning
  whichever window is active when the question is asked. Both facts shaped
  the design below.
- Buttons could not be greyed out. Menu items could.

## 3. The options, and what was chosen

The big choices (which signals, what they mean, which processes are protected,
polite first and force after) were the owner's, in the review. The ones below
came up while building. They are listed in `docs/DECISIONS.md` as "to confirm"
and each is cheap to change.

### 3.1 Where a signal goes when it is sent

| Option | What it means | Cost |
| --- | --- | --- |
| A. The sender acts at once | `kill(pid)` closes the windows there and then | The sender is in the middle of its own event. The Task Manager can end itself: its button's code would be running inside a window that no longer exists |
| B. A function the window manager registers | The table calls back into the window manager when a signal is sent | Same problem as A, with one more layer |
| **C. The signal waits on the row** | `proc::Table::signal(pid, signal)` checks and sets a bit on the process's row. The window manager looks for waiting signals at its safe point and acts there | One byte per row. A signal takes effect a moment after it is sent, never during |

**Chosen: C.** This is how Unix does it: `kill()` only marks the signal as
pending, and the process meets it the next time the kernel hands control back
to it. It is also the decision already made ("KILL takes effect at a safe
point"), applied to TERM as well so there is one rule. And it is the shape the
shell's `kill` needs later, when the sender is another task: setting a bit is
something another task can do, closing windows is not.

`signal()` is the one function every sender uses. It answers with one of four
results, named after what Unix prints:

| Result | When |
| --- | --- |
| Ok | The signal is waiting on the row |
| NoSuchProcess | No process has that PID |
| NotPermitted | The process is protected |
| NotSupported | The process is a service without windows (see 3.5) |

### 3.2 What TERM does

The decision says "same path as the close button". Three details:

- **Every window of the process is asked**, each once, in the order they were
  opened. The File manager's three folder windows all close. One window
  refusing does not stop the others being asked.
- **A window held by a dialog is not asked.** The program is in the middle of
  a question (Save As, say). Its dialog comes to the front and the program
  stays. This is what Shut Down already did.
- **The window comes to the front before it is asked.** See 6: without this
  the Editor's "save changes?" belonged to the Task Manager. If the program
  refuses without showing anything, the window that was in front before goes
  back in front.

The other option for a program with a dialog open was to close the dialog and
then ask. Not taken: closing a Save As box answers it as Cancel, which is the
program's code running on an answer the user never gave.

### 3.3 What KILL does

Nothing is asked. At the safe point the window manager closes the process's
menus, closes its windows from the top of the stack down (so dialogs go before
the windows they belong to), stops its timers and removes its row. If TERM and
KILL are both waiting, only KILL happens.

One thing is not as clean as on Unix, and it is worth saying on camera. When a
window is closed, its `closed()` function still runs. That is program code
running after the kill. It has to: a program here is a set of window objects
that are made once and used again by the next launch, and `closed()` is where
they put themselves back in order (the Task Manager stops its timer there).
Without memory separation there is no way to throw a program's state away, so
the OS tells the objects they are gone. A program that used `closed()` to open
a window again would be closed again, up to 32 times, and then its row goes
whatever it does. The test has such a program. A real clean kill comes with
the program lifecycle (item 19), when a program is created at launch and
destroyed at exit.

The same function (`Wm::end_process`) is now also how a window program leaves
after its last window closed. There is one place where a process ends.

### 3.4 When End Task offers force

Decided: TERM first, force offered "if the program has not closed after a few
seconds". Open: what "offered" looks like when the program answered TERM with
a question of its own, which is the usual case (the Editor with unsaved text).

| Option | What you see | Why / why not |
| --- | --- | --- |
| A. A box after 5 seconds, whatever is on screen | End Task, the Editor asks "save changes?", and 5 seconds later "End it now?" appears | There is one message box, so ours would replace the Editor's question while the user is reading it. With a Save As dialog open it would land on top while the user types the file name |
| **B. A box after 5 seconds, once the Task Manager is the window in front** | The Editor's question stays as long as the user needs. Back in the Task Manager, with the program still listed, the offer appears. Pressing End Task again on the same program offers it at once | Nothing is interrupted. A program that refuses without a word leaves the Task Manager in front, so there the box appears by itself after 5 seconds |
| C. No box: the button turns into "End Now" | The button's caption changes after 5 seconds | Easy to miss, and it loses the Windows 95 moment of a box that says the program has not closed |

**Chosen: B**, with 5 seconds (`TaskManager::kGraceUs`). The box says:

> Editor (PID 17) has not closed.
> It may be waiting for an answer from you.
>
> End it now? Changes that were not saved will be lost.

with **End Now** and **Cancel**. End Now sends KILL. Cancel leaves the program
alone, and the offer is not repeated until End Task is pressed again.

An honest note: Windows 95 offered force when a program stopped answering its
messages. Chinchilla cannot detect that, because a window program that hangs
takes the whole UI task with it, the Task Manager included. So "has not
closed" here means what it says: still in the table 5 seconds later. A hung
program is the watchdog's job (item 7), and becomes Task Manager's when
programs get their own tasks (item 26).

### 3.5 Signals and the services

`netd`, `vncd` and `httpd` are rows without windows, and not protected. What
should a signal do to them?

| Option | Result |
| --- | --- |
| **A. Refuse for now** | `signal()` answers NotSupported. Task Manager says "is a service without windows. Services cannot be ended yet." |
| B. Remove the row and its timers | True for `netd` (its 250 ms timer is all that drives it). A lie for `vncd` and `httpd`: they are polled from the UI loop in `main.cpp`, so the remote screen would keep working with its row gone |
| C. Give each service a stop function now | `kill 4` really stops the remote screen until the next start. Needs changes in `firmware/os/main/main.cpp` and `host/sim.cpp`, which the message-queue branch is rewriting this week, and a real board check of each service |

**Chosen: A**, because item 5 is "TERM and KILL for window programs" and B
would show something false. C is the right end state and is now backlog item
5b; it is small once the services are real (after items 8 and 18).

### 3.6 Protected

Decided: the user cannot signal a protected process. `signal()` refuses with
NotPermitted, for both signals, whoever asks. Today that covers `init`, `dbgd`
(the debug link) and `desktop`. Task Manager greys End Task out when such a
row is picked: the caption is etched, like a greyed menu item, and the mouse,
the keyboard and Alt+E do nothing.

Protection is about signals only. A protected window program could still be
closed by its own close button (the test has one). There is no such program
today.

### 3.7 Respawn

"Started again if it ends." Two ways to build it:

| Option | Result |
| --- | --- |
| A. Wait for init (item 18) | init will read a start-up file and start things; restarting is its job. Until then the flag does nothing |
| **B. A start function on the row now** | `Table::set_restart(pid, start, context)` sets the flag and remembers how to start the program. When the process ends, for any reason, the function goes on a list. At the safe point `Table::restart_due()` runs the list with init as the current process, so the new process is init's child with a new PID |

**Chosen: B.** It is small, it can be tested on the PC today, and init will
use it as it is. The restart is not done inside `exit()` but at the safe
point, for the same reason signals wait: no program code runs in the middle of
taking a process apart.

**Nothing on the real desktop has the flag yet.** The processes the review
marked respawn are the serial console, which does not exist, the debug link,
which cannot be ended, and the taskbar, which is not a process until the
window manager is split (item 21). So on the board there is nothing to kill
and watch come back. The tests do it with a test program.

Not built: Unix's "respawning too fast" brake. A program that ended the
instant it started would be restarted on every tick (100 times a second on
the board). The desktop would stay alive, since each restart is a separate
safe point, but busy. It needs a clock and belongs with init.

## 4. What was built

- `core/include/core/proc/table.hpp`, `core/src/proc/table.cpp`: `Signal`
  (Term, Kill), `Sent`, `signal()`, `take_signals()`, `signals_waiting()`,
  `set_restart()`, `restart_due()`. A row grew by a pending-signals byte and
  the start function with its context; the table is about 1.5 KB on the board
  (it was about 1.2 KB).
- `core/src/wm/wm.cpp`: the safe point now delivers signals, then removes
  what has ended, then restarts what comes back, and paints once more if
  anything changed. `ask_to_close()` is TERM, `end_process()` is KILL.
- `core/src/wm/widgets.cpp`: `Button::set_enabled()`.
- `core/src/shell/task_manager.cpp`: End Task sends TERM and watches; the
  offer; the greyed button; the list is read again at the next tick after a
  signal instead of up to a second later.

Shut Down was left alone. It asks each program to close in its own loop,
because it needs the answer at once to decide whether to go on.

## 5. How it was tested, and what was not

On the PC, with the address and undefined-behaviour checkers on. All 21 tests
pass.

- `test_proc`, the table alone: the four answers of `signal()`, a signal
  waiting until taken, two signals on one row, a process ending with one
  waiting; restart only at `restart_due()`, as init's child, once per ending,
  again the next time; the flag without a function; ten endings against a
  list of eight.
- `test_proc`, a window manager with test windows: TERM asks as the owner and
  only at the safe point; a refusal; every window asked once; a window held by
  a dialog not asked; KILL with a dialog, a timer and an open menu; TERM and
  KILL together; the program that reopens its window five times; a signal
  sent from inside an event; a protected window program; respawn after KILL
  and after a polite close, and no respawn once the flag is cleared.
- `test_proc`, the whole shell: End Task greyed on init, dbgd and desktop
  (the button's pixels differ, Alt+E and a click do nothing); the message on
  `netd`; a click on End Task for a program; the Task Manager ending itself.
- `test_system`: End Task on the Editor with unsaved text shows its question;
  Cancel keeps it; no offer for 7 seconds while the Editor is in front; the
  offer when the Task Manager comes back; Cancel on the offer and no repeat;
  nothing at 3 seconds and the offer at 6; End Now, and the file on disk
  without the unsaved text; a Save As dialog brought forward, then End Task
  twice and End Now; a program with nothing to ask closing at once.

Each new group was checked by breaking the code on purpose (protection off,
the button never greyed, KILL treated as TERM, and so on) and seeing tests
fail.

Three new reference pictures: `34c-task-manager-protected.png`,
`34d-end-task-asks.png`, `34e-end-task-force.png`. No existing picture
changed.

Not tested:

- **Nothing has run on the board**, and the firmware was not compiled.
- Respawn has no user on the real desktop, so the board cannot show it.

What the board check should look at: Ctrl+Alt+Del, Alt+A, pick `init`, then
`dbgd`, then `desktop`: End Task goes grey each time. Pick `netd`: End Task
gives the "services cannot be ended yet" message. Type something in the
Editor, End Task on it: its question appears. Cancel, Ctrl+Alt+Del: within
about 5 seconds of the first End Task the "has not closed" box appears. End
Now: the Editor goes and the caret and clock carry on.

## 6. What was learned

- **The Editor's question belonged to the Task Manager.** The first version
  called `close_requested()` on the Editor from the safe point. The Editor
  asked "save changes?", and the message box made itself modal to the window
  in use, which was the Task Manager, because nobody had clicked the Editor.
  Escape on the question went back to the Task Manager with the Editor's
  state half way through closing. A click on a close button activates the
  window first; TERM has to do the same to be "the same path".
- **One message box is a real limit.** It decided option B in 3.4, and it is
  why the offer, when it does appear, takes the place of a "save changes?"
  still on screen. That is acceptable there, since the user came back to the
  Task Manager to force the program out.
- **After a signal, the sender knows nothing yet.** The old End Task closed
  the windows and refreshed its list in the same breath. Now the click only
  leaves a note; the windows close after the click. The Task Manager's list
  showed the dead program for up to a second until it was told to look again
  at the next tick. Asynchronous is the honest word for it, and `kill` on
  Linux behaves the same: it returns before the process has died.
- **A kill here still runs the victim's code**, in `closed()`. See 3.3.
- **Windows 95 protected nothing, and NT and Unix did.** In 95 the Close
  Program box would end whatever was picked, and picking the wrong thing took
  the machine down. Chinchilla now sits on the NT side of that line, with a
  greyed button to show for it.

## 7. What is left

- The board check and the merge, by the board thread.
- To confirm with the owner: 3.2, 3.4 (asked on a card), 3.5 and 3.7.
- Item 5b: signals for the services, each with a stop function.
- INT (Ctrl+C) with the terminal (item 12), and `kill` in the shell (item
  13), which calls `proc::Table::signal` and prints its four answers.
- KILL also cancels jobs and frees tagged memory once those exist (items 10
  and 3).
- The brake on a program that restarts too fast, with init (item 18).
- The table still has no lock. `signal()` is the first function another task
  will want to call; it needs the lock, or the bit set atomically, before the
  shell's `kill` runs on its own task.

## On the board (added by the board thread, 2026-11-16)

The first firmware compile was the board thread's, with main merged in
(84688f2, the file system lock): no errors or warnings. Flashed with a fast
start and checked:

- Ctrl+Alt+Del, Alt+A: End Task is greyed out with init, dbgd and desktop
  selected, and active with netd and the Task Manager.
- End Task on netd: "netd (PID 6) is a service without windows. Services
  cannot be ended yet."
- End Task on the Editor (PID 8) with unsaved text: the Editor comes forward
  with its question. Cancel, then Ctrl+Alt+Del: the "has not closed" box
  ("Editor (PID 8) has not closed. It may be waiting for an answer from
  you.") with End Now and Cancel. Timed in a second run: 5.2 s after End Task.
  When more than 5 s had passed, it appeared as soon as the Task Manager came
  back.
- End Now: the Editor and its question went, its row went, the file on disk
  kept its old text. Afterwards a new Editor opened (PID 12), its caret
  blinked, the clock changed with the minute, K held for 1 s typed 17 letters,
  and File > Save left the title clean.
- Free internal RAM 101,699 bytes against 102,059 on main (360 bytes less);
  repaint of the empty desktop 14.32 ms against 14.06 ms; video 0 late, 0
  missed refills.

Respawn has no user on the real desktop yet, so the board could not show it.

