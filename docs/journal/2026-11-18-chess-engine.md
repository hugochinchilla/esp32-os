# The chess engine: the rules and the search, tested on the PC

2026-11-18. Backlog item 50, its first step. The design and the owner's
choices are in [the design entry](2026-11-18-chess-design.md); this is the
part that plays. No window yet, and nothing has run on the board.

## What was asked

The owner, in the project chat (the whole request is in the design entry):

> A chess game with an engine that can play against the user [...] Before
> starting a game, the user selects the difficulty level and if starts with
> black, white or random. [...] tell me which engine you choose to implement.

He chose, on a card, an engine written for this project. This piece of work
is that engine: the rules and the search in `core/src/games/chess.cpp`, with
PC tests, apart from the window and the look, which another thread owns.

## What was found

- The two games there are keep their rules in `core/src/games/` with a
  header in `core/include/core/games/` and a test in `host/tests/`. Chess
  goes in the same three places.
- A program kept in a file runs on a task with 4 KB of stack unless its
  header asks for more, and a job (`core/job`) gets 6 KB unless asked. A
  search calls itself once for every half move it looks ahead, so what one
  call puts on the stack decides how deep it can go.
- A program kept in a file can reach only what the firmware exports by
  name (`tools/gen_exports.py`): the OS calls, a few dialogs, a short list
  of C functions. `job::jobs()` is not among them, and there is no OS call
  that starts a job or gives the processor away. The Editor starts its save
  job directly because it is built in. **The chess window will need an OS
  call for a background job** (or will think on its own task and let the
  system call it "not responding"); that is the window's piece of work to
  settle, and item 20's rule says the call is added rather than gone
  around.
- The PC tests are built with the address and undefined-behaviour checkers
  and without optimisation. Counting millions of positions that way takes
  minutes.

## The options considered

**The board.** (a) 64 squares and a test for the edge at every step; (b) a
120-square array, the board inside a border of "off the board" squares;
(c) bitboards, a 64-bit word per kind of piece. Chosen: (b), as the design
entry said. Bitboards are the fast way on a 64-bit processor; the ESP32-S3
is 32-bit and has no instruction to find the lowest set bit of 64. The
border makes the generator short and easy to read aloud.

**Legal moves.** (a) Generate only legal moves, working out pins; (b)
generate moves by how the pieces move, play each, and throw away those that
leave the king attacked. Chosen: (b). It is a third of the code and is
checked exactly by the position counts below; (a) is faster by perhaps a
quarter and far easier to get subtly wrong.

**Where the search keeps its lists of moves.** A position has up to 218
moves. (a) An array in every call: 1 KB of stack per half move, 30 KB for a
modest search; (b) one array in the engine's own memory that all the calls
share, each using the part above the one before. Chosen: (b). A call of the
search then takes about 100 bytes of stack on the PC (2.5 KB for a whole
search to depth 5, 3.5 KB to depth 9), and the engine object is 13 KB that
the window asks the Memory port for.

**How the search gives way.** (a) The engine starts its own task; (b) it
sleeps now and then; (c) it is plain code that calls two functions it is
given, about every 256 positions: one tells the time, the other may give
the processor away and may say "stop". Chosen: (c). `core/` has no tasks
of its own to start, the PC tests need a clock they control, and the
window is the one that knows whether it runs a job. `stop()` can also be
called from another task.

**The table of positions already seen.** Memory for it is handed in by the
caller (256 KB proposed in the design; any size works, 12 bytes a
position), or not at all: the engine then plays the same way and looks at
more positions (14,198 against 11,325 for the first position to depth 6).

**The evaluation's numbers.** (a) Tables of square values published by
others (several are in wide use); (b) tables made from a few rules by a
`constexpr` function. Chosen: (b): the middle is better than the edge,
pawns want to advance, the king hides while there are queens and comes out
afterwards. Nothing typed in from elsewhere, so nothing to credit, and the
rules can be read. On top: passed, doubled and lonely pawns, the two
bishops, rooks on open files, pawns in front of a castled king, and a
push-the-king-to-the-edge term so that it can mate with a queen or a rook.

**The levels.** The design says: told apart by how long and how deep, the
lowest also by mistakes on purpose. Mistakes are made by "noise": every
move within N hundredths of a pawn of the best gets a random bonus below N,
and the highest total is played; never when there is a mate to give or
avoid. The table, in `chess.cpp`, is a **proposal for the owner**, to be
set once the board's speed is known:

