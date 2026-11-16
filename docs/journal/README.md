# Journal

How Chinchilla came to be the way it is, told as it happened. The owner is
recording the project, so this is the written memory behind it: what was asked,
what was found, which options were on the table, which was chosen and why, and
why the others were not.

It complements two other files:

- [`docs/DECISIONS.md`](../DECISIONS.md) is the reference: the current state of
  every decision, short, kept up to date.
- [`docs/BACKLOG.md`](../BACKLOG.md) is the list of work, with its status.

The journal is the story. Entries are not rewritten later; a correction is a new
entry that says what changed.

## Writing an entry

One file per piece of work or conversation, named `YYYY-MM-DD-topic.md`, added
in the same change as the work it describes. An entry has, as far as they
apply:

1. **What was asked**, in the owner's own words where there is a message
   (in the language he wrote it; the rest of the entry is in English).
2. **What was found**: how things worked before, with file names and figures.
3. **The options considered**, each with what it would cost.
4. **What was chosen, by whom, and why**, and why each other option was not.
5. **What was built**, how it was tested, and what was not tested (PC only,
   compiled only, run on the board).
6. **What was learned**: surprises, mistakes, things worth telling.
7. **What is left**, with pointers to the backlog.

Plain words, real numbers, no summaries of summaries.

## Entries

| Date | Entry | About |
| --- | --- | --- |
| 2026-11-16 | [Architecture review](2026-11-16-architecture-review.md) | How the OS works today, the owner's ideas for processes, signals, a shell, telnet, a TTY and init, twelve design choices and what was decided |
| 2026-11-16 | [The process table](2026-11-16-process-table.md) | PIDs for programs and services, windows and timers that know their owner, how the OS knows who is asking, when a process ends, and Task Manager's new list |
