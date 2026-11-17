# Ending a service: TERM and KILL for processes without windows

2026-11-17. Backlog item 5b.

## What was asked

"TERM and KILL for services: each gets a stop function, so ending one
really stops it. Waits until a service is a real task with a row." Since the
services loop (item 8b) the network services are a real task, `inetd`, with
a row; End Task on it answered "services cannot be ended yet", because
removing the row would have left the task running: a lie in the list.

## What was found

- A signal to a window program is acted on by the window manager at a safe
  point: TERM presses its close buttons, KILL takes its windows away. A
  process without windows has nothing there to act on.
- The services task sleeps in `select()` with no limit when nobody is
  connected. Nothing another task does wakes it.
- The remote screen holds 183 KB while a viewer is connected, and a telnet
  session has a task of its own: ending the loop under them would leak
  both.

## Options

**How a service is ended.**

- *Chosen: the row carries a stop function*, set by whoever made the
  service. A signal to a process without windows is accepted only if it
  has one. The window manager calls it at the same safe point where it acts
  on the other signals; the function really stops the service, which may
  finish on another task, and the row is removed when it has.
- Not chosen: *remove the row and delete the task from outside.* A task
  deleted while it holds a lock or a socket takes the machine with it (the
  rule under "Signals").

**How the services task hears of it.**

- *Chosen: the loop's sleep gets a limit of one second* on the board, and
  the loop looks at a flag each round. One wake a second of a task that
  does nothing: microseconds.
- Not chosen: *a socket to itself that ends the sleep at once.* Exact, and
  more code in the one part that cannot be tried on the PC.
- This changes one line of the services loop's journal: an idle board's
  services task now wakes once a second instead of never.

**What ending `inetd` does.**

- Every service is told to stop on its own task: the viewer is let go and
  its memory returned, a telnet session is hung up and its task given back,
  an update under way is abandoned. Then the ports are closed and the rows
  `inetd` and `telnetd` go. TERM and KILL do the same.
- It does not come back until the next start. Starting it again by hand,
  and the respawn flag for services, are not built.
- `telnetd` alone cannot be ended: its port belongs to the same task. End
  Task says so.
- The serial console, init and the desktop stay protected.

## What was built

- `proc::Table`: `set_stop`, `stop_service`; `signal()` accepts a service
  that has a stop function. The window manager calls it.
- `net::Service::stop()`, and in `net::Services`: `request_stop()`,
  `set_longest_sleep()`, `round()` answers false once stopped. `stop()` for
  the remote screen, telnet and updates.
- The board and the simulator: `inetd` has a stop function; its task ends
  the loop, closes the ports and tells the UI task to remove the rows.
- The Task Manager's words for a service that cannot be ended.

## How it was tested

- `test_proc`: the table (a service ended, one that takes its time and
  refuses a second signal, a protected one) and the window manager calling
  the function at the safe point. `test_services`: the limit on the sleep
  and the stop, each service told once.
- `tools/test_serial_console.py`, on the simulator: `kill` on the console
  is refused; `kill` on inetd, then `ps` shows neither `inetd` nor
  `telnetd` and the telnet port refuses connections; the debug tool still
  answers.
- All 37 PC tests pass. The firmware compiles and links. Not run on the
  board.

## What is left

- On the board: End Task on inetd in the Task Manager with a VNC viewer
  connected (the viewer drops, external RAM returns, the row goes within a
  second or two, the ports refuse), `kill` from a Terminal, and the same
  from a telnet session, which ends itself that way.
- The services task's wakes: one a second now (the `stats` counters).