| Level | Depth (half moves) | Time at most | Noise (pawns) |
| --- | --- | --- | --- |
| 1 | 1 | 1 s | 2.5 |
| 2 | 2 | 1 s | 1.2 |
| 3 | 3 | 2 s | 0.5 |
| 4 | 5 | 4 s | none |
| 5 | as deep as the time allows | 10 s | none |

Levels 4 and 5 get 0.15 of noise in the first six moves, or every game
would open the same way: there is no book of openings.

## What was built

- `core/include/core/games/chess.hpp`, `core/src/games/chess.cpp`:
  `Position` (the rules: every move including castling, taking in passing
  and promotion to any piece; check, mate, stalemate; the draws by
  repetition, fifty moves and bare material), `Game` (a game from the first
  move: the moves, what each side has taken for the rows of small pieces,
  take back) and `Engine`.
- The search: deeper by one half move at a time until a limit; alpha-beta
  with the best move so far first; captures followed to the end; the table
  of positions; killer and history ordering; the null move; later quiet
  moves looked at less deeply; a check extends the line.
- Limits: depth, time, positions. It always returns a legal move, even
  when stopped at the first poll.
- `Engine::think` works on a copy of the position, so the window can paint
  its own while a job thinks.
- `host/tests/test_chess.cpp` (test `chess`), and `chess.cpp` compiled with
  `-O2` on the PC even in the checked build.
- `tools/bench/chess_bench/`: a small program for the board check, not one
  of the system's programs. It lets each level think about three positions
  and writes the figures to `C:\chess_bench.txt`. `tools/build_programs.sh`
  builds it when `EXTRA_PROGRAMS` names its folder.
- On the board `chess.cpp` is left out of the core library, like the other
  games: it will be part of the Chess program's file.

## How it was tested

On the PC only.

- **The rules, by counting.** The number of ways a game can go on for N
  half moves from six positions, against the figures every chess program is
  checked with: the first position to depth 4 (197,281), "Kiwipete" to 4
  (4,085,603), and four more chosen for castling, pins, taking in passing
  and promotions. All equal. (The first position to depth 5, 4,865,609, was
  equal too; it is left out of the test for its time.)
- The key and the running totals after every move made and taken back in
  those walks equal what the board alone gives.
- The evaluation gives the same figure with the colours swapped.
- Endings: the two-move mate, stalemate, the bare-material draws, three
  times the same position (and not when the castling rights differ), fifty
  moves, positions that cannot be.
- Positions with one right answer, with and without the table: mate in
  one, mate in two (score "mate in 3 half moves"), a hanging queen, a
  knight fork, a promotion by capture, a perpetual check found as a draw.
- The engine against itself mates with king and queen (19 half moves), king
  and rook (37) and king and pawn (25).
- Limits: depth exact; positions within one poll; time on a pretended
  clock; stop from the poll, at the very first poll, and by `stop()`; the
  same question twice gives the same answer; a position with 218 moves.
- Levels: the table grows; the same seed gives the same move and other
  seeds others; noise never passes up a mate; each level against the one
  below over four games (by depth and noise, no clock): +4, +4, +4 and,
  for 5 against 4 with 5 held to depth 6, +2.

The whole PC suite passes (45 tests). `chess` takes about 20 s of it.

Speed on the PC, for scale only: 1.9 million positions to depth 9 in 1.2 s
(1.6 million a second). The board will be tens of times slower; how many
is what the board check is for.

## What was not tested

Nothing ran on the board. In the cloud, `tools/build_programs.sh` built
the bench program, and so the engine, with the ESP32-S3 compiler (`-Os`,
warnings as errors): `chess_bench.app` is 26,204 bytes, about 17 KB of it
the engine. Everything it asks the system for by name is an OS call or on
the list in `tools/gen_exports.py` (checked by reading the list, not by
the script's own check, which needs a firmware build). The firmware itself
was not built in the cloud: ESP-IDF's Python packages could not be
installed there this time. The firmware does not contain the engine, so
nothing in it changed.

## What was learned

- The first perpetual-check test position was wrong, not the engine: the
  defender had a queen that could step between. The engine's "you are
  simply lost" was right.
- A test position with a single legal move comes back at depth 1 by design
  (nothing to choose), so its score says nothing about a mate two half
  moves away.
- Level 1's noise of 2.5 pawns never passes up a free queen. Its weakness
  comes from looking one half move ahead: it takes what is defended and
  leaves things hanging. Whether that is the right kind of weak is for the
  owner to judge by playing it.

## What is left

- The board check: positions a second, think time and depth per level, the
  stack used, memory given back, and no missed refills while it thinks.
- The owner's word on the level table once those figures are in.
- The window (the rest of item 50), which needs item 49 and a way for a
  program in a file to think in the background (see "What was found").
