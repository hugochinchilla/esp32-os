---
title: "Chosen for the services loop"
date: 2026-11-17
by: owner+agent
status: to-confirm
area: system
summary: "One task, `inetd`, runs every network service off the UI task (backlog 8b)"
---

# Chosen for the services loop

Heading in the single-file log: "Chosen for the services loop (2026-11-17, to confirm)".


Backlog item 8b, built on a branch, tested on the PC, compiled for the board
and not yet run on it. The owner chose the services loop itself (above); he
was away for the rest, so the points below are the agent's and his to
change. The options and the reasons are in
`docs/journal/2026-11-17-services-loop.md`.

- One task, `inetd`, runs every network service: the remote screen (VNC) and
  its web page today, the telnet listener next. 4 KB of stack in internal
  RAM, priority 4, just under the UI task. It is started after the desktop,
  and only if a port could be opened. It has a row in the process table
  (system flag), one row for the one task; `vncd` and `httpd` stay unused
  names. The other options were a task per service, or leaving the network
  in the UI loop.
- It sleeps in `select()` on all the services' sockets (`net::Services`,
  `net::PosixWait`). Each service says what it waits for (`watch()`); the
  loop knows nothing of VNC. With nobody connected it does not wake at all.
  The UI loop no longer looks at the network.
- The services task never touches windows, timers or the process table. A
  viewer's keys, mouse and typed text reach the window manager as messages
  (`msg::PostedInput`); when the queue is full the sender waits a second,
  then the event is lost and counted. Anything a later service needs from
  the process table also goes through `msg::post()`.
- The grid of what was painted, written by the UI task and read by the
  services task, has a lock (a `ports::Lock`). The screen's pixels have
  none: a block read while it is being painted is sent again, because the
  window manager reports a rectangle after painting it. The other option
  was a lock around painting.
- While a viewer waits for the screen to change, the services task looks
  every 10 ms, since the UI task cannot end a `select()`. The other option,
  an event the UI task signals (ESP-IDF's `eventfd`), is left for later.
- A service with work left comes straight back; after 40 ms without a sleep
  the loop rests 10 ms, so lower tasks and the idle task always run.
- A departing viewer's held buttons are released where that viewer last had
  the pointer.
