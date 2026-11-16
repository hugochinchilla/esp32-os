# 2026-11-16: The process table, and PIDs for everything that runs

Backlog item 2, the first piece of "know what is running". Built on the branch
`claude/project-thread-9cvne3`, tested on the PC only. It has not run on the
board.

## 1. What was asked

The idea comes from the owner's backlog-grooming message (the full text is in
the [architecture review](2026-11-16-architecture-review.md)):

> Me gustaría que los procesos tuvieran un PID de proceso como en Linux, de
> forma que yo pudiera hacer un kill de los procesos [...] en Linux pues hay un
> PID número 1 y todo es hijo de él.

The review answered with a process table as the piece "everything else hangs
off", and the owner approved the plan as it stood:

> apruebo todas tal y como están, con sus sugerencias. [...] Empieza poquito a
> poco, no intentes hacer todo a la vez [...] Adelante con el plan.

The task for this branch, as handed over: a process table in the portable
core, with PIDs counting from 1, a parent, a name, a state and a flags field;
an owner PID on each window and timer in the window manager; `launch()` makes
a row and closing the last window removes it; the services that already exist
(VNC, web, Wi-Fi manager, debug link) get rows; Task Manager lists processes
with their PIDs; tests on the PC.

`kill`, protected processes that really refuse, per-process time and memory,
and init as a real program are later items (5, 4, 3 and 18). This one only
makes the list they all need.

## 2. What was found

How it worked before, with the places in the code:

- A program was a row in a table of three things: name, icon, and a function
  that opens its window (`core/src/shell/apps.cpp`). `launch()` was one line:
  `app.open()`.
- The window manager (`core/src/wm/wm.cpp`) kept up to 16 windows and 16
  timers. A window had an id; a timer had a function, a pointer and a due
  time. Neither said who it belonged to.
- Task Manager's list, "Programs running", was the list of windows that have
  a taskbar button, minus itself.
- Several ways into a program do not go through `launch()`: a double click on
  a text file calls `open_in_editor()` directly, the desktop's Documents icon
  calls `open_folder()`, Ctrl+Alt+Del calls `open_task_manager()`, and the
  Settings menu opens the Screen Saver and Network windows.
- Dialogs are shared objects. There is one message box, one Open/Save dialog,
  one text question. Whoever asks gets it.
- The services are not tasks. The VNC server is polled from the UI loop
  (`remote.poll()` in `firmware/os/main/main.cpp`), the debug link is fed from
  the same loop, the web gate is polled by the VNC server, and the Wi-Fi
  manager is driven by a 250 ms timer that the shell starts
  (`core/src/shell/network.cpp`). Everything runs on the one UI task.

So the question behind the whole piece was: when a window opens or a timer
starts, **how does the OS know which program asked?** Nothing in a call such as
`manager().add(window)` or `start_timer(...)` says who is calling.

## 3. The options, and what was chosen

The big choice (what a process is: a row that owns things, with window code
staying on the UI task) was the owner's, in the review. The choices below came
up while building. They follow the decisions already made where those reach;
where they do not, they are listed in `docs/DECISIONS.md` as "to confirm", and
each is cheap to change.

### 3.1 How the OS knows who is asking

| Option | What it means | Cost |
| --- | --- | --- |
| A. Every call names its PID | `add(window, pid)`, `start_timer(pid, ...)`, and so on | Every program and every dialog changes, and a program can lie or forget |
| B. Programs register what they own | Each program tells the table "this window is mine" after opening it | Easy to forget; shared dialogs would need it at every use |
| **C. A "current process"** | The table knows which process the UI task is working for right now. The window manager sets it before every call into a program and puts it back after. A window or timer made meanwhile belongs to the current process | About 20 places in the window manager, one line each. Programs do not change |

