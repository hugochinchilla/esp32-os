# The OS calls: Display, Screen Saver, Network and Shut Down

2026-11-17. Backlog item 20, second part.

## What was asked

The owner's standing request is to work through the backlog. Item 20 had
stopped at 11 of 16 programs: four waited for the cut between the window
server and the desktop shell (item 21, second part), which now exists on
this branch, and the Terminal waits for the shell's commands.

Each of the four was two things in one file: a window the user opens, and
something the system keeps going whether the window is open or not.

| Program | The window | What the system keeps going |
| --- | --- | --- |
| Display | Colour and pattern, with a preview | The background of the desktop |
| Screen Saver | Which animation, after how long, a preview | The animation and its timer |
| Network | The networks in range, a password | The Wi-Fi manager's tick, the tray icon, the clock set from the network |
| Shut Down | Shut Down, Restart, Cancel | Closing every program and the screen that says the power can go |

## The options

**1. One call for each thing the window sets (chosen).** The window stays a
program and asks: `os::set_backdrop`, `os::set_screen_saver`,
`os::preview_screen_saver`, `os::last_saver_run`, `os::shut_down`,
`os::network`. What keeps going moves to the system's side.

**2. A general "desktop setting" call**, `os::set("background.colour",
"teal")`, with the system reading names. One call instead of six, and new
settings need no new call. Not chosen: the names would be a second, loose
interface nobody checks at build time, and the system would have to know
every program's settings. It is the registry's road, which the settings
decision already turned down (`docs/DECISIONS.md`, settings as files).

**3. Leave them as system code** and count them out of the list, as the
desktop is. Not chosen: three of them are ordinary dialogs, and a dialog is
a program (the owner's rule, 2026-11-17).

For the network there was a second choice: what a program gets.

- **The service itself, through one call (chosen):** `os::network()` gives
  the Wi-Fi manager, or nothing on a board with no radio. The same shape as
  `os::file::storage()`: named for what it is, asked for each time, not
  kept.
- **A call for each thing the panel does** (scan, connect, forget, the
  list, the link): eight to ten calls that only pass through. It is what
  numbered calls with handles will want (item 37), and belongs to that
  step; writing them now would be writing them twice.

These are the agent's choices, inside the option the owner chose for item
20 (a thin layer now, numbered calls later). To confirm.

## What was built

- `core/os`: the six calls. The screensaver's runner (which animation, how
  the last run went) lives behind them now, not in the settings window's
  file. `os::Kit` gained the way down (`shut_down`), which the desktop
  hands in; `os::install_network()` hands in the Wi-Fi manager.
- **Display** (`display.cpp`): asks `os::set_backdrop`. It no longer keeps
  the colour and the pattern in two variables for good: it reads the
  settings file when it opens and at start-up.
- **Screen Saver** (`screensaver.cpp`): asks the three screensaver calls.
  Its choice and its 2 KB settings buffer were kept for good; they are in
  the window now and go when it closes.
- **Network** (`network.cpp`): the panel and the password dialogs only. The
  panel has a timer of its own while it exists and looks at the service
  four times a second; before, the service's tick reached into the panel.
  What the system keeps going is in `network_service.cpp`.
- **Shut Down** (`shutdown.cpp`): the dialog left `desktop.cpp` for a file
  of its own and asks `os::shut_down()` and `os::restart()`. `power.cpp`
  (closing every program, the night screen, Ctrl+Alt+Del) is the system's
  and is listed as such.
- `tools/check_os_calls.py`: 15 of 16 programs moved. The Terminal is the
  one left.

Nothing changes on the screen.

## How it was tested

- `test_os` gained the new calls: the background is set; a preview with no
  screensaver set shows one and a key ends it; one that is set starts after
  its wait and not once taken away; Display and Screen Saver opened and
  closed leave no memory out; closing Display puts back what the settings
  file says; no network on a machine with no radio; Shut Down with nothing
  open takes the screen.
- All 39 PC tests pass, with the address and undefined-behaviour checks.
  The reference pictures match, the network scenes included, with none
  redrawn.
- The firmware compiles and links. It has not run on the board.

## For the board

Display: Apply, Cancel puts the old background back, OK keeps it over a
restart. Screen Saver: Preview, the wait, OK kept over a restart, "(None)".
Network: the panel follows a connect and a disconnect while open, a wrong
password is told once, the tray icon changes with the panel closed. Shut
Down from the Start menu and from the Task Manager, Restart, and
Ctrl+Alt+Del on the night screen.


## On the board (added by the board thread, 2026-11-17)

Checked together with the window server split (cut 2) at 657a022, with main ebe84b0 (the missed-refill
log) merged in; only the journal index conflicted. PC tests 41 of 41
(`serial_console` passed), the firmware built at 0x161600 and was flashed by
cable, the app only. A Firefox tab on the owner's laptop had the web page
viewer open during part of the turn.

1. Display: a colour chosen and Apply changes the background with the
   dialog open; Cancel puts the old one back. A colour and a pattern with
   OK are written to C:\.config\desktop.ini and are kept over a restart. At
   the end Chinchilla blue and no pattern were put back with the dialog (the
   file is again 114 bytes, as before the turn).
2. Screen Saver: Starfield with Preview fills the screen and a mouse move
   ends it, the dialog still open. Starfield and 1 minute with OK: kept over a
   restart (the dialog showed them), and after 66 s without input the screen
   was the starfield; a mouse move ended it. "(None)" with OK writes
   `saver = none`. At the end Flying Chinchillas and 5 minutes were put back;
   the file is byte for byte as before the turn.
3. Network, without the owner's password: with the panel open, a disconnect
   (`dbg.py`) showed "Not connected" and a connect with the remembered
   password showed it connected again. A made-up password, not remembered,
   was told once: "Could not connect ...: the password is not right." in one
   box, which stayed one box. The board then rejoined with the remembered
   password and still has its one remembered network. With the panel closed
   the tray icon changed on a disconnect and came back on the connect. The
   clock is set from the network ("Clock: from network" in the panel).
4. Shut Down from the Start menu: the dialog, then Shut Down gives the night
   screen; Ctrl+Alt+Del on it started the board again. From the Task
   Manager, Shut Down... opens the same dialog and Restart restarted the
   board.
5. Memory: Display and Screen Saver opened and closed, three rounds: internal
   and external RAM back to the byte. With the Network panel in the rounds,
   internal back to the byte and external 1.2 to 1.6 KB lower, not growing
   from round to round. Main shows the same with the Network panel (1.3 to
   1.6 KB), so it is not this change.
