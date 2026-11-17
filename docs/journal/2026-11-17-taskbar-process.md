# The taskbar is a process: ended, it goes and comes back

2026-11-17. Backlog item 21, first part.

## What was asked

"Split the window manager into window server and desktop shell. The taskbar
becomes a process that restarts when killed." The architecture review drew
it as two rows: `desktop`, the window server, protected; and under it the
shell with the taskbar, the Start menu and the icons, not protected, with
the respawn flag. "Killing the shell would make the taskbar vanish and come
back."

## What was found

- One process, `desktop`, owned everything that is not a program: the
  windows' frames, the pointer, the background, and also the taskbar, the
  Start menu, the quick-launch icons, the desktop icons and the timer that
  looks at the desktop's folder. It is protected, so none of it could be
  ended.
- The window manager (`core/src/wm/wm.cpp`, 2,700 lines) draws the taskbar
  and the icons itself, but it does not decide what is in them: the Start
  menu, the icons and the quick-launch entries are handed to it by the
  desktop code in `core/src/shell/desktop.cpp`, with a callback each.
- Since item 5b a process without windows can be ended if its row has a
  stop function, and since item 18 a row with the respawn flag is started
  again, with a brake when it ends five times within two seconds of
  starting.
- So everything needed for "a process that restarts when killed" was
  there. What was missing was the process.

## Options

**How deep to cut.**

- *Chosen: the process first.* A row `taskbar`, child of `desktop`: it
  hands the window manager the Start menu, the quick-launch icons and the
  desktop icons when it starts and takes them back when it ends. The
  drawing code stays in the window manager, which draws a taskbar only
  while a shell says there is one. What the user sees and what `ps` says
  are now the same thing.
- The whole cut: the window manager in two classes, the taskbar a surface
  that the shell draws through the same calls as a program. Cleaner, and
  the end state. Not chosen for this step: it moves a third of a
  2,700-line file and can change almost every reference picture, and it is
  better done once the OS calls (item 20) have their window part settled.
  Nothing in this step is in its way.
- Leaving it.

The owner was asked (a card in the thread, the first option recommended);
built on it meanwhile and marked "to confirm".

**Its name.** `taskbar`, not `shell`: on this system "shell" is what one
types commands into, and whoever watches a `kill` on screen should see the
thing named go. (The review said `shell`; Windows says Explorer.)

**What goes when it ends.** The taskbar, the Start menu (closed if open),
the quick-launch icons, the desktop icons, the desktop's right-click menu,
dropping files on the desktop, its message boxes, and its timer. What
stays: every window, the pointer, the background, the screensaver, the
network and its timer, the clock (it keeps counting; it is shown again when
the bar is back), Ctrl+Alt+Del. Programs it started are not ended: they
become init's children, as on Unix.

**The space where the bar was.** Options: give it to the windows (a
maximized window grows) or leave the work area as it is. *Chosen: leave
it*: the bar is back a moment later and windows should not jump twice. The
strip shows the background.

**Seeing it go.** The end and the restart happen at the same safe point,
so without care the bar would be gone and back before anything was
painted, and killing it would look like nothing. *Chosen:* the window
manager paints once between the two, when something ended that is about to
be restarted. No waiting is added; the screen shows what happened in the
order it happened. On the board that is one frame or so without the bar. A
pause before a restart (half a second, say, as service managers have)
would make it plain to the eye; that is a choice for the owner and is not
built.

**TERM.** The taskbar has nothing to ask before closing, so TERM ends it
like KILL does. End Task in the Task Manager works on it (shown with "Show
all processes").

**Ended too often.** Killed five times within two seconds of each start,
it is not started again (the brake). The machine then has windows and
Ctrl+Alt+Del but no way to open a program from the screen. Options: no
brake for the taskbar; a key that starts it; a command. *Chosen: a
command*, `run taskbar`, from the serial console or telnet (or a restart).
The brake stays because it is what stops a taskbar that crashes on start
from taking the whole machine's time. A key was left out: it would be a
key nobody knows.

**Who starts it.** The desktop, as its child, when the desktop starts: no
new line in the start-up file, so a board that already has the file needs
no change. An init unit of its own (`start taskbar`) would let the file
leave it out, which is a machine nobody wants by accident.

## What was built

- `wm::Wm`: `set_shell(pid)`, `shell_ended()`, `shell()`,
  `taskbar_shown()` and `end(pid)`. The callbacks of the Start menu, the
  quick-launch icons, the desktop icons, the tray and drops on the desktop
  run as the shell's process, so what they open is its child. With no
  shell, the strip is painted as background and takes no clicks, and the
  Start key does nothing. The extra paint between an ending and a restart.
- `proc::Table::restarts_waiting()`.
- `core/src/shell/desktop.cpp`: `start()` keeps what is the window
  server's (the OS calls, the clipboard, the attention key, screensaver,
  background, network) and calls `start_taskbar()`, which makes the row,
  gives it its stop and restart functions and sets the menu and the icons
  up. `stop_taskbar()` takes them back and ends the row.
- `run taskbar` in `run_text`.
- Programs opened from the Start menu, the quick-launch icons and the
  desktop icons are now children of `taskbar`, not of `desktop`.
- Every PID after 2 is one higher: `taskbar` is 3 on a machine with init,
  `inetd` 4. Five reference pictures with a process list changed for that.

## How it was tested

- `host/tests/test_proc.cpp`, `test_taskbar`: the row and its flags; KILL
  with its Start menu open, a box of its own and a program it started (the
  box goes, the program stays and is init's, a new row with a new PID, at
  least one frame painted without the bar, the screen afterwards equal to
  before, the new menu starts a program whose parent is the new row);
  TERM; the brake (no bar, no menu on the Start key or a click, the Task
  Manager still opens); `run taskbar` brings it back and refuses a second.
- The existing process tests, with the taskbar's row counted and the
  Task Manager's End Task button usable on it.
- All 39 PC tests pass. The firmware compiles and links (0x160580 bytes).
  It has not run on the board. The PC desktop program is not built in the
  cloud.

## What is left

- On the board: `ps` shows init 1, desktop 2, taskbar 3; `kill 3` from a
  Terminal, and End Task on `taskbar` in the Task Manager: the bar and the
  icons go and are back, open windows stay, the Terminal goes on working,
  and the program list shows the new PID. With a minimized window: its
  button is back with the bar. Time to the desktop and free memory against
  main.
- The owner's choice: a visible pause before the restart, or none.
- The second part of item 21: the cut of the window manager's code in two,
  with the taskbar drawn by the shell. With it Display, Screen Saver,
  Network and Shut Down can be split the same way (item 20's last files).
