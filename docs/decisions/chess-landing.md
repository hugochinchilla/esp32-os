---
title: "The computer's chess piece lands"
date: 2026-11-18
by: owner+agent
status: decided
area: programs
summary: "The computer's piece drops 4 pixels onto its square and bounces once, 233 ms (backlog 64); details to confirm"
---

Backlog item 64; the story is in
`docs/journal/2026-11-18-chess-landing.md` and the four landings drawn are
in `docs/look/chess-landing/`. PC only so far.

- The owner, on a card: the piece the computer plays falls the last 4
  pixels onto its square and bounces once, 233 ms in all, so that a move
  is seen by someone who was not looking.
- Not chosen: a sideways shake, a rocking piece (the recommendation), a
  fall followed by rocking; and not a slide from the old square, which the
  owner ruled out in the request because it would look odd for knights.
- As built, to confirm or overrule: only the computer's piece lands, not
  the user's; the piece stays inside its square (a king's top two rows are
  cut for two frames); there is no setting to turn it off.
- Only the landing square is painted again, and only when the height
  changes: 7 paintings of 40 by 40 pixels a move.
