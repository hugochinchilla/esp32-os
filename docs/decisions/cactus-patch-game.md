---
title: "Chosen in the Cactus Patch game"
date: 2026-11-16
by: agent
status: to-confirm
area: programs
summary: "Cactus Patch, the mine-sweeping game: name and theme, rules, sizes; the right mouse button reaches windows"
---

# Chosen in the Cactus Patch game

Heading in the single-file log: "Chosen in the Cactus Patch game (2026-11-16); to confirm or overrule".


The mine-sweeping game from the owner's list, built on its own branch and
tested on the PC only (not yet compiled for or run on the board).

- Name and theme: Cactus Patch. Cacti hide under the tiles, the player
  plants orange pennants as markers, and the mascot's face on the button
  shows how the game is going. The owner asked for the original game's name
  and look; that is not done (no copies of another product's name or art,
  as the rest of this file says), but it plays the same way. A plain "Mines"
  with mines and flags of our own drawing is the alternative if he prefers.
- Rules as in the genre: left click opens, right click marks, the first tile
  opened is never a cactus (nor are its neighbours when the field has room),
  an empty tile opens its area, and a click on a number with all its markers
  placed opens the rest around it. F2 or the face button starts again.
- Three sizes in the Game menu: Small 9x9 with 10 cacti, Medium 16x16 with
  40, Large 30x16 with 99. Tiles are 16x16; the window fits the field and
  cannot be resized.
- The status row shows cacti left (cacti less markers) and seconds up to
  999, in black digits on white boxes. The clock starts with the first tile
  opened. Closing the window ends the game.
- No best times yet: nothing keeps settings between restarts.
- The right mouse button now reaches windows: a press in a client area
  calls `Window::right_click()`. No program used it before.
- The rules are in `core/games/cactus.hpp`, the window and its pictures in
  `programs/cactus_patch/cactus_patch.cpp`. Its tests and reference pictures are
  `host/tests/test_cactus.cpp` (pictures 50 to 54).
