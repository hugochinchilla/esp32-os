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


## On the board (added by the board thread, 2026-11-17)

Branch commit f0cd0b0 with main 80948eb merged in (clean). PC tests 42 of 42,
firmware 0x161d10, flashed by cable, the app only. The board's
C:\.config\startup.txt (the earlier default, old form, no taskbar line) was
saved first and put back byte for byte at the end. Each case below is one
restart with that file; processes read with `ps` over telnet, the log with
`dbg.py dmesg`.

1. The board's own file (old form): the bar is there; `ps` shows taskbar PID
   3, parent 1, flags S-R-; the log says "init: taskbar goes before inetd",
   "init: started taskbar", "0 lines failed"; no box. `kill` on it: back as
   PID 6 with "init: taskbar ended; starting it again".
2. Killed again and again: after the fifth quick end it stayed away with
   "init: taskbar keeps ending as soon as it starts; not started again" (no
   bar). `run taskbar` brought it back (PID 12, R), and one more kill after
   that brought it back again (PID 13): its tries start again.
3. `desktop:once`, `taskbar:once`, `inetd:once`: taskbar without R; killed,
   it stays away (no bar); `run taskbar` brings it back (no R).
4. `taskbar:off` as the first line, then the old lines: no bar after the
   start, "init: taskbar is off", "0 lines failed". `run taskbar` starts it;
   it then has R (it is started as the respawn kind).
5. `taskbar:respawn`: killed, it came back with a new PID.
6. `inetd:respawn`: "init: inetd cannot be started again when it ends;
   started once", inetd started, "0 lines failed", no box.
7. `inetd:twice`: the box "Start-up" after the start; the log says "not
   understood: inetd:twice (once, respawn or off after the colon)" and "1 line
   failed".
8. `terminal:once` as the last line: a Terminal is open after the start.
9. No file: written again as `desktop:once`, `taskbar:respawn`,
   `inetd:once`, `console:off` (with its comments).
10. `console:once`: the serial console runs (row console, SP). `console:off`:
    no console row. An Editor started with `run editor` and killed does not
    come back. `kill 1` and `kill 2`: "it is protected".
11. `dbg.py misses`: no missed refill not caused by the debug link. VNC took
    a full screen, telnet answered. Free RAM on the idle desktop: internal
    206,131 bytes against 206,159 on main, external 6,471,144 against
    6,472,080. No late or missed refill in 60 s.

The single painted frame without the bar between an end and the restart was
not filmed again in this turn; the earlier turn (the taskbar as a process)
showed that restart is not visible to the eye.

## The same day: the old form taken out

Checked on the board and merged with the old lines still understood. The
owner then wrote: do not support old formats, rewrite the file on my
board; there are no releases, the board's data can be wiped whole, C:
included; keep the saved Wi-Fi networks if that is free; simplify the
parser.

So the branch that read `start NAME` and `run WHAT` is gone from init:
a line is `WHAT:ACTION` or it is not understood. One thing changes with
it: a name init does not know is no longer "nothing here is called
that" but a program to run, since the fields do not say which of the two
a line means. The simulator's start-up text and the serial console test
moved to the new form. The rule is in `CLAUDE.md` and `docs/DECISIONS.md`
("No backward compatibility") for every thread.

Tested: `test_init` with every line in the new form and `start desktop`
as one of the lines that is not understood; all 41 PC tests pass, the
pictures match. The firmware compiles and links (0x169640 bytes, with
what `main` gained since). It has not run on the board. On the board the
file has to be rewritten by hand or deleted, or the start-up box will
count its lines: asked of the board thread.



## On the board, only WHAT:ACTION (added by the board thread, 2026-11-17)

Branch commit f3193a2 with main 6433aa0 merged in (clean). PC tests 42 of
42, firmware 0x167a10, flashed by cable, the app only. Nothing but
C:\.config\startup.txt was changed on the board: no erase, NVS and the rest
of C: untouched.

1. The first start after the flash, with the board's old file (the earlier
   default: `start desktop`, `start inetd` and comments): the box "Start-up"
   after the start; the log says "not understood: start desktop", "not
   understood: start inetd", "no line starts desktop; started anyway", "no
   line starts taskbar; started anyway", "2 lines failed". Desktop and bar up;
   no network services (ports 23, 5900 and 80 refused, chinchilla.local did
   not resolve).
2. The file deleted and a restart: init wrote it again in the new form
   (`desktop:once`, `taskbar:respawn`, `inetd:once`, `console:off`, with its
   comments and `# terminal:once`); no box; the log has "started desktop",
   "started taskbar", "started inetd", "console is off" and no "not
   understood"; telnet, VNC and the web page answer.
3. A file with `start desktop`, `taskbar:respawn`, `inetd:once`: the box,
   "not understood: start desktop", and the desktop still came up. One line
   of the log reads oddly: "init: taskbar goes before taskbar" (after
   "desktop goes before taskbar"); harmless, but its wording could be
   looked at.
4. `editor:once` and `frobnicator:once` added: an Untitled Editor opened
   ("init: ran editor"); "init: run frobnicator: Cannot find 'frobnicator'."
   and the box with one failed line.
5. respawn: the taskbar killed came back. The brake: six kills through one
   telnet session, 0.7 to 0.8 s apart: after the fifth quick end "taskbar keeps
   ending as soon as it starts; not started again"; `run taskbar` brought it
   back. `taskbar:off` in place of respawn: no bar, "taskbar is off"; `run
   taskbar` started it. (A first try with `taskbar:off` added above the
   respawn line started the taskbar, as the later line asks.)
6. `dbg.py misses`: none. VNC and telnet answered. Free RAM on the idle
   desktop: internal 206,051 against 206,047 on main, external 6,469,328
   against 6,469,572.

Left on the board: the new-form file init wrote in 2. The old file is kept
in the board thread's scratchpad.
