---
title: "Chosen for pipes and redirection"
date: 2026-11-17
by: agent
status: to-confirm
area: console
summary: "A pipe is a hidden file and the commands run one after the other, as on MS-DOS (backlog 23)"
---

# Chosen for pipes and redirection

Heading in the single-file log: "Chosen for pipes and redirection (2026-11-17, to confirm)".


Backlog item 23. Journal: `docs/journal/2026-11-17-pipes.md`. Chosen by the
agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **A pipe is a hidden file and the commands run one after the other**, as
  on MS-DOS: `A | B` runs A with its output in `.~pipeNa.tmp` in the root of
  the session's drive, then B with that file as input, then removes it. Up
  to six commands on a line. Rejected for now: real pipes with a task per
  command (the right way once a program that never ends needs one; the
  commands would not change), and a buffer in memory (it has a size).
- `> FILE` writes a file, `>> FILE` adds to it, `< FILE` reads one; anywhere
  on the command, quoted names allowed. `<` only on the first command of a
  pipe and `>` only on the last. No `2>`, `&&`, `;` or variables.
- **Errors always go to the terminal**, never into the file or the pipe.
- A file gets Latin-1 and the terminal UTF-8: the session converts what a
  command prints when its output is a file, and typed input when a command
  reads it. The filters work in Latin-1.
- The filters: `more`; `grep` with the DOS names `find` and `findstr`
  (plain text, not patterns; `-i -v -n -c`); `head` and `tail` (`-n N`;
  `tail` shows 2 KB at most); `wc` (`-l -w -c`); `cat`/`type` without a file
  copies its input. `ls` writes one name a line into a file or pipe. No
  `sort` yet.
- A command reads typed lines (until Ctrl+D) only where the session has a
  task of its own. On the UI task it says what it needs instead of waiting,
  and `more` does not stop between screens.
