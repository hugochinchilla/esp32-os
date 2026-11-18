---
title: "Chosen for the frame budget"
date: 2026-11-18
by: agent
status: to-confirm
area: video
summary: "The budget is one video frame, 16 800 us; how drawing is measured against it (backlog 63)"
---

# Chosen for the frame budget

Heading in the single-file log: "Chosen for the frame budget (2026-11-18, to confirm)".


Backlog item 63, `docs/journal/2026-11-18-frame-budget.md`. Picked by the
agent that built it, on the owner's outline; each is easy to change.

- The budget is one video frame as the board really sends it: 16 800 µs,
  59.52 frames a second (a 25 MHz pixel clock, 800 x 525 pixels a frame),
  not the standard's 59.94. The firmware works it out from the pixel clock.
- What is measured is each piece of the desktop's work, painting included
  (a key, a mouse event, typed text, a tick, the messages from other
  tasks), not the painting alone: while a piece runs nothing else reaches
  the screen. A piece longer than the budget is a frame over budget.
- A timer that fires a frame or more late is counted too.
- It goes to the system log, not a log of its own: one `desktop:` line at
  once, then at most one every ten seconds with what came meanwhile. The
  exact figures and the last 16 slow pieces, with where their time went and
  whose it was, are read by `tools/dbg.py frames`.
- The debug link's own work on the desktop's task (a screenshot, a file
  copy) is not counted.
- This is apart from the missed video refills (`dbg.py misses`): those are
  a torn or shifted picture, these are a picture shown late.
