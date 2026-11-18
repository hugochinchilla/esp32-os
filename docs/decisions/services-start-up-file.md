---
title: "Services are lines of the start-up file"
date: 2026-11-18
by: owner
status: decided
area: system
summary: "`telnetd` and `vncd` are lines of init's start-up file; a Services dialog and commands edit them (backlog 62)"
---

# Services are lines of the start-up file

Heading in the single-file log: "Services are lines of the start-up file (the owner's request, 2026-11-18)".


Backlog item 62. Journal: `docs/journal/2026-11-18-services.md`. The owner
asked for a Services dialog under Start > Settings that "just modifies the
init config", and for commands to stop and start telnet and VNC that do not
fight init's respawn: what he stops stays stopped until the next start.

- **`telnetd` and `vncd` are things init starts, with lines of their own**:
  `telnetd:once`, `vncd:once` in the usual `startup.txt`, after
  `inetd:once`. `off`, or no line, and the port is not opened. `inetd`
  stays the one task all the network services run on; the two are services
  of its loop, not tasks. `vncd` is the remote screen whole: the VNC port
  (5900), the browser's page (80) and what mDNS announces for them.
  `[telnet] enabled` of `network.ini` is gone (no backward compatibility);
  `[update] enabled` stays there, and updates stay inside `inetd`.
  The agent's, to confirm: the names (`telnetd`, `vncd`), no line meaning
  not started, and updates left out of the dialog.
- **The Services dialog only edits those two lines.** A ticked box writes
  `once`, an empty one `off`, and only for a box that was changed, so a
  line someone wrote as `respawn` stays. Every other line and comment is
  left as it is. It applies at the next start (the owner's words); the
  dialog says so. A program of its own (`services.cpp`), which keeps
  nothing while closed.
- **`service NAME stop` and `service NAME start`** in any terminal (the
  serial console, telnet, a Terminal window); `service` alone lists what
  init has and its state. `telnet` and `vnc` are accepted for `telnetd`
  and `vncd`. The agent's choice of name, after the Unix command; `net
  start` and `net stop`, the DOS and NT way, was the other and can be
  added as another name.
- **What is stopped by hand is held** (the owner's rule): init does not
  start it again when it ends, whatever its line says, until `service NAME
  start` or the next start of the machine, where the file alone decides.
  The rule is init's (`Init::stop`), for anything it starts: a part with
  a stop function (the two services), or one whose process it watches
  (`service taskbar stop` ends the taskbar and it stays away). The desktop
  cannot be stopped this way.
- **`inetd` can be stopped and started again** (since the board turn of
  2026-11-18, which found a second start ending at once): `service inetd
  stop`, End Task or `kill` end it and everything in it; `service inetd
  start` starts the task and its loop again, with updates if the settings
  say so. Its services stay stopped until each is started by hand
  (`service telnetd start`): the file is read once, when the machine
  starts.
- **The services cannot be `respawn` today**, as `inetd` cannot: they do
  not end by themselves, and they have no process init could watch (`vncd`
  has no row at all, by the rule that only real tasks are listed;
  `telnetd` keeps the row it had, which now comes and goes with the
  service). `telnetd:respawn` starts it once and says so in the log. So
  today the hold is what keeps a later change from fighting the owner's
  stop, not something a service needs yet.
- **Stopping one service leaves the others running.** The loop does it on
  its own task at its next round (within a second on the board): the
  service lets go of whoever is connected and gives back its memory, the
  port is closed there, and the UI task is told, which takes the row away.
  Starting opens the port on the UI task first, so a port that cannot be
  had is known at once. A start while the stop is still under way is
  refused ("try again in a moment"). Rejected: ending `inetd` and starting
  it again with fewer services (it would drop the other service's
  connection, and its task was not written to start twice).
- **`inetd` starts whenever there is a radio**, also with both services
  off, so that one can be started by hand later: 4 KB of stack and one
  wake a second on a board that listens to nothing. Before, it was not
  started when no port was open.
