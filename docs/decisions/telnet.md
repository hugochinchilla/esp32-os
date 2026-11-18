---
title: "Chosen for telnet"
date: 2026-11-17
by: owner+agent
status: to-confirm
area: network
summary: "Telnet on port 23, no password, on by default (the owner's decision); the rest is the agent's (backlog 17)"
---

# Chosen for telnet

Heading in the single-file log: "Chosen for telnet (2026-11-17, to confirm)".


Backlog item 17. Journal: `docs/journal/2026-11-17-telnet.md`. Chosen by the
agent under the owner's "dejo a tu criterio" of 2026-11-16, except the
default, which the owner decided.

- **On by default** (the owner's decision, 2026-11-17, on a card in the
  thread; it turns round what the agent had built). Port 23, no password
  (the owner's rule: no users). `[telnet] enabled = false` in
  `C:\.config\network.ini`, read at start-up, switched it off; since
  2026-11-18 it is the line `telnetd:off` of the start-up file instead
  (see "Services are lines of the start-up file"). The other
  option, off until the file says otherwise, was what the agent had
  recommended, because a shell that deletes files appears on any network
  the board joins; the owner accepts that.
- **One session at a time**; a second caller waits. Rejected: several, until
  someone wants them.
- **The socket work is on the services task and never waits; the session
  runs on a task of its own**, started at connect and given back at leave.
  Rejected: a task blocked on the socket.
- **A program that writes faster than the network takes waits** (the TTY
  asks for room before each 256-byte piece, `Tty::on_room`); typed bytes
  keep arriving meanwhile, so Ctrl+C works. Rejected: dropping output.
- The client is asked for character mode with our echo and for its window
  size; nothing else.
