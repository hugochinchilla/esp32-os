---
title: "The OS calls"
date: 2026-11-17
by: owner+agent
status: decided
area: system
summary: "Programs ask the system through a thin C++ layer, `core/os/os.hpp`; numbered calls come later (backlog 20, 37)"
---

# The OS calls

Heading in the single-file log: "The OS calls (the owner's decision, 2026-11-17)".


Backlog item 20. The owner was asked with the first option recommended and
decided: the thin layer in C++ for now, and a backlog item (37) to look at
it again at the end and take it to numbered calls with handles. The
smaller points below are the agent's and still to confirm.
[Journal](journal/2026-11-17-os-calls.md), [the list](os-calls.md).

- Programs ask the system through `core/os/os.hpp` (namespace `os`): a thin
  layer in C++ over the window manager, the drives, the process table and
  the ports. Rejected for now: numbered calls with handles (every program
  and widget rewritten first, and its shape depends on how programs are
  delivered, which is open). The thin layer does not rule it out.
- A program that has moved is listed in `tools/check_os_calls.py`, a PC
  test that fails if it names `manager()`, `storage()`, `system_port()` or
  `proc::table()` again. New programs start on the list.
- A missing call is added to `os.hpp` and to `docs/os-calls.md`; a program
  does not go around.
- Files are still paths and `fs::File`. Open files owned by the process and
  closed at its end are the next step, not part of this one.
- The dialogs any program may open stay in `shell.hpp`: they are programs.
- init: what is needed (the desktop) starts before anything that is not,
  wherever its line is; a failed line of the start-up file opens a box
  after the start (`shell::startup_trouble`).
