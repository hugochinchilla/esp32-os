---
title: "GIF pictures"
date: 2026-11-20
by: owner+agent
status: to-confirm
area: programs
summary: "One GIF decoder for the Picture viewer and Vizcacha: the first picture, transparency, interlacing; animation asked on a card (backlog 84)"
---

# GIF pictures

Backlog item 84. Journal: `docs/journal/2026-11-20-gif-pictures.md`.

- **One decoder for both programs** (the owner's request, 2026-11-20:
  "tanto el file viewer como el navegador soporten mostrar GIFs"). It is a
  fourth format behind `image::Decoder`, in `core/src/image/gif.cpp`,
  written for the project like the other three and for the same reasons
  ([image viewer milestone](image-viewer-milestone.md)). The Picture viewer
  and Vizcacha did not change: they ask the decoder for rows and it tells
  the kind of file by its first bytes. Every machine (board, Linux, web
  page) runs the same code.

The rest is the agent's, to confirm:

- **What is shown is the first picture of the file.** Whether animations
  move was put to the owner on a card on 2026-11-20 (first picture now and
  animation as an item of its own, recommended; animated at once in both
  programs; never animated) and is not answered yet. Until it is, an
  animation shows its first picture, still.
- **The picture has the size of the file's "screen"**, and the first
  picture lies on it where the file says. A screen too small for the
  picture, or 0 as some old files have it, grows to hold it.
- **Transparent pixels, and the screen the picture does not cover, show
  the background handed to the decoder**: the viewer's dark grey, the
  page's colour in Vizcacha, as for PNG. The file's own background colour
  is ignored, as web browsers ignore it.
- **An interlaced GIF is decoded whole before its first row is given**,
  into the decoder's 384 KB pool, so it can be at most about 370,000 pixels
  (640x480 fits, 800x600 does not and says "too large"). A GIF that is not
  interlaced has no such limit below the decoder's own (4096 wide, 16384
  high). Not chosen: four decoders running at once over the four passes,
  which needs no picture in memory but reads the file four times over and
  is much more code; a block of its own from the Memory port, which is one
  more thing to give back.
- **Damaged files**: data that ends early or holds a code that cannot be
  gives "damaged" at the row where it happens, and the rows before it are
  shown (an interlaced picture shows none). A file with no picture in it is
  "damaged" too.
