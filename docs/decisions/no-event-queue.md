---
title: "No queue of events"
date: 2026-11-17
by: owner
status: decided
area: system
summary: "What happens to a program while it is not responding is lost (backlog 26, 37)"
---

# No queue of events

Heading in the single-file log: "No queue of events (the owner's decision, 2026-11-17)".


Backlog items 26 and 37, the last part of both.
[Journal](journal/2026-11-17-event-queue.md).

- **What happens to a program while it is not responding is lost**, as
  it was: its keys, its clicks, its timer's ticks. The owner's choice on a
  card ("Dejarlo como está"). Rejected: a queue that keeps them for the
  program until it answers (built first, on the agent's recommendation,
  and taken out again); and a message loop in every program, as Win16
  programs had, with the desktop waiting only to paint (all twenty
  programs rewritten, and locks around the OS calls).
- **The one exception is the result of a background job**, which the card
  named: a save that finishes while its program is away is kept
  (`proc::kKept`, said by `job::Jobs::finish` alone) and handed to the
  program when it answers again, before anything new. Before, it was lost
  and the Editor's window stayed locked for good. It waits in external
  RAM counted as the program's memory, and goes with the program if that
  is ended. At most eight a program, as many as there can be jobs.
- Events therefore still reach a program as calls that the server waits
  for. Item 37's "events through a queue" is closed as not wanted.
