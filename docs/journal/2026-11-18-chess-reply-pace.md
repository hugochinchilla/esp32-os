# The computer does not answer at once

2026-11-18. Backlog item 67. Built and tested on the PC; the firmware and
`chess.app` compile in the cloud. Nothing here has run on the board.

## 1. What was asked

The owner, after trying the chess game on the board with the landing of
item 64:

> on chess, wait at least a few ms before responding to a move, give 250
> to 350ms of minimal wait just for the sake of feel

The coordinator added a question from the board's report on item 64: some
steps of the landing were measured painting 10,880 pixels instead of one
square's 1,600. Is that a fault of the landing, and is it small to mend?

## 2. What was found

At levels 1 to 3 the engine looks one to three half moves ahead and has
its answer in a few milliseconds. `ChessWindow::answer()` played it as soon
as the job came back, so the computer's piece moved in the frame after the
user's own, or in the same one. Levels 4 and 5 think for up to 3 and 10
seconds, but answer as fast when the move is forced or the game is nearly
over.

The 10,880 pixels. The status line under the board is 320 by 14, 4,480
pixels, and a square is 1,600. The computer's move changes four squares
(where its piece was and where it is, in yellow, and the two squares of the
user's move, which lose theirs) and the status line, from "The computer is
thinking..." to "Your move.": 4 x 1,600 + 4,480 = 10,880. The user's click
measured at 12,480 is the same sum with five squares (the two of the move,
the two that lose the yellow, and one that loses its dot). So the large
paintings are, by this sum, the move itself being shown, once a move, in
the same piece of work as the landing's first step; the steps after it
paint one square, which is the 1,600 pixels in 6.1 ms the board also
measured, and what the PC test of item 64 checks at every step. This is
worked out from the figures, not seen on the board. Nothing in the landing
was changed. What a move costs to show (16 to 19 ms then) was measured
before the faster drawing of item 66 was merged, and is for the board to
measure again.

## 3. The options considered

For the wait, three readings of "250 to 350 ms of minimal wait":

1. **The move is shown no sooner than a length drawn between 250 and 350
   ms after the computer began to think, another length each move.**
   Thinking that takes longer is not made longer.
2. A fixed 300 ms. Simpler, and every answer at the low levels would come
   after exactly the same pause, which reads as a machine's delay.
3. 250 to 350 ms added after every answer, however long the thinking. It
   would make level 5 slower for nothing.

Not asked to the owner on a card: his message names the range and says it
is a minimum, which is the first reading.

## 4. What was chosen

The first, by the agent as the plain reading of the request; to confirm or
overrule. The game goes on waiting for the computer during the pause, with
the hourglass and "The computer is thinking...", because that is what the
user is waiting for; before, at the low levels, they were there for a frame
or not at all.

## 5. What was built

- `think()` notes the time and draws the length: 250,000 us plus the
  game's seed modulo 100,001.
- `answer()`, when the job has come back with a move that is still wanted:
  if less than that length has gone by, a timer of the window's for the
  rest (`reply_`), and `play_answer()` when it fires. Otherwise
  `play_answer()` at once. If no timer is to be had the move is played at
  once.
- `forget_answer()` (Take back, another game, Resign) and the destructor
  stop that timer: an answer found and waiting is dropped like one still
  being worked out.
- The first move of a game the computer plays as White waits the same.

Tested on the PC (`host/tests/test_chess_window.cpp`, `test_pace`): after
the user's move the game has one move more, not two, waits for the
computer with the hourglass, and paints nothing stale; at 249 ms the answer
has not come; it comes between 250 and 360 ms (the test looks every 10 ms);
Take back and a new game during the pause drop the answer for good; the
window closed during the pause gives back all its memory. The other tests
of the window now wait 400 ms where they expected the answer inside the
click. 53 of 53 PC tests pass and the reference pictures are unchanged.

Compiled in the cloud with ESP-IDF v5.5: the firmware builds and
`chess.app` (51,904 bytes) asks for nothing the firmware does not export.

## 6. What was learned

- The landing test took the colour of the square from the middle of its
  second row. A king or a bishop lifted four pixels reaches that row, so
  the test measured nothing for those pieces; it had passed only because
  the engine had not happened to answer with one. Which piece answers
  depends on the clock, and adding the pause moved the clock. The colour is
  now read in a corner.

## 7. What is left

The board: that the pause feels right to the owner, and the frames reading
again now that the user's move and the computer's are painted 250 ms
apart and item 66 is in.

## On the board (2026-11-19, 07b9272)

App-flashed on the BIOS layout with `chess.app` (51,904 bytes, unchanged in
size). Level 1 as White, six moves by click-click (e2-e4, d2-d4, g1-f3, b1-c3,
f1-e2, c1-e3). A screenshot taken 0.4 s after the second click (the capture
itself takes that long) showed the user's piece moved, the hourglass over the
board, "The computer is thinking..." and the computer's piece not yet moved,
every time; one second later each showed the computer's move at rest with its
two squares in yellow and "Your move.". Take back (Ctrl+Z) 0.1 s after a move,
three times: the board was whole each time with nothing moved by the computer
two seconds later. F2 > Play 0.1 s after a move: a fresh board, nothing moved
two seconds later. As Black the first move appeared after the pause (the
screenshot right after Play showed the hourglass and no move, one second later
the computer's h2-h4). Level 5 as White: still thinking 1.5 s after the move
and answered (e7-e5) well before 12 s. Closing Chess 0.1 s after a move left no
crash and the desktop went on. No missed refill of the video signal; free
internal RAM 191,311 before opening Chess and after closing it.

The frames reading over the six moves: 450 pieces of work, 54 painted, one
over budget (a mouse event of 26,044 us that painted only 2,404 us, 9,280
pixels), the user's move's painting 9,280 pixels and a tick of 10,880 pixels
(3.7 ms) for the computer's, other landing steps cheap (a few ticks of 25,000
to 27,000 pixels in 3.5 ms). The webcam was not used: whether the pause is
short and even by eye is for the owner to judge on the monitor.

