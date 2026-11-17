# The window server and the desktop shell: the code cut in two

2026-11-17. Backlog item 21, second part.

## What was asked

After the first part (the taskbar as a process, already on the board) the
owner asked what the choices for the split were, what each costs to build
and to run, and how systems of the time did it. Then: "hagamos el corte 2".

## What he was told

| Cut | What it is | To build | To run | Who did it so |
| --- | --- | --- | --- | --- |
| 1. The process only (was on main) | `taskbar` is a row of its own, but the window manager still draws the bar and the icons | Done | Measured: RAM within 20 bytes, start a little faster | Mac OS: the Finder was a program, the menu bar was drawn by the system |
| 2. The code cut in two, on the same task | The server knows windows, pointer and background. The bar and the icons are drawn by the shell's own code | Three to five days estimated | About the same (estimated) | Windows 95: USER and GDI were the system, `explorer.exe` a program whose bar was a window. Windows 3.1 and `progman.exe`; AmigaOS, Intuition and Workbench |
| 3. Two tasks that talk by messages | The shell on a task of its own, a queue between it and the server | Weeks; it is item 26 applied to the desktop | Each paint crosses a queue: microseconds, and memory for stacks and messages | X11: server, window manager and panel as separate processes |

The difference between 2 and 3 is how many threads of execution there
are, not how the code is laid out. In 2 the bar calls the server as a
function and both take turns on one task, as every program of Chinchilla
does today; a bar stuck in a loop stops the desktop until the watchdog. In
3 it could be ended while stuck.

How this sits with the OS calls (item 20): they are the same border seen
from two sides. The calls decide how a program talks to the system; the
cut decides what is on each side. Painting is not an OS call here today:
the server calls a window to paint and hands it a canvas, and the drawing
is shared C++ classes. (Windows 3.x and 95: painting was system API, GDI,
called as a library. Mac OS: QuickDraw, by the same mechanism as the rest
of the system. Unix with X11: a message on a socket, not a system call.)
Cut 2 works with the calls as chosen; cut 3 needs numbered calls with
handles (item 37), because two tasks cannot share C++ objects safely. The
order that follows: cut 2, then item 37, then cut 3 with item 26.

## What was found

- The window manager held, besides windows and menus: the taskbar's
  layout, painting, buttons and clicks; the quick-launch icons; the tray
  icon; the desktop icons with their selection, double click, dragging and
  drop targets; the right click on the desktop. About 420 of its 2,700
  lines, and eleven of its capture and state variables.
- The shell side (`core/src/shell/desktop.cpp`) only handed it data: a menu,
  an array of icons, three quick-launch entries.
- Nothing draws to an off-screen buffer and copies: each pixel is painted
  once, straight to the picture being shown. The icons code depends on
  that: it fills the background around the icons, then each icon with the
  background under it in one pass.

## Options

**What the shell's surfaces are to the server.**

- *Chosen: panels.* A panel is a part of the screen that is not a window
  and belongs to the shell. There are two places for one: the desktop
  panel has the work area behind every window, the bar panel has the strip
  under it, in front of the windows. The server asks a panel to paint,
  hands it the mouse and files dragged over it, and tells it three kinds
  of news (the windows changed, the Start menu opened or closed, the clock
  shows another minute). It knows nothing of what is in a panel.
- Windows of a special kind (no frame, always on top or always at the
  bottom), which is how Windows 95 did it. Closer to the original, and
  what I described to the owner as the likely shape. Not chosen after
  reading the code: every count of windows, Alt+Tab, the taskbar's own
  list, "close everything" at shut down and some sixty checks in the tests
  would have to learn to skip two windows that are not programs, and the
  desktop "window" would have to be transparent to the background, which
  the paint-once rule does not allow. Panels give the same separation
  with two slots and no special cases in the window list. The visible
  result is the same.
  The owner was told of the change once it was built and confirmed it the
  same day: "yes, panels are ok".
- The shell painting through the window manager's private parts (a friend
  class). No cut at all.

**Who paints the background.** The background is the server's (Display
sets it). But the desktop panel paints it where it has no icon, and under
each icon, through `Wm::fill_backdrop`, because of the paint-once rule.
With no desktop panel the server fills it itself.

**The Start menu.** Menus are the server's: it draws them and follows the
mouse and the keys in them, for every program. The shell hands over which
menu the Start key opens (`set_start_menu`); the bar opens it on a click
with the same call the key uses, and shows its button pressed while it is
open.

**The clock.** The time of day stays with the server: the debug protocol
sets it and the network does. The bar draws it and is told when the minute
changes.

**The strip.** The server still keeps the bottom 28 lines free of windows
whether or not there is a bar (`kTaskbarHeight`). Letting the bar say how
much room it wants is for the day there is a second kind of bar.

**The tray icon.** It is the network's to set and the bar's to show. The
desktop keeps it while there is no bar and hands it to the next one.

