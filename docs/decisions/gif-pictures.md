---
title: "GIF pictures"
date: 2026-11-20
by: owner+agent
status: to-confirm
area: programs
summary: "One GIF decoder for the Picture viewer and Vizcacha; animations play in both, decoded again each time round, within memory limits (backlog 84)"
---

# GIF pictures

Backlog item 84. Journal: `docs/journal/2026-11-20-gif-pictures.md`.

- **One decoder for both programs** (the owner's request, 2026-11-20:
  "tanto el file viewer como el navegador soporten mostrar GIFs"). It is a
  fourth format behind `image::Decoder`, in `core/src/image/gif.cpp`,
  written for the project like the other three and for the same reasons
  ([image viewer milestone](image-viewer-milestone.md)). Every machine
  (board, Linux, web page) runs the same code.
- **Animations play, in the viewer and in the pages** (the owner's choice
  on a card, 2026-11-20: "Animados desde ya en los dos"). Not chosen: the
  first picture now and animation as an item of its own (the agent's
  recommendation); never animated.

The rest is the agent's, to confirm:

- **The picture has the size of the file's "screen"**, and each picture of
  the file lies on it where the file says. A screen too small for the
  first picture, or 0 as some old files have it, grows to hold it.
- **Transparent pixels, and the screen no picture covers, show the
  background handed to the decoder**: the viewer's dark grey, the page's
  colour in Vizcacha, as for PNG. The file's own background colour is
  ignored, as web browsers ignore it.
- **An animation plays as the file says.** Each picture stays the time the
  file gives (under 20 ms counts as 100 ms, as in web browsers). It goes
  round as often as Netscape's extension says: for ever for 0, N more times
  for N, once if the file does not say. After the last time the last
  picture stays. A picture that asks to be taken away afterwards ("restore
  to background") leaves its patch see-through. "Restore to previous", which
  few files use, is treated as "leave it".
- **Nothing of an animation is kept but the file and one screen.** The
  decoder keeps the screen as RGB, 3 bytes a pixel, in its working memory,
  lays each picture on it, and gives the whole screen out again; the
  program turns that into board colours each time. So an animation costs
  processor time every picture, for as long as it plays, and memory that
  does not grow with its length. Not chosen: keeping every picture once
  converted (memory by the number of pictures, and a second set of limits).
  If the board cannot keep up, the animation runs slower than the file
  says; a picture is painted when it is whole, never half made.
- **Only the part a picture changes is converted and painted again**,
  since the first board turn (2026-11-20), where converting the whole
  screen for every picture gave 5 pictures a second in a page that asked
  for 47. While a page has animations its timer turns every 10 ms.
- **Memory limits.** The decoder's usual 384 KB plays a screen of up to
  about 124,000 pixels (400x300). The Picture viewer asks for more for a
  larger one, up to 800x600 (1.46 MB), while that picture is open. In a
  page each animation has a decoder of its own, with working memory sized
  to its screen, and a copy of its file: at most 6 animations and 768 KB
  between them (`kMaxAnimations`, `kMaxAnimationBytes` in `browser.cpp`).
  What does not fit shows its first picture, still.
- **A picture that moves is dithered with a pattern**, Bayer 4x4, in both
  programs, unless the viewer's chosen dither is already one of the
  patterns (`image::steady`). Error diffusion, the default for stills,
  carries the errors of the part that changes through the parts that do
  not, which would shimmer.
- **A page's animation that cannot be seen waits**: scrolled out of the
  window, or the window minimised.
- **An interlaced still GIF is decoded whole before its first row is
  given**, into the working memory, so it can be at most about 370,000
  pixels (640x480 fits, 800x600 says "too large"). Not chosen: four
  decoders running at once over the four passes.
- **Damaged files**: a still picture, or the first of an animation, whose
  data ends early or holds a code that cannot be gives "damaged" at the row
  where it happens. Damage further on in an animation ends it quietly: the
  last good picture stays. A file with no picture in it is "damaged".
