---
title: "Chosen for the message queue"
date: 2026-11-16
by: agent
status: to-confirm
area: system
summary: "Other tasks never touch windows: they post a message that the UI task runs (backlog 8)"
---

# Chosen for the message queue

Heading in the single-file log: "Chosen for the message queue (2026-11-16, to confirm)".


Backlog item 8, built on a branch, tested on the PC, compiled for the board
and not yet run on it. The options and the reasons are in
`docs/journal/2026-11-16-message-queue.md`. Each is easy to change.

- Other tasks never touch windows, timers or the process table. They post a
  message (`core/msg`), and the UI task runs it between two rounds of its
  loop.
- A message is a function to call on the UI task, addressed to a process:
  `msg::post(pid, fn, context, a, b)`. It is dropped if the process has ended
  by the time it is delivered, and its function runs as that process. PID 0
  is the OS itself and is always delivered. The other options were numbered
  messages to windows, as Windows did, or a bare function with no address.
  **Confirmed by the owner, 2026-11-16**, on a card with those three options.
- The queue holds 32 messages (0.8 KB of internal RAM with its wake-up
  semaphore, none from the heap). When it is full the sender chooses: give
  up at once, or wait for room for a given time.
- The UI loop sleeps until a message, serial bytes, or the window manager's
  next timer (`Wm::idle_us()`), one second at most. Serial bytes wake it
  through the serial driver's notification hook. Timers are as exact as
  before: one kernel tick, 10 ms.
- **Replaced 2026-11-17 by the services loop** (next section): the network
  was still looked at by the clock, since nothing can wake the
  loop for a socket: every 10 ms while a viewer is connected or a web page
  is being sent, every 50 ms while only waiting for a connection, never
  with no port open. The other option was a watcher task (about 3 KB of
  internal RAM); it goes away by itself when the VNC server becomes a task.
  **Confirmed by the owner, 2026-11-16** ("Ok", after asking who would own
  the watcher: nobody does today, the UI task looks at the network itself).
  For now only: the owner found it odd that the UI looks after the network
  (a board with no monitor might not run the UI at all) and chose, on a card,
  a **services loop** as its own backlog item (8b): one task shared by the
  network services, asleep on all their sockets, that reaches the desktop
  only by messages. The other options were to build it in the same change or
  to leave the network in the UI loop.
- The UI task runs at most 16 messages per round, then timers and painting.
