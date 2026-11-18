---
title: "Parallel work"
date: 2026-11-16
by: owner
status: decided
area: project
summary: "Threads work on branches with pull requests; the laptop-and-board thread merges after a board check"
---

# Parallel work

The owner asked for work to be split across several threads, each in its own
worktree, with board time taken in turns. The laptop-and-board thread keeps
committing to `main`. Other threads use one branch per feature, test on the PC
and open pull requests; this replaces "no branches, no pull requests" for those
threads only. The owner decided that the laptop-and-board thread ("board
manager") merges each branch into `main` after it has passed on the board; the
other threads do not merge their own work.
