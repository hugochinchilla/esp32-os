---
title: "The chess computer does not answer at once"
date: 2026-11-18
by: owner+agent
status: decided
area: programs
summary: "Chess: the computer's move is shown no sooner than 250 to 350 ms after it began to think (backlog 67)"
---

Backlog item 67; the story is in
`docs/journal/2026-11-18-chess-reply-pace.md`. PC only so far.

- The owner: the computer waits 250 to 350 ms at the least before it
  answers a move, for the feel of it.
- As built, the agent's reading, to confirm or overrule: the length is
  drawn anew for each move between 250 and 350 ms and counted from when
  the computer began to think; thinking that takes longer is not made
  longer. Not a fixed 300 ms (every answer after the same pause reads as a
  machine's delay), and not a pause added after every answer (level 5
  slower for nothing).
- During the pause the game waits for the computer as while it thinks:
  the hourglass, "The computer is thinking...", and the user cannot move.
  Take back, another game or Resign drop the answer that waits.
