---
title: "Slow repaints: faster drawing first"
date: 2026-11-18
by: owner
status: decided
area: desktop
summary: "Large windows painted slowly (backlog 65): fix the drawing code first, measure, kept window pictures only if still needed"
---

- The owner chose to measure before fixing, and then, of three options, the
  faster drawing: the title bar's fade worked out once, text with its
  background a character at a time, Chess's pieces coloured once, and the
  scratch band no wider than what is asked for. About 22 KB of memory.
- Not chosen now: a kept picture of every window in external RAM (0.7 to
  1 MB, the larger change, and it does not make drawing faster). It stays
  on the table for what is still over a frame after the next board
  measurement: going back from maximised, a drop that uncovers several
  windows. "Windows keep no stored picture of themselves" stands.
- The scratch band of "Painting each pixel once" stays; a band is now cut
  to the rectangle around the parts of the clip in it.
- Journal: `docs/journal/2026-11-18-slow-repaints.md` (the measurement and
  the options), `docs/journal/2026-11-18-faster-drawing.md` (the fixes, and
  the note for the video).
