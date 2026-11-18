# Where a slow repaint's time goes

2026-11-18. Backlog item 65. A measurement, not a fix: the owner chose to
measure before anything is changed. The profile was taken on the PC. The
paint bench was built and tested on the PC; the firmware with it has not
been compiled in the cloud and has not run on the board: its figures come
from the board turn.

## What was asked

The frame-budget benchmark ([journal](2026-11-18-frame-budget.md)) found
that painting a large window in one go takes 30 to 59 ms against a frame of
16.8 ms: a window dropped, brought to the front or maximised. The owner
chose to measure before fixing. The coordinator's note for this thread: find
out where that time goes (each program's drawing against the window
manager's own work), on the PC and, if needed, with one board measurement;
then bring the owner two or three options with estimated figures and their
cost in memory, for example a kept picture of each window in external RAM
against faster drawing in the Terminal and Chess. Build nothing until he
picks.

## What the board's figures already said

The three benchmark runs kept every slow piece (`frames-1.json` to
`frames-3.json` in the project's shared folder). Read again for this:

- The window manager's own work is small. A piece that paints is almost all
  painting (39.1 of 39.3 ms), and most of the painting is one window's own:
  Chess brought to the front, 39.1 ms of painting, 33.2 ms of it inside
  Chess's window; the Terminal, 45.3 ms, 39.3 ms of it the Terminal's.
- Chess's time hardly depends on how much is painted. A piece moved paints
  12,480 pixels and takes 16.4 to 16.8 ms, a frame by itself; the whole
  window, 176,748 pixels, takes 33 to 40 ms. Fourteen times the pixels,
  twice the time.
- The Editor maximised paints 289,280 pixels in 21.2 ms, 73 ns a pixel. The
  Terminal paints 192,380 in 39.3 ms, 204 ns a pixel, for the same kind of
  picture (text on one colour).
- Going back from maximised takes 39 to 42 ms with only 13 ms of it the
  Editor's: the rest is everything the big window uncovered, the desktop,
  the taskbar and four other windows, each painting its part.

## The profile on the PC

A scratch program (not kept in the repository) started the shell as the
tests do, opened one program at a time and repainted its window 300 times,
with a counter in each drawing call of `gfx::Canvas`: calls, pixels written
and time. Built with `-O2` and no sanitizers. The PC is about 80 times
faster than the board for all three big windows (Chess 0.51 ms against 33
to 40, the Terminal 0.47 against 39, the Editor 0.26 against 15 to 21), so
the shares below should hold on the board within reason; the absolute
figures do not.

| Window | Pixels | PC time | Where it goes |
| --- | --- | --- | --- |
| Editor, 200 lines of text | 145,200 | 0.26 ms | 69 % in `text_cells` (94,050 pixels), 12 % the title bar's fade, 8 % fills |
| Terminal, full of text | 168,656 | 0.47 ms | 63 % in `text_cells` (144,000 pixels), 8 % the title bar's fade, about a tenth the scroll bar, painted once for each of 28 bands |
| Chess, a game begun | 154,112 | 0.51 ms | 66 % outside any drawing call; pixels written 2.05 times the window's |
| Chess, 104 x 120 of the board | 12,480 | 0.094 ms | 73 % outside any drawing call; pixels written 4.5 times the area |
| Chess, one square | 1,600 | 0.010 ms | pixels written 11 times the area |
| File manager | 120,000 | 0.095 ms | 30 % the title bar's fade; pixels written 1.9 times the window's |
| Culebra | 92,976 | 0.29 ms | 58 % in `blit` (370 calls) |
| Desktop and taskbar alone | 307,200 | 0.17 ms | 79 % in `fill_rgb` |

What that says:

- **The Terminal and the Editor: the text itself.** Both write their text
  with `Canvas::text_opaque`, which is `text_cells` (`core/src/gfx/canvas.cpp`):
  for every pixel it works out which character the pixel is in (a division
  by the font's width), finds the glyph's row again and calls a function for
  the background colour. Nothing is drawn twice; each pixel is just dear.
  On the PC it costs 2 ns a pixel where a plain fill costs 0.15 to 0.6.
- **Chess: the board is painted once for each 12 lines of it.** Chess draws
  a square's colour and then the piece over it, so its picture goes through
  the scratch band (`Canvas::layered`, the answer of 2026-11-16 to pixels
  painted twice). The band is 12 lines; the whole of `paint_in` runs once
  for each band, 38 times for the window. Each time it goes over the 64
  squares, and for every square that touches the band it fills the square
  and unpacks the piece's 1,600 pixels from two bits each into a scratch
  picture (`draw_piece`), to show 12 lines of it. A square is 40 lines, so
  each piece is unpacked about four times. That unpacking is the 66 %
  outside the drawing calls. The band is also as wide as the board whatever
  part is asked for, which is why one square costs 11 squares' worth of
  writes and a moved piece costs a frame.
- **Every window: the title bar's fade.** `fill_fade` works a colour out for
  every column and dithers it; on the PC it is 6 to 30 % of a window's
  repaint for 3 % of its pixels.
- **The window manager**: working out which parts of which window show is
  not visible in the profile.

## What the PC cannot say

Two things decide between the options, and both are the board's:

- **How fast memory is copied to the screen.** A kept picture of each
  window in external RAM turns "bring to the front" into a copy from
  external RAM to the framebuffer, which is external RAM too. The start-up
  memory test writes and reads back about 20 MB a second; if a copy runs at
  that speed, 180,000 pixels take 9 ms and a maximised window 14 ms, close
  to the frame. It has to be measured with the video interrupt running.
- **What each kind of drawing costs there**, a pixel, to put milliseconds on
  "faster text" and "Chess without the band".

## What was built to measure it

- `Wm::paint_bench` (`core/src/wm/paint_bench.cpp`) and the debug link's
  request 0x18, `tools/dbg.py paintbench`. On the real screen, over the same
  480 x 300 pixels (80 by 25 characters), the shortest of three: `fill`,
  `fill_rgb`, `fill_fade`, a fill through the scratch band, `text_opaque`,
  the same text written by a straight loop that reads each glyph row once (a
  sketch that nothing uses, to see what the text could cost), `text`,
  `blit_opaque` and `blit` of 84 pictures of a chess square's size, a copy
  from external RAM (another part of the screen's own memory) and a copy
  from internal RAM. Then every open window painted alone, whatever covers
  it: its frame, and what the program paints. Then everything is painted
  again. The reply is text.
- `tools/bench/desktop_frames.py --paint`: opens the benchmark's five
  windows and prints the paint bench three times, and once more with the
  Editor maximised.

Tested on the PC: `tools/test_dbg_sim.py` asks the simulator for the bench,
checks every line is there and that the picture is the same afterwards; the
script's `--paint` runs to the end against the simulator. All the PC tests
pass.

## What is left

- The board turn, and then the options for the owner with the board's
  figures in them. Nothing that changes how windows are painted is built
  before he picks.

## On the board (2026-11-18, b63d78b)

The firmware compiled and linked in the cloud before the turn. The board
thread ran the bench with the benchmark's five windows open, three times
and once with the Editor maximised; the runs agree within about 5 %, and
`dbg.py misses` stayed at 0. The figures below are as the coordinator
relayed them from the board thread's report (the raw output stayed in that
thread). Free memory with the five windows open: 161,359 bytes internal,
5,163,708 external.

Each kind of drawing over 480 x 300 = 144,000 pixels (the tiles: 134,400):

| Drawing | Time | A pixel |
| --- | --- | --- |
| `fill` | 3.8 ms | 26 ns |
| `fill` through the scratch band | 4.1 ms | 29 ns |
| `fill_rgb` | 5.4 ms | 37 ns |
| `fill_fade` | 216.5 ms | 1,504 ns |
| `text_opaque` | 22.5 ms | 156 ns |
| the same text, the straight loop | 4.9 ms | 34 ns |
| `text` | 9.9 ms | |
| `blit_opaque`, 84 squares | 6.0 ms | 44 ns |
| `blit`, 84 squares | 8.9 ms | |
| copy from external RAM | 6.2 ms | 43 ns |
| copy from internal RAM | 3.8 ms | 26 ns |

Each window painted alone, its frame and its inside:

| Window | Frame | Inside |
| --- | --- | --- |
| Editor (empty) | 7.6 ms | 6.2 ms |
| File manager | 6.8 ms | 5.7 ms |
| Terminal | 7.9 ms | 25.4 ms |
| Culebra | 5.8 ms | 10.4 ms |
| Chess | 7.0 ms | 28.8 ms |

Everything painted again: 14.9 ms with no window open, 69.9 to 71.0 ms with
the five, 20.8 ms with the Editor maximised over them.

What the board adds to the PC's profile:

- **Writing to the screen has a floor of 26 ns a pixel**, 38 MB a second.
  A window of 180,000 pixels cannot be painted in less than 4.7 ms, the
  whole screen in less than 8.
- **The title bar is the surprise.** `fill_fade` costs 1.5 µs a pixel, 58
  times a plain fill: it walks down each column, and works a colour out
  with three divisions and a dither for every column. A window's frame
  takes 6 to 8 ms, nearly all of it the 18 lines of fade, and five frames
  are 35 of the 70 ms of a whole repaint. The PC showed it as a tenth of a
  window; on the board it is as much as the Editor's whole inside.
- **The text loop is 4.6 times dearer than it needs to be**: 156 ns a pixel
  against 34 for the straight loop, which is close to the floor.
- **The Terminal's and Chess's insides** are the two outliers the PC
  pointed at: 25.4 and 28.8 ms.
- **A copy from external RAM runs at 43 ns a pixel**, 23 MB a second. A kept
  picture of a 180,000-pixel window goes to the screen in 7.7 ms; a whole
  screen in 13.2 ms. That fits in a frame, but not by much.

Meanwhile the taskbar's two-second timer was fixed on another branch (PR
65): the benchmark now counts 40 to 41 pieces over budget, all of them
repaints (drops 14, to the front 12, maximise 6).

## The options, for the owner

Estimates, worked from the board's rates above; none is built.

**A. Faster drawing, no kept pictures.** Three changes in the drawing code:
the title bar's fade worked out once and copied (a few KB); `text_cells`
as the straight loop; Chess painted square by square, each pixel once,
from pieces unpacked once when the window opens (about 19 KB), without the
band. Estimated: a frame 7 ms to about 1.5; the Terminal 33 ms to about 8;
Chess 36 to about 9, and a moved piece 16 ms to under 1; the Editor full
of text about 22 to about 6. Bringing any one window to the front then
fits in a frame. What does not: going back from maximised, which paints
the desktop and every window (70 ms to an estimated 45 to 50), and a drop
that uncovers several windows (an estimated 15 to 25 ms). Memory: about 20
KB. It also makes everyday painting faster: typing and scrolling in the
Terminal, a chess move.

**B. A kept picture of every window in external RAM.** Each window draws
into its own picture and the window manager copies to the screen. To the
front, a drop and back from maximised become copies: 7.7 ms for a window,
at most 13 ms for a screenful. Memory: a byte a pixel, 681 KB for the
benchmark's five windows and up to 289 KB more for each one maximised, of
5.1 MB free. It does not make drawing faster: the first paint, a resize
and scrolling the Terminal still cost what they cost today, plus the copy,
and a moved chess piece stays at a frame. It reverses "Windows keep no
stored picture of themselves", and it is the larger change: every program's
painting goes through it.

**C. A now, measure again, then B only for what is still slow.** After A,
what is left over budget is the repaint of many windows at once, which is
exactly what B is good at, and B's copies would then sit on top of cheap
drawing instead of slow drawing.

The agent recommends C: A first.
