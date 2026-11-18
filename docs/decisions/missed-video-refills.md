---
title: "Missed video refills are logged"
date: 2026-11-17
by: owner
status: decided
area: video
summary: "Missed refills are logged; `dbg.py tasks` stays although it can cause them"
---

# Missed video refills are logged

Heading in the single-file log: "Missed video refills are logged (2026-11-17, the owner's request)".


- `dbg.py tasks` stays as it is (the owner, 2026-11-17: "we can leave the
  thing be as is"): it holds the kernel's lock for about 0.8 ms per call and
  can make the video miss refills. Only the debug link asks for it. Rejected
  for now: raising the video interrupt's level (needs ESP-IDF changed or a
  handler of our own at a high level) and a firmware list of task handles to
  make the call cheap.
- **Every missed refill reaches the system log**, counted by the interrupt
  and written once a second by the scanlog task (`sys::MissedRefills`): one
  line at the first miss of a burst, then at most one every 10 s with the
  sum. A storm of a miss every second writes about 5 KB in ten minutes.
  Misses during the debug link's task list are named in the line. Late
  refills that finished in time are not logged.
- **Log read** (debug request 0x16) gives the log to `tools/dbg.py dmesg`
  and `tools/dbg.py misses`, which exits 1 when a miss was not the debug
  link's. Agents run it after board checks (CLAUDE.md).
