# init starts again what ends

2026-11-17. Backlog item 39.

## What was asked

The owner killed the taskbar and asked what brought it back and where the
count of its endings was kept. The answer was: the process table. A process
could be marked "respawn" with a function to start it again; the table kept
a list of what was due, counted quick endings (five in a row under two
seconds: it gave up) and the window server ran the list at its safe point.
init only wrote the "gave up" line in the log.

He then asked for a backlog item to move that into init, "Unix inittab
style", so that `startup.txt` says what comes back, and chose it as the
next piece of work ahead of item 37.

## The options

**How the file says it.** Put to the owner on a card. The work went ahead
on the first, the one recommended, while he decided; he chose the third,
and the file was changed to it the same morning.

| Option | The file | For | Against |
| --- | --- | --- | --- |
| 1. A new verb (built first) | `respawn taskbar` beside `start desktop` and `run terminal` | Fits the file as it was; an old file still works | Not the look of inittab |
| 2. The same, and for programs too | `respawn run terminal`: a program that comes back when closed (a kiosk) | A good trick to show | init must follow the process of a program it did not make itself; more to build |
| 3. inittab's fields (chosen by the owner) | `taskbar:respawn` | The historical look | Changes the format of a file that is already on the board |

What the third became, the details being the agent's and to confirm:

- Two fields, `WHAT:ACTION`. inittab has four (`id:runlevels:action:process`);
  there are no runlevels here, and an id beside the name would say the
  same thing twice.
- ACTION is `once`, `respawn` or `off`. `off` came with the fields: it
  is inittab's, and it says "the console is not started" better than a
  line turned into a comment.
- WHAT is a part of the system by its name, or else a program or a file as
  Start > Run... takes it. The action is after the last colon, so
  `C:\Documents\notes.txt:once` keeps the colon of its drive.
- The answer to "it changes the file on the board": the lines of the first
  form (`start desktop`, `run terminal`) are still understood. Nothing has
  to be edited on the board; a file that is deleted is written again in
  the new form. The verb `respawn` of option 1 never left this branch and
  is gone.
- The desktop cannot be `off` (a machine with nothing on its screen and
  no way to say so). The taskbar can.
- A program cannot be `respawn` yet (option 2): init says so in the log.

**Where the rule lives** (the agent's choices, to confirm):

- *All of it in init (chosen).* The table keeps one thing: it tells a
  supervisor of every ending (`proc::Supervisor`), and passes on the two
  questions the window server asks at its safe point (is anything waiting,
  start it). init is that supervisor. The mark on a row (`R` in `ps`) is
  only what init wrote there to be seen.
- *The rule stays in the table and init only reads the file.* Less to
  move, but then two places know what a restart is, and the file's word
  would be passed on as a flag. Not what was asked.
- *init on a task of its own, waiting for endings* (Unix: `wait()`). It is
  how the real thing works and what item 26 will bring; today everything
  that draws runs on the UI task, and a taskbar started from another task
  would break that rule.

**How init knows a thing has ended.** A thing that can come back must make
a process with its own name each time it starts ("taskbar" makes the
process `taskbar`); init looks the name up after starting it and watches
that number. Chosen over changing every start function to hand back a
number: the three machines (board, simulator, desktop program) and the
tests keep their start functions as they are.

**Old files.** The `startup.txt` on the board has no taskbar line. The
taskbar is "needed", as the desktop is: started even if no line names it,
and for it the usual way is to come back. So: no line, or
`taskbar:respawn`: it comes back. `taskbar:once`: started once; ended, it
stays away until `run taskbar`. `taskbar:off`: not started at all.

## What was built

- `core/proc`: `set_restart`, the list of what is due, the count of quick
  endings and the give-up notice are gone from the table (125 lines out, 39 in).
  In their place `proc::Supervisor` and `Table::set_supervisor`.
- `core/init`: init is the supervisor. `WHAT:ACTION` lines in the file;
  `add()` says whether a thing can come back (`Again::No`, `Can`, `Does`);
  the brake (2 s, 5 times) is init's and is kept for each thing, not for
  each start function; `start_again()` starts a thing by hand with all its
  tries; things may be added while the file is being done. The log says
  "init: taskbar ended; starting it again" each time.
- `core/shell`: the desktop hands its taskbar to init when it starts, and
  init starts it at its line. The taskbar is init's child now, not the
  desktop's (`ps` shows PPID 1). `run taskbar` goes through init. On a
  machine with no init (most tests) the taskbar is started as before and
  does not come back.
- The usual `startup.txt` is `desktop:once`, `taskbar:respawn`,
  `inetd:once`, `console:off`, with the form explained at its top.
- `inetd:respawn` or `console:respawn`: started once, with a line in the
  log saying it cannot come back. Their threads were not written to be
  started twice.

## How it was tested

- `test_init`: the fields (parts, programs, a path with its colon, `off`
  before the desktop has brought the taskbar, the desktop refusing `off`,
  six kinds of wrong line each with its reason, the usual file); the old
  verbs, in the tests that were there before; `respawn` and `once` for
  things that can, could and
  cannot come back; the child of init, the mark, back at the safe point
  and not before; a needed thing with no line (the old file); a thing
  added by the desktop while the file is done, with its line before, after,
  as `start`, or missing; started by hand before the file; the brake as
  before, now per thing, and five tries again after a start by hand.
- `test_proc`: the table tells a supervisor of every ending with the row
  whole and starts nothing itself; the real desktop without init (the
  taskbar stays away), then with init (`run taskbar` hands it over, it
  comes back, five quick kills and it gives up, `run taskbar` again).
- All 41 PC tests pass. One reference picture redrawn: the `ps` listing,
  where the taskbar's parent is 1.
- The firmware compiles and links (0x1615a0 bytes). It has not run on the
  board.

## For the board

With the file as it is on the board (the old form, no taskbar line): the
bar is there after start-up, `ps` shows it with PPID 1 and `R`, End Task
brings it back, `dmesg` has "taskbar ended; starting it again". Five quick
kills: it stays away, `run taskbar` brings it back. Then `taskbar:once`
in the file and a restart: ended, it stays away. `taskbar:off`: no bar at
all until `run taskbar`. `taskbar:respawn`. The file deleted: the usual
one is written in the new form. `inetd:respawn`: started once, said in
the log, no start-up box.
