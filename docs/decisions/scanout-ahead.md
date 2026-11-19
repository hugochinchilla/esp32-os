---
title: "Chosen for the refills missed at 800x600"
date: 2026-11-19
by: agent
status: to-confirm
area: video
summary: "800x600 copies the frame's third piece to internal RAM in the vertical blank (8,000 bytes); 640x480 is left as it was (backlog 68)"
---

# Chosen for the refills missed at 800x600

The story and the figures are in
`docs/journal/2026-11-19-refills-800x600.md`. On the board (batch 21,
2026-11-19): the frame benchmark at 800x600 missed nothing in three runs,
against 270 before. One burst of 3 missed refills in the middle of a frame
was seen once by hand (Culebra started with the Editor maximised); the
owner chose to merge with that known and left ("Fusionar tal cual").

Chosen by the agent, to confirm:

- **In 800x600 the third piece of each frame (lines 20 to 29) is copied to
  internal RAM during the vertical blank**, in the video interrupt, when the
  second piece is handed over. The first refill inside the frame then copies
  from internal RAM, and the cache has a whole period to fetch the piece
  after it. It costs 8,000 bytes of internal RAM while the mode is on, and
  one copy at the external RAM's own speed per frame, in the blank, where
  nothing waits for it (the interrupt runs 0.1 to 0.25 ms longer once a
  frame).
- **640x480 is left as it was**: it missed nothing in any board turn, and
  the copy would cost it 6,400 bytes of internal RAM and a longer interrupt
  for no measured gain. The switch is one word per mode in `vga.c`.
- **A missed refill is counted as before** (not done when the DMA comes back
  to that buffer). The DMA needs a buffer's first byte then and its last
  byte a period later, so most of the 270 were probably never on the
  screen, and the webcam saw nothing. The count was kept strict: it is the
  early warning, and a looser count would have hidden this.
- **The memory report says more**: the longest fill of each of a frame's
  first eight pieces and the longest copy made ahead. The scanout figures
  start again at a change of mode.

Not chosen:

- A 64 KB data cache (Espressif's advice for a picture in external RAM): it
  takes 32 KB of internal RAM in every mode, and a cache twice the size is
  still emptied by a millisecond of painting.
- 800x600 at 56 Hz (36 MHz): 10% less to copy, a non-whole clock division,
  and the same millisecond of blank before the first refill.
- Bounce buffers of 20 lines: the period doubles and so does the copy.
- Raising the interrupt's level or the external RAM's clock (120 MHz is
  marked experimental in ESP-IDF v5.5).
