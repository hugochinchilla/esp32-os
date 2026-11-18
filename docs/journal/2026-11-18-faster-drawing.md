# Faster drawing: the fade, the text and Chess

2026-11-18. Backlog item 65, its second half. Built and tested on the PC;
the firmware and `chess.app` compile and link in the cloud (ESP-IDF v5.5).
Not yet run on the board: the "after" column of the tables below is empty
until the board turn.

## What was asked

The measurement ([journal](2026-11-18-slow-repaints.md)) ended in three
options: A, faster drawing; B, a kept picture of every window in external
RAM; C, A first, measure again, B only for what is still slow. The owner
asked for each fix in A to be explained, and then:

> que bueno, haz ambos fixes, y añade una nota de esto para el video de
> youtube

"Ambos" where three fixes had been listed: read as the fixes of A, all of
them, with B left for after the next measurement, which is option C. The
note for the video is the last section of this entry.

## What was found, in short

From the paint bench on the board (b63d78b):

- A window's frame takes 6 to 8 ms, and the PC's profile says most of a
  frame is the title bar's fade.
- Text with its background costs 156 ns a pixel; a straight loop, sketched
  in the bench and timed on the board, 34. A plain fill is 26.
- Chess's inside takes 28.8 ms, and a moved piece 16 ms.

## The three fixes

### 1. The title bar's fade (`Canvas::fill_fade`, left to right)

Before: for every column, the colour (three divisions), then four dithers,
each of them three searches through the board's levels; then the column
written from top to bottom. A title bar is 18 lines and goes through the
12-line band, so all of it ran twice for each paint, and again for every
repaint of the window, though the fade only changes with the theme or the
window's width.

Now: the dither repeats every four lines, so a whole fade is four rows of
pixels. `fade_rows` works them out for the 640 columns of the screen and
keeps them (2.6 KB, one fade: the last asked for); a column whose colour is
the colour four columns back copies its four pixels from there. Painting is
then one `memcpy` a row. The second band of a title bar, and every later
paint of the same bar, finds the rows ready.

Options not taken: rows kept in each window (memory per window, and a
window object that must know about the drawing); a table of several fades
(a to-the-front changes two title bars, so one kept fade is recomputed
twice then, about 640 cheap columns each; to add if the board says it
matters). The top-to-bottom fade of the flat themes was left alone: it
already works a colour out once a row.

### 2. Text with its background (`Canvas::text_cells`)

Before: for every pixel, a division to find its character, the string
walked again from where the last pixel left it, the glyph's row looked up
again, a call for the background colour and a comparison with the caret's
column.

Now: for every character and row, the glyph's row is read once and its six
pixels written one after the other. The first character of a clipped part
is found once for all twelve rows. Only a character cut by the edge of the
clip, or the one with the caret in it, goes pixel by pixel as before. The
dithered background of a selection still asks for each pixel's colour; the
call is inlined.

This is `text_opaque`: the Terminal's grid, the Editor's lines, text
fields and lists.

### 3. Chess

What was explained to the owner was "each square composed in a scratch of
its own and copied once, without the band". What was built gets the same
two things by a shorter road, because Chess is a program that draws
through the OS calls and has no canvas of its own to compose in, and
because the landing piece of item 64 and the piece in the hand reach over
the edges of a square:

- **The pieces are coloured once.** They are kept two bits a pixel and were
  unpacked, 1,600 pixels, each time a band touched one: about four times a
  repaint for each of 32 pieces, two thirds of the window's time on the PC.
  Now the twelve of the set in use are unpacked when the window opens and
  when another set is picked, into 19,200 bytes asked from the system and
  given back when the window closes (`colour_pieces`, `draw_big`). Without
  that memory the old way is used.
- **The band is only as wide as what is asked for** (`Canvas::layered`,
  `asked`). It used to span the whole picture whatever the clip, so one
  square of Chess was painted as a strip across the board, eleven squares'
  worth of writes. Now each band is cut to the rectangle around the clip's
  parts in it. This is for every picture painted through the band, not
  only Chess: title bars, the File manager's list, small windows.

Chess still goes through the band, so a pixel of the board is written to
the band's memory (internal RAM, cheap) and then once to the screen.

## What was built and how it was tested

- `core/src/gfx/canvas.cpp`, `core/include/core/gfx/canvas.hpp`:
  `fill_fade` and `fade_rows`, `text_cells`, `layered` and `asked`.
