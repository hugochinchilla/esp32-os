---
title: "Pincel, the drawing program"
date: 2026-11-20
by: owner+agent
status: to-confirm
area: programs
summary: "Pincel: a program file with eight tools, a palette of 28 of the board's colours, one Undo; saves 8-bit BMP, opens what the system decodes (backlog 86)"
---

# Pincel, the drawing program

Backlog item 86. Journal: `docs/journal/2026-11-20-pincel.md`.

The owner's, on 2026-11-20:

- **A basic drawing program** ("una app de paint básica, como el paint de
  windows 95"): in the spirit of the paint programs of the mid 1990s, with
  its own design, pictures and name.
- **Its name is Pincel** (his choice on a card). Not chosen: Cochinilla,
  Colibrí, Painter.

The rest is the agent's, to confirm:

- **A program file like the others** (`programs/pincel`, `pincel.app` in
  `C:\Programs`), the same source on the board, in the Linux program and
  in the web page. It asks only through the OS calls; the firmware did not
  change. Many can run at once, each a process with its own sheet, as the
  Editor.
- **The window**: eight tools in two columns at the left, under them a box
  with the tool's options, the sheet with scroll bars when it is larger
  than its area, the two colours in use and the palette along the bottom,
  and a status line (the tool and the sheet's size; where the pointer is
  while the button is held).
- **The tools**: pencil (one dot wide), brush (round), eraser (square,
  leaves the paper colour), line, rectangle, ellipse, fill, and picking a
  colour from the sheet. Four sizes (1, 2, 4, 7 dots; the eraser's are 4,
  6, 10, 16), each tool keeping its own. A rectangle or an ellipse is an
  outline, an outline filled with the paper colour, or solid.
- **Two colours, ink and paper.** A click on the palette is the ink, the
  right button the paper. The palette is 28 of the board's 256 colours; a
  double click on one opens the system's colour dialog to change it, and
  the palette is kept in `C:\.config\pincel.ini`.
- **The sheet is one byte a pixel, the board's colour**, 320 by 240 when
  new, any size up to 800 by 600 with Image > Size (what was drawn stays
  from the top left corner). Image also flips it, inverts its colours and
  clears it.
- **One Undo**, which is a second copy of the sheet: Undo again brings the
  change back. A sheet of 800 by 600 so takes 960 KB of external RAM. Not
  chosen: several levels (a copy each), or keeping only the part a stroke
  changed (more code for a first version).
- **It saves BMP files of 8 bits a pixel whose palette is the board's 256
  colours**, so the file holds exactly what was drawn and the Picture
  viewer and any PC open it. Not chosen: PNG or GIF, which need a
  compressor the system does not have, for files that are 78 KB at the
  first size. It opens every kind of picture the system decodes (BMP, PNG,
  JPEG, GIF's first picture), at its size, or shrunk to fit 800 by 600;
  colours the board does not have are dithered (Floyd-Steinberg). A picture
  opened from another kind of file is saved under a `.bmp` name.
- **Saving is a background job with the window locked**, as the Editor's;
  opening is done at once under the hourglass.
- **Left out of the first version**: zoom, a selection with Cut, Copy and
  Paste, text, and drawing with the right button held (the system gives a
  program the right button's click, not its drag). A double click on a
  `.bmp` file still opens the Picture viewer.