**Where the two surfaces live.** `core/desk` (`desk::Taskbar`,
`desk::Icons`): plain objects that need only a window server, so the
tests make them without the rest of the desktop. The `taskbar` process
makes them when it starts, with memory from the OS calls (counted as its
own: 1 KB in `ps`), and destroys them when it ends.

## What was built

- `wm::Panel`, `wm::PanelPlace`, `wm::PanelNews`; `Wm::set_panel`,
  `panel`, `panel_owner`, `canvas`, `fill_backdrop` (now public). The
  server's mouse code has one capture for "a panel has the button" in
  place of two (taskbar button, icon); drags ask the desktop panel as they
  ask a window.
- Out of the window manager: the taskbar's layout, painting and clicks,
  quick launch, the tray icon, the desktop icons, their menu and drop
  functions, `set_shell` and `shell_ended` of the first part. It went from
  2,717 lines to 2,478.
- `core/src/desk/taskbar.cpp` (280 lines) and `core/src/desk/icons.cpp`
  (227): the same code, moved, asking the server through its public
  calls.
- `core/src/shell/desktop.cpp`: the `taskbar` process makes the two
  objects, sets them up and attaches them; its end destroys them.
  `shell::taskbar()` and `shell::desktop_icons()` for the tests.
- The screen is the same to the pixel: every reference picture matches
  except three with a process list, where `taskbar` now shows 1K of memory
  instead of none.

## How it was tested

- `host/tests/test_wm.cpp`, `test_panels`: the server with no panel (the
  strip is background, clicks and the Start key do nothing); a panel that
  is not a taskbar gets paint, Down, Move and Up, a double click, the
  right button, a drag over it, away and dropped, the three kinds of news;
  a panel taken away in the middle of a press.
- The window manager's own tests run with a `desk::Taskbar` and a
  `desk::Icons` attached, and every check on the bar and the icons that
  was there before passes unchanged in what it checks.
- `test_proc`'s taskbar test (kill, the frame without it, the restart, the
  brake, `run taskbar`) passes on the new code.
- All 39 PC tests pass; 99 reference pictures identical, 3 redrawn for the
  memory figure. The firmware compiles and links (0x160b20 bytes). It has
  not run on the board. The PC desktop program is not built in the cloud.

## What is left

- On the board: the whole desktop by hand, since the code that draws the
  bar and the icons moved: Start menu by click and by key, hold a taskbar
  button and slide off it, quick launch, minimize and restore from the
  bar, the tray icon and its click, the clock changing minute, icons
  selected, opened and dragged to the Trash, a file dropped on the
  desktop, the right click, `kill` on the taskbar and its return. Paint
  times and free memory against main.
- Display, Screen Saver, Network and Shut Down can now be split into a
  settings window and what the server keeps running, which finishes item
  20's list.
- Whether a visible pause is added before the taskbar is restarted is
  still the owner's to say.


## On the board (added by the board thread, 2026-11-17)

Checked together with the OS calls' second part at 657a022, with main ebe84b0 (the missed-refill
log) merged in; only the journal index conflicted. PC tests 41 of 41
(`serial_console` passed), the firmware built at 0x161600 and was flashed by
cable, the app only. A Firefox tab on the owner's laptop had the web page
viewer open during part of the turn.

1. The Start menu opens with a click, with Ctrl+Esc and with the Windows
   key.
2. A taskbar button pressed and slid off onto the desktop before release:
   nothing happens. A click on the button minimises its window, a second
   click brings it back.
3. Quick launch: File manager (My Board), Editor and Terminal open with one
   click each.
4. The tray icon shows the signal, and a click opens the Network panel. The
   clock moved from 10:56 to 10:57 on the minute.
5. Desktop icons: a file put in C:\Documents\Desktop appeared; a click
   selects it (highlighted name), a double click opens it in the Editor, and a
   drag onto the Trash moves it to C:\.trash (the Trash icon shows it is not
   empty). A file dragged from the File manager onto the desktop moved into
   C:\Documents\Desktop. A right click on the desktop gives "Display...".
6. Task Manager with all processes: taskbar shows 1K. End Task on it: it
   came back (PID 19, parent 2), and its quick launch, the desktop icons and
   the Start menu worked.
7. Repaint of the whole screen: 13.3 to 14.7 ms, against 13.5 to 14.0 ms on
   main right after. Free RAM on the idle desktop: internal 206,171 bytes
   against 206,027 on main, external 6,472,080 against 6,470,292.
8. The idle desktop (no pointer) after the merge, against main: see the end
   of this section.
9. `dbg.py misses`: no missed refill not caused by the debug link. VNC
   took a full screen; telnet answered.

A note on the clock: on the first start after the flash, a hard reset that
clears the clock, the bar showed 12:00 AM for about a minute and a half
before the network set the time. Measured again after two more hard resets
the time came in 3 and 4 s. The code that sets it is unchanged, so this was
the time server being slow that once.