- `core/src/shell/chess.cpp`: `pieces_`, `colour_pieces`, `draw_big`.
- `host/tests/test_canvas.cpp`, new: the fade, the text and the band each
  against a pixel-by-pixel version written in the test, over 300, 600 and
  200 random places, clips, colours, carets and dithered backgrounds; every
  pixel outside the clip must stay as it was.
- The gate passes, 53 of 53 tests. The reference pictures (`render_scenes`,
  the Chess window's, Cactus Patch's, the network's) are the same to the
  pixel, and `test_paint` still counts no pixel written twice.
  The tests that count the memory a window holds after it closes pass with
  Chess's 19,200 bytes in them.
- The firmware and the programs build in the cloud: `chess.app` is 51,904
  bytes as before (its sections are padded), 0 names missing.

The PC profile, the same scratch program as before, one window at a time:

| Window | Before | After |
| --- | --- | --- |
| Editor, 200 lines of text | 0.26 ms | 0.09 ms |
| Terminal, full of text | 0.47 ms | 0.19 ms |
| Chess, a game begun | 0.51 ms | 0.15 ms |
| Chess, 104 x 120 of the board | 0.094 ms | 0.019 ms |
| Chess, one square: pixels written | 11 times the square | 2 times |
| File manager | 0.095 ms | 0.065 ms |
| Culebra | 0.29 ms | 0.18 ms |

The fade went from 30 µs a window to under 1; the text from 2 ns a pixel
to 0.5; Chess's time outside the drawing calls from 335 µs to 38.

## What is left

- The board turn: the paint bench and the frames benchmark again, for the
  real "after". The estimates given to the owner were a frame 7 ms to about
  1.5, the Terminal's window 33 to about 8, Chess's 36 to about 9 and a
  moved piece 16 to under 1.
- Then, with those figures, whether B (kept pictures) is still wanted for
  what stays over a frame: going back from maximised and a drop that
  uncovers several windows.

## For the video

The story in one line: the desktop missed frames, and the cure was not a
faster chip or more memory but three places where the code did the same
work over and over.

Beats, in order:

1. **The question.** "Does it run at 60?" The benchmark answered no: 64
   to 66 pieces of work over a frame in two minutes, and the monitor never
   lost a line. The picture was steady; it was late.
2. **The guess that was wrong.** The obvious fix is the one every modern
   desktop uses: keep a picture of each window and copy it. It costs up to
   1 MB of the board's 8. The owner said: measure first.
3. **The measurement.** A bench that paints the same 144,000 pixels eleven
   ways on the real screen. Show the output of `dbg.py paintbench` while
   the board flashes stripes, text and a fade for a second. The numbers
   worth a caption:
   - a plain fill: 3.8 ms;
   - the title bar's fade: 216 ms for the same pixels, 58 times slower;
   - text: 22.5 ms, and 4.9 ms when the loop is written straight.
4. **The three culprits.**
   - The pretty blue fade of a Windows 98 title bar: 18 lines of it cost
     as much as all the text of the Editor under it. Five windows, 35 ms
     of fade.
   - Text: for each of the 72 pixels of a letter the code asked "which
     letter am I in?" with a division.
   - Chess: to move one piece it repainted the board in strips, 38 times
     over, unpacking every piece four times.
5. **The fix.** No new memory to speak of: 2.6 KB for the fade, 19 KB for
   the chess pieces. Not one pixel on the screen changes; the tests compare
   the old drawing and the new one pixel by pixel.
6. **Before and after on the board.** To film: `tools/bench/desktop_frames.py`
   on `main` before the merge and after it, side by side ("to front +
   chess" and "maximise" are the parts to watch), and Chess with a piece
   dragged across the board.

Figures for the captions (board, before; the "after" column is filled in
by the board turn):

| | Before | After |
| --- | --- | --- |
| A window's frame | 6 to 8 ms | |
| The Terminal's inside | 25.4 ms | |
| Chess's inside | 28.8 ms | |
| A chess piece moved | 16.4 ms | |
| `text_opaque`, 144,000 pixels | 22.5 ms | |
| `fill_fade`, 144,000 pixels | 216.5 ms | |
| Five windows painted again | 69.9 ms | |
| Pieces over a frame in the benchmark | 40 to 41 | |

A line that may be worth saying: one frame at 60 Hz is 16.8 ms, and this
board writes a pixel to its screen memory in 26 ns at best, so a full
screen is 8 ms of pure writing. Half the frame is gone before any drawing
is clever or stupid; there was never room to be wasteful.