**Chosen: C.** It is what a real kernel does: on Linux, "current" is the
process whose code the processor is running, and every system call is charged
to it without the program saying who it is. Here the window manager plays the
kernel's part, because it is the only thing that calls into programs: for a
key, a click, a paint, a timer, a menu choice, a close request. It wraps each
of those calls in a small object (`proc::Scope`) that sets the current process
and restores the previous one when the call returns, so calls can nest.

A was not taken because it changes every program for no gain, and B because it
depends on nobody ever forgetting. C also gives the later items their hook for
free: time spent inside a call can be charged to the current PID (item 3), and
a large memory block can be tagged with it.

Two details that had to be right:

- **Menus.** A menu stays open across several events, and the choice comes
  from a key or a click handled "by the desktop". So each open menu remembers
  the process it was opened for, and its action runs as that process. A menu
  bar's menu belongs to its window's owner; the Start menu belongs to the
  desktop; a submenu inherits from its parent menu.
- **The window manager's own timers.** The caret's blink timer is restarted
  whenever a program moves the caret, which happens inside the program's call.
  With the simple rule it would have become the program's timer, and died with
  it. The manager's own timers (caret, clock, key repeat, menu delays) are
  therefore always the desktop's, whoever is current.

### 3.2 Which process a program's entry points belong to

`launch()` now does two things: finds or makes the program's process, and
makes it current while the program opens its window. The entry points that
skip `launch()` got the same one line (`AsProgram as("Editor");`), so a file
opened from a folder window lands in the Editor's process, not in the File
manager's.

| Question | Chosen | The other option, and why not |
| --- | --- | --- |
| One process per program, or per window? | **Per program.** The File manager's folder windows are one process; so is the Trash window, which is the File manager's code | Per window: there is still only one Editor object, so two "processes" would share one program's state. More than one instance is backlog item 19 |
| Who is the parent? | **Whoever launched it.** The desktop for the Start menu and icons; the File manager for a file it opens in the Editor | Always the desktop: simpler, but the tree would say nothing |
| What happens to children when the parent ends? | **init adopts them**, as on Unix | Ending the children too: closing a folder window would close the Editor with your text in it |
| The Screen Saver and Network settings windows | **Their own processes** ("Screen Saver", "Network"), so they show in Task Manager and can be ended there, as before | The desktop's: they would vanish from the default list |
| The Shut Down dialog, and message boxes the desktop shows | **The desktop's** | Own processes: they are the desktop talking |
| A shared dialog (message box, Open, Save As) | **Belongs to whoever asked**, each time it opens | A dialog process: then End Task on the Editor would leave its Save As behind |

### 3.3 When a process ends

| Option | What happens |
| --- | --- |
| A. The moment its last window closes | The Picture viewer breaks: started from the menu, it shows the Open dialog first; choosing a file closes the dialog (its only window) and then opens the picture window. The process would end in between |
| B. The program says when it is done | Every program needs a new call, and one that forgets stays in the list for ever |
| **C. At the end of the event or tick** | Closing the last window marks the process "exiting". When the window manager has finished the current event, any window program that still has no window is removed. If it opened another window meanwhile, it is back to "running" |

**Chosen: C.** This is the first use of the "safe point" idea from the review:
things that end a process happen between events, never in the middle of one.
KILL (item 5) will use the same place.

When a process is removed, the window manager also stops any timer it left
running. No program today leaves one (the test opens and closes all ten and
checks), but it is the first small piece of "the OS releases what the process
owned".

Only window programs end this way. A row carries a fourth flag, "windowed",
that says so. The desktop and the services have no windows and stay.

### 3.4 PID 1

The decision already says "init is PID 1". There is no init program yet (item
18). Three ways to deal with that:

| Option | Result |
| --- | --- |
| A. Give PID 1 to the desktop for now | Numbers move when init arrives, and the films would show two different trees |
| B. Leave 1 unused | A tree without a root; parent 0 would mean "nobody" |
| **C. An "init" row now** | PID 1 exists from the first moment, system and protected, parent of the services and the desktop. Today it stands for the start-up code. Item 18 gives it a start-up file and real work |

