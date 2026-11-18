# A chess game: the design, before anything is built

2026-11-18. Backlog items 49 and 50. Nothing here is built: this entry is
the proposal the owner chooses from.

## What was asked

The owner, in the project chat:

> Add to the backlog:
>
> **The hourglass can be show on demand by apps.** An app blocked or
> thinking can set the cursor to show the hourglass while the cursor it's
> over the app. The app knows when it's entering a locking or slow path and
> requests it, then on completion goes back to normal cursor. The chess
> game will show the hourglass while the engine calculates a move.
>
> **A chess game** with an engine that can play against the user, theme
> like chess.com green and white, with option for brown and white (the
> board, pieces are always black and white). The screen shows what each
> player has captured from the other player as miniature pieces, me, the
> user sees captures below board, the engine captured pieces are above the
> board. Before starting a game, the user selects the difficulty level and
> if starts with black, white or random. The selection is remembered for
> future games.
>
> Show me designs before starting, and tell me which engine you choose to
> implement.

## What was found

- The highest number in the backlog was 47 on `main` and 48 (the beeper) on
  another branch, so the two items are 49 and 50.
- A program can already get the hourglass in two ways, and neither is what
  was asked. `os::ui::set_locked` shows it over the window but also takes
  the window's keys, clicks and its close button away (the Editor uses it
  while a save runs). `wm::Wait` shows it over the whole screen and is not
  an OS call. Item 49 is a third thing: the hourglass over the window, the
  window still alive. Its design is left to its own piece of work.
- The two games there are, Culebra and Cactus Patch, are built the same
  way: the rules in `core/src/games/` with no drawing in them and their own
  PC test, the window in `core/src/shell/`, and a seven-line file in
  `programs/` that builds both into a `.app` kept on C:. Chess fits the
  same three places.
- The screen is 640 by 480 with a taskbar of 28 lines. A board of 40-pixel
  squares with a row of captured pieces above and below, a status line, a
  menu bar and a title bar is 445 lines tall: it fits, with 7 to spare.
  Squares of 44 would not.
- The board has 256 colours: 8 levels of red, 8 of green, 4 of blue.
  The green of the common online board (118, 150, 86) lands almost exactly
  on a board colour (109, 146, 85). Its cream (238, 238, 210) does not:
  the nearest are white and a clearly yellow (255, 255, 170). The brown
  set lands well: (182, 146, 85) and (255, 219, 170).

## The mock-ups

Drawn by the system's own code, not by a paint program:
`tools/mockup/chess_mockup.cpp` opens real windows, a real menu bar and real
radio buttons on the PC build of the desktop, as `render_scenes` does, and
paints fixed positions. There are no rules and no engine in it. It is built
only when asked for (`cmake --build build/host --target chess_mockup`).

The pieces are original. `tools/mockup/chess_pieces.py` describes each of the
six once, as a handful of polygons and ovals on a 40 by 40 grid, and turns it
into pixels at 40 (the board) and 20 (the captured pieces). A pixel is
outline, body or detail; the game paints body and detail in the side's
colours, so six drawings serve both sides. No font and nobody else's set was
used.

In `docs/look/chess/`:

- `chess-1-green-thinking.png`: green and white, the user plays White and
  has just moved; the engine is thinking and the pointer over the window is
  the hourglass. The last move's two squares are yellow. Above the board,
  what the engine has taken (two bishops, two pawns); below, what the user
  has taken, and "+1": the user is a pawn ahead.
- `chess-2-brown-black-side.png`: brown and cream, the user plays Black, so
  the board is turned and Black is at the bottom. The user has picked up a
  knight: its square is yellow and a dot marks each square it can go to.
- `chess-3-new-game.png`: the dialog before a game. Five levels, and White,
  Black or Random.
- `chess-pieces.png`: every piece of both sides on every square colour, at
  twice the size, and the small ones.

Proposed with them, each one the owner's to change:

- Menus: Game (New game... F2, Take back, Resign, Close), Options (Board:
  Green or Brown; Show where a piece can go), Help.
- A move is two clicks (the piece, then the square) or a drag.
- The levels are told apart by how long and how deep the engine looks, and
  the lowest also by mistakes made on purpose. The times on the dialog are
  guesses until the engine has run on the board.
- Level, side and board colour are kept in `C:\.config\chess.ini`.
- The light squares of the green board are white, as the owner said. The
  cream of the online board would need two colours mixed in a checker
  pattern; the owner chose solid colours over mixed ones for the desktop.
- Not drawn: a list of the moves played. The window has no room beside the
  board without growing to the right; it could be a second window or a
  later addition.

## The engine: the options

What the machine gives: two cores at 240 MHz, no floating point worth
using in a search, 512 KB of internal RAM that is mostly spoken for, and
8 MB of external RAM that is slower for scattered reads. A loaded program
runs on its own task with a stack of a few kilobytes. The video must not
miss a refill, so a search has to be ordinary code at low priority that can
be stopped, with no lock held. And the repository takes only code that is
ours or under a permissive licence.

1. **An engine written for this project** (recommended). In
   `core/src/games/chess.cpp`: a board as an array with a border, a move
   generator checked on the PC by counting positions to a known depth
   (the usual "perft" figures), and a search of the classic kind: deeper
   one step at a time until the time is up, alpha-beta, captures followed
   to the end, the likeliest moves tried first, a table of positions
   already seen, and an evaluation from material and where each piece
   stands. About 1,200 to 1,500 lines. The table of positions is asked for
   in external RAM when a game starts (256 KB proposed) and given back when
   the window closes; without it the engine still plays, a little weaker.
