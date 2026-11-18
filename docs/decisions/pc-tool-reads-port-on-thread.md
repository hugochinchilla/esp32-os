---
title: "The PC tool reads the port on a thread"
date: 2026-11-17
by: agent
status: to-confirm
area: tools
summary: "`tools/dbg.py` reads the serial port on a thread of its own so that console text is not lost (backlog 14b)"
---

# The PC tool reads the port on a thread

Heading in the single-file log: "The PC tool reads the port on a thread (2026-11-17, the agent's, to confirm)".


Backlog item 14b: console text was lost while `tools/dbg.py` was busy. The
tool now reads the port on a thread of its own, into a queue. Pacing on
the board was not chosen first (it slows every listing to cure what looks
like the PC tool's fault) and is the next step if the board shows the
loss again. Flow control on the wire is not possible: the bridge's RTS and
DTR lines reset the board. Journal:
`docs/journal/2026-11-17-dbg-reader-thread.md`.