**Chosen: C**, because it matches the decision and keeps the numbers stable.
The table makes init by itself the first time anything is started, so there is
no order to get wrong.

### 3.5 The numbers

PIDs count up from 1. A number is not given out again until the count has gone
round: after 32767 (the classic Linux limit) it starts again at 2, skipping
numbers still in use. PID 1 is never reused and init cannot be ended.

The table has 32 rows and takes about 1.2 KB of internal RAM. A name is up to
23 characters. There is no heap use.

### 3.6 The services

The four that exist get rows where they start, as children of init with the
system flag:

| PID on the board (expected) | Name | What it is | Flags |
| --- | --- | --- | --- |
| 1 | `init` | The start-up code, for now | system, protected |
| 2 | `dbgd` | The debug link | system, protected |
| 3 | `netd` | The Wi-Fi manager and the clock from the network | system |
| 4 | `vncd` | The remote screen | system |
| 5 | `httpd` | The viewer page for a browser | system |
| 6 | `desktop` | The window manager and the desktop shell, until they are split (item 21) | system, protected |

`netd` is missing when the board has no radio or Wi-Fi is switched off, and
`vncd` and `httpd` when their ports could not be opened. The numbers above
assume all four start; the board will tell.

The names are the ones from the tree in the review (Unix style, a "d" for
daemon). The other option was plain names ("Remote screen"); they would read
better in Task Manager and worse in `ps` and `kill`, which are coming.

**These rows are honest about what they are.** The services still run on the
UI task; a row is a name and a number, not a FreeRTOS task. `netd` really owns
its 250 ms timer, which the tests check. The rows become real tasks when
services move off the UI task (phase 2).

The protected flag is stored as the decision lists it (debug service, init,
the window server). Nothing enforces it yet: that is item 4, with the respawn
flag.

### 3.7 Task Manager

| Option | What you see | Why / why not |
| --- | --- | --- |
| A. Every process, always | init, dbgd, netd, vncd, httpd, desktop, then your programs | The list shows five rows, so the programs would start below the fold |
| B. Two tabs, Programs and Processes | As on Windows NT | There is no tab widget yet |
| **C. Programs by default, a "Show all processes" box** | The programs with windows, as before, now with PID and name. Ticking the box shows the whole table | The decision already says system processes are hidden in the default view |

**Chosen: C.** Each row is PID, process name and the title of its main window
(or "(system)"). The default list leaves out Task Manager itself, as it did
before; with the box ticked it lists itself too, and can end itself.

- **Switch To** brings the process's main window forward.
- **End Task** closes every window the process owns, dialogs first, without
  asking to save, as before. The process then leaves the table by the rule in
  3.3.
- **End Task on a process without windows** (a service, init) shows "has no
  windows to close. It cannot be ended from here yet." Ending those needs
  signals (item 5) and the protected flag (item 4).

The window grew by 32 pixels in height for the column headings and the box.

## 4. What was built

- `core/include/core/proc/table.hpp`, `core/src/proc/table.cpp`: the table.
  `spawn`, `exit`, `find`, `find_by_name`, the current process, and
  `proc::Scope`. Portable, no ESP-IDF headers, no heap.
- `core/src/wm/wm.cpp`, `wm.hpp`, `window.hpp`: `set_processes()`, an owner
  PID on every window, timer and open menu, the scopes around every call into
  a program, `windows_of()`, `timers_of()`, `main_window_of()`,
  `timer_owner()`, and the reaping at the end of each event and tick. Without
  a table set, the window manager behaves exactly as before.
- `core/src/shell/`: `launch()` and the other entry points make their process
  current; `shell::start()` makes the desktop's process and hands the table to
  the window manager; `start_network()` makes `netd`; Shut Down asks each
  program to close as that program, so the "save changes?" question is the
  Editor's.
- `core/src/shell/task_manager.cpp`: the list described above.
- `firmware/os/main/main.cpp` and `host/sim.cpp`: rows for `dbgd`, `vncd` and
  `httpd` where those start.