2. **micro-Max, through mcu-max.** micro-Max is H. G. Muller's engine
   written to be as small as possible (about 2,000 characters of C);
   mcu-max wraps it for microcontrollers and is, as far as was checked
   from memory and not from the file, under the MIT licence. It runs on
   far smaller chips than this one.
3. **A port of a strong free engine** (Stockfish and its relatives).
4. **TSCP**, Tom Kerrigan's simple program, the size and style that would
   suit.

## What is recommended, and why

Option 1.

- Licence: nothing to check and nothing to carry. Every line is the
  project's, as with the two games there are.
- It fits how the repository works: rules in `core/` with no ESP-IDF
  header, tested on the PC before the board sees them. A chess engine is
  the kind of code where that pays most: the move generator can be proved
  right by numbers known for decades, and the search by positions with one
  right answer.
- Difficulty is ours to shape. The levels need a time limit, a depth
  limit and, for the lowest, deliberate mistakes; the search has to stop
  when the user closes the window or starts again; and it has to give way
  to the video. Those are simple in code written for them.
- Strength is not the limit. A plain alpha-beta engine of this kind plays
  well above a casual player on hardware much slower than this. How many
  positions a second the board manages is not known and will be measured;
  nothing in the proposal depends on a figure.
- It makes a better story for the film than a file brought in from
  elsewhere.

Why not the others:

- micro-Max is a feat of smallness, and the price is that it cannot be
  read: single-letter names and everything in one function. Changing how
  it picks a level, stopping it part-way or explaining it on camera would
  each be harder than writing a clear engine. It also leaves out parts of
  the rules that a full game needs (it promotes only to a queen, as far
  as is remembered; to check if this option is taken).
- Stockfish and its relatives are under the GPL, which the repository
  does not take, and are built for megabytes of tables and a network of
  weights. Far more than the game needs.
- TSCP is copyrighted and may not be redistributed without its author's
  permission.

The cost of option 1 is the work: it is the largest part of item 50, and
its bugs are ours. The PC tests are how that is kept in hand.

## What was built

Only what the proposal needs: the two scripts above, the mock-up target in
`host/CMakeLists.txt` (not part of the normal build), four pictures, the
two backlog rows and an entry under "Still open" in `docs/DECISIONS.md`.
No program, no OS call, no change to anything that runs on the board.

A third backlog row went in with the same change, asked for by the owner
in the project chat while this was being written ("podremos conectar tanto
un teclado como un raton por bluetooth? podemos sincronizar con 2
dispositivos?", then "si" to having it listed): item 51, a Bluetooth LE
keyboard and mouse connected together. It is only listed; its figures are
estimates.

The PC tests were run after the change to the CMake list. Nothing ran on
the board, and nothing needed to.

## What is left

The owner's choices: the engine, the look (the pieces above all), the
levels, the name. Then item 49, and item 50 in steps: the rules and their
tests, the search, the window.

## The owner's choice (the same day)

Asked on a card with three options (our own engine, micro-Max through
mcu-max, something else), the owner chose **our own engine**. It is
recorded in `docs/DECISIONS.md`. The look, the levels and the name have not
been answered yet. The rules and the search do not depend on them, so they
can be written and tested on the PC meanwhile, as their own piece of work.

## The owner's first look at the mock-ups (the same day)

> the captured pieces show non regular spacing between them.
>
> Produce a new pieces theme, more symbolic, less detail.
>
> Pieces move either by dragging or by click on piece and click on
> destination

- The spacing: the first drawing put pieces of one kind close together
  and left a gap between kinds, as the online boards do. The owner read it
  as uneven. Now every captured piece is one step of 18 pixels from the
  last; all fifteen a side can lose still fit beside the "+1".
- A second set of pieces, "symbols", in `tools/mockup/chess_pieces.py`
  beside the first ("classic"): each piece is its sign and little else. A
  flat slab to stand on, heavier shapes, three points on the queen's crown
  instead of five, and no inner lines but the bishop's cut and the knight's
  eye. The mock-ups use it; `chess-pieces.png` shows the new set on the
  left and the first on the right. Which of the two stays is the owner's
  to say.
- Moving a piece by dragging it, or by a click on it and a click on where
  it goes: decided by the owner, as proposed.

The three mock-ups were drawn again. PC only; nothing built.

## Piece sets become themes (the same day)

> add more detail to the pieces by contour, not by internal lines, but
> record this as a theme, the app will have custom pieces to choose

So the plain set stays, and the game is to offer sets of pieces to choose
from, as it offers board colours: Options > Pieces. Decided by the owner.

A third set was drawn, "Contour": turned pieces seen against the light.
Collars, waists, a stepped foot, the cut in the bishop's mitre, the
knight's ears and mane and the five points of the queen's crown are all in
the outline, and nothing is drawn inside. The bishop's cut is a real bite
out of the shape (`notch` in the script), so the outline follows it.

The three sets, in the order of the menu: Contour, Symbols, Classic (the
first one drawn, with inner lines). `chess-pieces.png` shows them side by
side. The first three mock-ups use Contour; `chess-4-pieces-symbols.png`
shows the Symbols set with the menu open at Pieces, and
`chess-4-pieces-classic.png` the Classic set. Which set a new installation
starts with, and whether Classic is kept at all, are the owner's to say.
The set chosen would be kept in `chess.ini` with the rest.

What a set costs: six drawings at two sizes, about 3 KB of the program's
file if kept at two bits a pixel. The mock-up keeps them as text rows,
which is larger.

PC only; nothing built.
