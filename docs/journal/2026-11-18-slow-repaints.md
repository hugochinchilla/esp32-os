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
