# The chess engine on the board, and the five levels

2026-11-18. Backlog item 50. Follows [the engine](2026-11-18-chess-engine.md),
which ended with a table of levels proposed and nothing run on the board.

## What was asked

The engine's entry left two things: the board check, and the owner's word
on the levels once its figures were in. He was shown the figures and a
card with four ways to set the levels, and chose:

> Level 4 deeper, level 5 at 10 s

## What the board measured

Board batch 8, commit 4a3fe71, with `tools/bench/chess_bench` copied to C:
(26,180 bytes, every name it asks for exported). The program lets each
level think about three positions (the first move, a middle game, an end
game) on its own task and writes what it measured:

```
chess bench: engine 12984 bytes, table 262144 bytes
level position move depth nodes ms nodes/s stack
1 1 g1f3 1 40 2 20000 224
1 2 c3d5 1 2295 69 33260 1264
1 3 b4f4 1 64 2 32000 512
2 1 b2b3 2 512 12 42666 512
2 2 c4d5 2 9314 285 32680 1552
2 3 b4f4 2 386 10 38600 800
3 1 b1c3 3 2006 58 34586 800
3 2 c3d5 3 9974 271 36804 1328
3 3 b4f4 3 1052 23 45739 896
4 1 d2d4 5 10768 306 35189 1344
4 2 c3d5 5 17852 560 31878 1808
4 3 b4c4 5 4967 118 42093 1392
5 1 b1c3 8 145152 10028 14474 2288
5 2 c3d5 6 80634 5525 14594 1856
5 3 e2e3 8 154880 10007 15477 2240
total: 439896 nodes in 27276 ms
```

- **Speed**: 32,000 to 45,000 positions a second while the program's task
  had its normal priority: about one fortieth of the PC's.
- **Level 5 ran at 14,500 a second.** A program that does not answer for
  about two seconds is put at the lowest priority until it answers again,
  and the bench thinks inside its own event. It got about 5% of a
  processor's time by the Task list's figure while the desktop was being
  used. So the 10 s of level 5 bought less than half of what they could:
  depth 6 to 8. How the window runs the search (a background job at a
  priority of its own) decides this figure, not the engine.
- **Stack**: 2,288 bytes at most below `think()`; 11,016 of the task's
  16 KB were never used. A job with 8 KB has room to spare.
- **The video**: no refill missed. The desktop stayed usable throughout.
- **Memory**: back exactly after the window closed (internal 203,747,
  external 6,333,296).
- No watchdog line, no panic.

## The options for the levels

Levels 1 to 3 answer in under 0.3 s and are told apart by depth and by
mistakes on purpose; nothing to choose there. Level 4 as proposed (depth 5)
answered in 0.1 to 0.6 s and level 5 took its whole 10 s: one step between
them much larger than the others.

1. **Level 4 deeper, level 5 at 10 s** (recommended): level 4 looks 6 half
   moves ahead for up to 3 s.
2. Keep the table as measured: level 4 at depth 5, about half a second.
3. A quicker top level: level 4 as in 1, level 5 at most 5 s.
4. A stronger top level: level 4 as in 1, level 5 up to 20 s, about one
   half move deeper.

## What was chosen, and why

The owner chose 1. It evens the steps: level 4 becomes a real opponent
without a long wait, and 10 s keeps the top level strong but bearable.
Not 2, for the gap. Not 3 or 4: nobody has played it yet, and 10 s is the
middle; the figure is one number in a table if playing it says otherwise.

| Level | Depth (half moves) | Time at most | Noise (pawns) |
| --- | --- | --- | --- |
| 1 | 1 | 1 s | 2.5 |
| 2 | 2 | 1 s | 1.2 |
| 3 | 3 | 2 s | 0.5 |
| 4 | 6 | 3 s | none |
| 5 | as deep as the time allows | 10 s | none |

## What was built

One row of the table in `core/src/games/chess.cpp`. In the test, level 5
is held to depth 7 against level 4 (it was 6, which is now level 4's own
depth), and the two deepest pairs play one game each way instead of two,
for the time: +4, +4, +2 and +2. The `chess` test takes about 40 s in the
checked build.

Tested on the PC (the whole suite). The new row has not run on the board:
at 35,000 positions a second, depth 6 from the bench's three positions
should take one to three seconds, and the 3 s limit holds it either way.

## What is left

The window. It should run the search as a job with its own priority, so
that level 5 gets its 10 s at full speed; that needs the OS call for jobs
that programs kept in files do not have yet (see the engine's entry).
