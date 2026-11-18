# The computer's chess piece lands

2026-11-18. Backlog item 64. Built and tested on the PC. Nothing here has
run on the board.

## 1. What was asked

The owner:

> the chase game moves the pieces so fast that if you are not looking you
> miss the move. Can we animate the piece landing on the new square, not
> like transitioning the whole way as it would look weird for knights, but
> making the landing piece shake a bit on land, not too shaky neither,
> something smooth. Show a preview on a gif or html render before doing any
> real work

("chase" is Chess.)

## 2. What was found

The computer's move is played in `ChessWindow::answer()`
(`core/src/shell/chess.cpp`) and shown by `sync()`, which paints again the
squares whose look changed: the piece is on its new square in the next
frame, with the two squares of the move in yellow. At levels 1 to 3 the
engine answers at once, so the answer appears in the frame after the
user's own move. The yellow is the only trace.

A square is 40 by 40 pixels and a piece is a 40 by 40 drawing painted at
the square's corner. The drawings leave 2 to 8 clear rows above the piece
(the kings 2, the bishops 3) and 3 below. The canvas a program paints on
has no clipping of its own beyond the part asked to be painted again.

## 3. The options considered

Drawn before anything was built, as GIFs on the chess window's reference
picture, the knight going from c6 to e5 (`tools/mockup/chess_landing.py`;
the pictures are in `docs/look/chess-landing/`). All four move only the
landing square, in whole pixels:

- **A, Shake**: the whole piece goes 3 pixels left and right and dies away
  in about half a second.
- **B, Drop and bounce**: it falls the last 4 pixels and bounces once.
- **C, Rock**: the foot stays and the head sways about 4 pixels for 0.6 s,
  each row of the drawing shifted by its height. This was the
  recommendation: a shake with the piece staying on its spot.
- **D, Drop, then rock**: a short fall and a gentler sway.

Not drawn: the piece travelling from its old square, which the owner had
ruled out in the request.

## 4. What was chosen

The owner chose **B, drop and bounce**, on a card. No reason was given;
C was recommended and not taken.

Decided while building, to confirm or overrule:

- Only the computer's piece lands. The user's own piece was put there by
  hand.
- The piece never leaves its square. It is drawn higher inside its own 40
  by 40, and rows that would go over the top are left out. A king has 2
  clear rows above it, so the top 2 rows of its cross are missing for the
  first 33 ms and 1 row for the next 33 ms. The other way was to let the
  piece overlap the square above and paint both, which on the top row of
  the board would have gone over the board's frame.
- There is no setting to turn it off.

## 5. What was built

- `draw_piece` takes a `lift`: the drawing is coloured that many rows
  higher in the scratch block, and the rows left at the bottom are clear.
- `kLanding`, fourteen heights, one every 60th of a second: 4 4 3 3 2 1 1
  1 2 2 2 1 1 1, then at rest. It is the curve of the preview (a fall of
  90 ms, then one bounce that dies away) sampled at 60 steps a second. In
  whole pixels the piece does not quite touch down between the fall and
  the bounce, and the second bounce of the formula rounds to nothing, so
  the landing takes 233 ms, as in the GIF the owner saw.
- `start_landing(square)` from `answer()`, when the computer's move has
  been played: a timer of the window's every 16.7 ms. Each tick takes the
  step from the clock (`os::now_us()`), not from a count, so a late tick
  skips steps and the landing does not last longer. The square is asked to
  be painted again only when the height changes: 7 paintings of one 40 by
  40 square in all, the first being the one that shows the move anyway.
- The landing ends early when the game's length changes (the user's next
  move, Take back, another game), and its timer stops with the window.
- No new OS call, no new memory: five small fields in the window.

Tested on the PC (`host/tests/test_chess_window.cpp`, `test_landing`): the
height of the piece at each of the fourteen steps, read from the screen;
that nothing but the square changes meanwhile and that every frame is what
a full repaint would give; a late tick; Take back while the piece lands;
the user's own piece not lifted; the window closed while a piece lands,
with all memory given back. 50 of 50 PC tests pass and the reference
pictures are unchanged.

## 6. What was learned

- The test's clock goes on 300 ms with each mouse event, and a move is
  played on the press, not the release: by the release the landing was
  over. The test presses, looks, and releases afterwards.
- The taskbar's clock is painted on a tick, so a test that lets a minute
  end between ticks sees "stale pixels" that are the clock's.

## 7. What is left

The board: that it looks smooth on the monitor, and that the timer at 60
ticks a second for a quarter of a second costs no missed refill and no
frame over budget (item 63).
