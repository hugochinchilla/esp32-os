---
title: "Chosen in the second review round"
date: 2026-11-17
by: agent
status: to-confirm
area: desktop
summary: "Start button width, Display's colour list and colour chooser, Terminal scroll bar and maximising (backlog 41)"
---

# Chosen in the second review round

Heading in the single-file log: "Chosen in the second review round (2026-11-17, the agent's, to confirm)".


Backlog item 41, asked by the owner. Journal:
`docs/journal/2026-11-17-review-round-2.md`.

- The Start button is 60 wide: two clear pixels on each side (the owner
  suggested one; one leaves the sides unequal).
- Display's list is 15 colours the board shows exactly, plus "Chinchilla
  blue (dithered)". The default was left dithered here and made solid the
  same day (see "The default desktop colour is solid"). "Lilac" is gone
  from the list.
- "Other..." opens a grid of the board's 256 colours, not a 24-bit picker
  and not sliders: everything it offers is undithered. The grid is one
  sheet of 16 by 16: a square of 8 by 8 for each level of blue, red
  running back in the right-hand squares and green running back in the
  lower ones, blue going round the four: dark at the top, the light
  colours across the middle, the blues at the bottom. The owner's choice
  (2026-11-17, "I think I like 3 more") after a hue-sorted grid, a strip
  of greys, a 32 by 8 sheet and two other folds were shown to him and set
  aside: `docs/journal/2026-11-17-colour-chooser.md`.
- A colour without a name is kept in `desktop.ini` as `#RRGGBB`.
- The chooser is `colour_dialog()`, for any program, as the file dialogs
  are (the owner's request): in the asking program's process, modal to its
  window, nothing kept between two openings.
- The Terminal's grid follows its window (20x4 to 104x36, opening at
  80x25) rather than staying 80x25 or scaling the font. Lines already
  written are not wrapped again. It has a scroll bar over its history.
