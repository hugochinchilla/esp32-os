---
title: "Chosen for ending services"
date: 2026-11-17
by: agent
status: to-confirm
area: system
summary: "A process without windows can be signalled only if its row has a stop function (backlog 5b)"
---

# Chosen for ending services

Heading in the single-file log: "Chosen for ending services (2026-11-17, to confirm)".


Backlog item 5b. Journal: `docs/journal/2026-11-17-stopping-services.md`.
Chosen by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **A process without windows can be signalled only if its row has a stop
  function**, which really ends what the row stands for and then removes
  the row. The window manager calls it at the safe point. Rejected:
  deleting the task from outside.
- **`inetd` has one.** Ending it lets the viewer and the telnet session go,
  returns their memory, closes the ports and removes `inetd` and `telnetd`.
  It does not come back until the next start. TERM and KILL are the same.
- **The services loop's sleep is at most one second on the board**, so it
  hears the stop; an idle board's services task wakes once a second where
  it did not wake at all (item 8b). Rejected: a socket to itself.
