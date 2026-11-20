---
title: "Pictures in Vizcacha"
date: 2026-11-18
by: owner+agent
status: decided
area: programs
summary: "Vizcacha shows a page's PNG, JPEG, BMP and GIF pictures, fetched after the text and fitted to the page (backlog 53, 84)"
---

# Pictures in Vizcacha

Backlog item 53. Journal: `docs/journal/2026-11-18-browser-pictures.md`.
The owner reported that the browser showed no pictures, was given four
options (in pages; only when opened; in pages with GIF; none) and chose
pictures inside pages (2026-11-18). The details below are the agent's.

- Pictures are drawn inside the page, with the decoders the Picture viewer
  already has: PNG, JPEG, BMP. A GIF kept its box with the `alt` text
  until 2026-11-20, when the owner asked for a GIF decoder (backlog 84,
  [GIF pictures](gif-pictures.md)): since then a GIF is drawn like the
  others, its transparent parts showing the page's colour.
- The text comes first. The pictures are fetched after it, one at a time,
  from the same timer, and each appears when it is whole. Not chosen:
  several at once (connections are four in the machine) and drawing a
  picture as it decodes (the page would be painted many times over).
- A picture takes the size the page gives it, is never made larger than it
  is, and never wider than the page. It is stored as board colours, a byte
  a pixel, with Floyd-Steinberg, the Picture viewer's default.
- Limits: the first 24 pictures of a page, 1.5 MB of pixels in all, and a
  picture's file at most 256 KB, as a page's. What does not fit keeps its
  box and the status line counts it.
- A link to a picture opens it alone in the window, from the bytes that
  were fetched to find out it was a picture.
- Not kept between pages: Back fetches the pictures again. A cache is more
  memory held and more to decide.
