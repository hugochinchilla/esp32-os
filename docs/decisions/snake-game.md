---
title: "Chosen in the snake game"
date: 2026-11-16
by: agent
status: to-confirm
area: programs
summary: "Culebra: look, rules, keys, speeds, pausing"
---

# Chosen in the snake game

Heading in the single-file log: "Chosen in the snake game (to confirm)".


Culebra, the snake game, was built on a branch from the PC tests and the
simulator only; it has not run on the board. The owner chose the name (see
"Decided"); the rest is still his to confirm:

- Look: a green striped snake with a face on pale sand, red berries, a
  24 x 18 board of 12-pixel cells in a sunken frame. All drawn in
  `programs/culebra/culebra.cpp`; the 16x16 icon is in `core/src/gfx/art.cpp`.
- Rules: walls end the game (no wrapping round), as does the snake's own
  body; the head may follow its tail. Two turns can be waiting, so quick
  corners work. Filling the board wins.
- Keys: arrows steer and start; Space or P pauses and carries on, and starts
  again after the end; F2 is a new game.
- Speed: Slow, Normal, Fast (a step every 200, 130, 80 ms) from the Game
  menu, worth 5, 10, 20 points a berry. The speed does not rise as the snake
  grows.
- The game pauses by itself when another window or a menu is in use, and
  when it is minimised. Closed in the middle of a game, it is still there,
  paused, when opened again.
- The best score lasts until restart; it is not saved to disk.
- It sits in Programs in alphabetical order with the other programs; there
  is no Games folder yet.
- The rules are in `core/games/snake` with no drawing or clock, tested in
  `host/tests/test_snake.cpp`; the timer runs only while the snake moves and
  each step repaints only the cells that changed.
