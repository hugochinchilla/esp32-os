---
title: "Chosen for the serial console"
date: 2026-11-17
by: agent
status: to-confirm
area: console
summary: "The shell on the serial port shares the wire with the debug protocol: a zero opens a frame (backlog 14)"
---

# Chosen for the serial console

Heading in the single-file log: "Chosen for the serial console (2026-11-17, to confirm)".


Backlog item 14. Journal: `docs/journal/2026-11-17-serial-console.md`.
Chosen by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **A zero opens a frame; outside a frame every byte is typed** and goes to
  the shell at once. This sharpens "bytes between two zeros that pass the
  check go to the debug service, everything else to the console" under "OS
  architecture": a person never sends the closing zero. A frame's bytes
  never reach the shell, also when the frame fails its check.
- **A frame left open with nothing arriving for half a second is given up**
  and what it held is taken as typed. Rejected: a limit from the opening
  zero; no way out.
- **The session's output is plain text between frames.** Rejected: frames
  of its own.
- **Off unless the start-up file says `start console`** (12 KB of internal
  RAM, and a shell on the port). Rejected: always on.
- After `exit` the next session greets. The row is `console`, protected.