- The debug link's window list (`tools/dbg.py windows`) now gives each
  window's `pid`.

## 5. How it was tested, and what was not

On the PC: `host/tests/test_proc.cpp`, a new test program with about 120
checks in three parts.

1. The table alone: init is 1, numbers count up, parents, names cut to fit,
   state and flags, children handed to init, a full table, scopes that nest,
   and a run of 32,764 short-lived processes to see the numbers wrap to 4
   while 1, 2 and 3 are still alive.
2. A window manager with two test windows: who owns each window and timer;
   that paint, keys, the mouse, a timer, a menu action, the close request and
   `closed()` each run as the owner; that the process goes with its last
   window and its timer stops; the dialog hand-over of 3.3; and that a window
   manager without a table records nothing and breaks nothing.
3. The whole shell: the rows after start-up, a launch from the Start menu
   (parent: the desktop), a text file opened from the File manager (parent:
   the File manager, then init when the File manager closes), the Editor's
   dialogs, Shut Down's question, Task Manager's End Task, Switch To and "Show
   all processes", and every program in the table opened and closed with
   nothing left behind.

The existing Task Manager test (`test_system`) passes unchanged, which says the
default list behaves as it did. All 21 PC tests pass, with the address and
undefined-behaviour checkers on. One reference picture changed
(`34-task-manager.png`) and one is new (`34b-task-manager-all.png`).

The firmware compiles and links in the cloud container with ESP-IDF v5.5
(`esp32-os.bin` is 0x143240 bytes, 68 % of the app partition free). That says
nothing about how it runs.

Not tested:

- **Nothing has run on the board.** The expected PIDs in 3.6 are a prediction.
- The cost of the scopes on the board. Each is two assignments around a call,
  so it should not be measurable, but nobody has measured.

What the board check should look at: Ctrl+Alt+Del, tick "Show all processes"
(Alt+A), and compare the rows with the table in 3.6; open and close a few
programs and watch the PIDs count up; `tools/dbg.py windows` shows a `pid` for
each window; the caret still blinks and the clock still ticks after a program
has been closed.

## 6. What was learned

- **A name can be one character too long.** The first version stored 15
  characters of the name. "About Chinchilla" has 16. The stored name would
  never have matched the program's name again, so every launch would have made
  a new process. Names now have room for 23, and the lookup is tested with the
  longest one.
- **The caret would have died with the first program closed.** See 3.1: the
  blink timer is restarted from inside programs. This is the kind of bug the
  "current process" idea invites: anything the OS does on its own behalf while
  a program is current must say so.
- **One label too many moved a keyboard shortcut.** Alt+P goes to "the widget
  after the label with the underlined P". Adding the column-headings label
  between the heading and the list sent Alt+P to a label that cannot take the
  focus. The test that drives Task Manager by keyboard caught it.
- **The Picture viewer decided the exit rule.** "A process ends when its last
  window closes" sounds complete until a program's only window is a dialog
  that is about to be replaced.
- **Windows 95 had this table and hid it.** Its Close Program box listed
  window titles, as ours did until today. The numbers were there inside the
  system all along; NT's Task Manager was the first to show them. Chinchilla
  now does both: titles by default, the Unix view behind one tick.

## 7. What is left

- The board check and the merge, by the board thread.
- Item 3: time and memory per process. The scopes are where the stopwatch
  goes.
- Item 4: make "protected" refuse and "respawn" restart. The flags are stored.
- Item 5: TERM and KILL. End Task on a service has nothing to send yet.
- Item 18: init as a real program.
- The table is used by one task only and has no lock. That is true today
  because everything runs on the UI task. The first item that starts a process
  from another task (jobs in item 10, shell sessions in item 13) must give the
  table a lock and make "current" a per-task value.
- To confirm with the owner: the choices in 3.2 and 3.6 (the names). He
  confirmed 3.7 the same evening, asked with the three options and the two
  pictures: programs by default, plus the "Show all processes" box.
