---
title: "Chosen for the file system lock"
date: 2026-11-16
by: agent
status: to-confirm
area: storage
summary: "One lock for the whole file system, held for one call (backlog 9)"
---

# Chosen for the file system lock

Heading in the single-file log: "Chosen for the file system lock (2026-11-16, to confirm)".


Backlog item 9. Journal: `docs/journal/2026-11-16-file-system-lock.md`.

- One lock for the whole file system, in `fs::Storage`, around every call
  (`Storage::set_lock()`, a `ports::Lock`). All drives share it. The other
  options were a lock in each drive, one lock per drive, a read-write lock, a
  file system task with a queue (the AmigaOS way), and `std::mutex` in the
  core.
- It is held for one call. The calls made of several steps (`begin_save`,
  `finish_save`, `read_file`, `write_file`) hold it across all of them.
  Between the pieces of a long read or write it is free.
- It is not recursive. Code that holds it must not call `Storage` again; a
  KILL must not take a task that holds it (see "Signals" above).
- It does not stop two tasks from working on the same file. A program that
  saves from a job locks its own window (item 10); the shell will need a
  rule.
- With no lock set, `Storage` is as before (the simulator and one-thread
  tests). The board sets one at start-up.
