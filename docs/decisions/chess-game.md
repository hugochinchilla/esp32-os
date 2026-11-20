---
title: "The chess game"
date: 2026-11-18
by: owner
status: decided
area: programs
summary: "Chess: own engine, moving by drag or click, piece sets, board colours, levels (backlog 50)"
---

# The chess game

Heading in the single-file log: "The chess game (the owner's decisions, 2026-11-18)".


Backlog item 50; the mock-ups are in `docs/look/chess/` and the reasoning
in `docs/journal/2026-11-18-chess-design.md`. Designed and approved; built
and tested on the PC, not yet run on the board.

- The engine is one written for this project in `programs/chess/chess_game.cpp` (it was in `core/src/games/`), tested
  on the PC (chosen on a card). Not micro-Max (through mcu-max), and not a
  port of a larger engine.
- A piece is moved by dragging it, or by a click on it and a click on
  where it goes.
- What each side has taken is shown as small pieces, evenly spaced: the
  engine's captures above the board, the user's below, with the lead in
  pawns.
- The game offers sets of pieces to choose from (Options > Pieces), as it
  offers board colours (Options > Board). Three sets, drawn by
  `tools/mockup/chess_pieces.py`: Contour (the detail all in the outline),
  Symbols (plain signs) and Classic (with inner lines).
- Approved as shown ("perfect, go ahead"), and so to confirm only if it
  turns out not to be what he meant: Contour is the set a new installation
  starts with; squares of 40 pixels; green with white squares or brown
  with cream ones; the last move in yellow, dots where a picked piece can
  go, letters and numbers inside the squares, the board turned so the
  user's side is at the bottom; five levels of difficulty, and White,
  Black or Random, in a dialog before each game; everything chosen kept in
  `C:\.config\chess.ini`; the program is called "Chess".
- The engine as built (`docs/journal/2026-11-18-chess-engine.md`): a
  120-square board, moves checked by playing them, one shared list of
  moves in the engine's own 13 KB (about 100 bytes of stack per half move
  looked ahead), a table of positions in memory the caller hands in, and
  no task of its own: it calls a clock and a poll it is given, and the
  poll or `stop()` ends it. The evaluation's tables are made from rules,
  not copied.
- The five levels (the owner, on a card, 2026-11-18, with the board's
  figures: about 35,000 positions a second;
  `docs/journal/2026-11-18-chess-levels.md`): depth 1, 2 and 3 with
  mistakes on purpose of 2.5, 1.2 and 0.5 pawns, answering at once; level
  4 looks 6 half moves ahead for at most 3 s; level 5 as deep as 10 s
  allow. Not level 4 at depth 5 (half a second, too far below level 5),
  and not 5 s or 20 s for level 5.

- The window as built (`docs/journal/2026-11-18-chess-window.md`), PC
  only so far. The engine thinks in a background job a program starts by
  an OS call (`os::start_job`, asked for by the coordinator from the
  engine's findings), on a block of memory apart from the window, which
  may close under it. Chosen on the recommended option and confirmed by
  the owner as a whole when shown the result ("perfect", 2026-11-18):
  the job runs at priority Low and rests a tick about four times a second
  (`os::job_rest`); the level's time is told by a timer of the window's,
  the engine having no clock; a pawn on the last rank asks in a dialog of
  four buttons what it becomes; a piece that can be taken is marked by the
  corners of its square instead of the dot; in a theme with a dark face
  Black's pieces off the board are outlined in the text colour; the
  dialog's lines say what the levels now do; Take back also undoes a
  resignation and Resign does not ask; where there are no tasks (the PC's
  plain desktop, the browser) the computer stops at 200,000 positions.
