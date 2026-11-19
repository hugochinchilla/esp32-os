---
title: "Chosen for screenshot requests that wait"
date: 2026-11-19
by: agent
status: to-confirm
area: tools
summary: "The debug link answers only the last screenshot request that waited, one per loop round; a hang's record says what the task was doing (backlog 75)"
---

# Chosen for screenshot requests that wait

Backlog item 75; the story is in
`docs/journal/2026-11-19-screenshot-backlog.md`. Chosen by the agent, to
confirm. Tested on the PC; not yet run on the board.

- **A Screenshot request is not answered as it is read.** The loop reads
  the serial bytes that have arrived, then calls
  `dbg::Protocol::answer_waiting()`, which sends one screenshot, for the
  newest request. Requests that had another behind them get no reply: the
  computer asks again only when it has given the one before up. Every other
  request is answered as it is read, as before.
- Why: a tool that asked again sooner than the board could send a
  screenshot (0.6 s against 0.8 s with the Bricks pattern) kept the UI
  task answering inside one round of its loop until the watchdog restarted
  the machine.
- **A hang's crash record says what the task was doing** ("Doing   : a
  screenshot for the debug link"): the loop names each of its parts with
  `watchdog_doing`. When the task was waiting, the record also has its
  chain of calls, read from its saved stack. The record's layout did not
  change.
- Not taken: one request per round (the board stays behind for good); a
  sign of life from inside the screenshot (the desktop stays frozen);
  sending the screenshot from another task off a copy of the picture (a
  real improvement, kept as an idea); a longer wait in the tool; an
  encoding with a copy from 8 rows up.
