---
title: "NASA photographs that fill the screen"
date: 2026-11-20
by: owner+agent
status: to-confirm
area: programs
summary: "Seven NASA photographs in C:\\pictures at 800x600 and 640x480, size in the name, to be desktop backgrounds later (backlog 85)"
---

# NASA photographs that fill the screen

Backlog item 85. Journal: `docs/journal/2026-11-20-nasa-backgrounds.md`.
Sources: `third_party/samples/README.md`.

- **Which photographs** (the owner's pick from fourteen candidates,
  2026-11-20: "1, 4, 6, 8, 10, 11, 12"): three of Mir seen from the
  Shuttle, which he asked for by name, and four of the Earth. Not picked:
  four more of Mir, the Blue Marble of 2012, two sunrises on the horizon.
- **Both sizes in C:\pictures itself, the size in the name** (the owner's
  choice on a card, 2026-11-20): `mir over clouds 800x600.jpg`. Not
  chosen: a folder for each size (the agent's recommendation), a folder
  `space` for all of them.
- **`landscape.jpg` is no longer a sample** (the owner, 2026-11-20, once
  the photographs were on the board: "elimina la foto del paisaje, no me
  gusta nada"). It was the drawn evening landscape of item 21, 320x240.
  The generator still draws it for the decoder's tests and the bench
  files; the tests that opened it from the drive open `earthrise.jpg`.

The rest is the agent's, to confirm:

- **Baseline JPEG at quality 85**, made by the project's own writer like
  the other samples: 14 files, 891 KB of the drive's 9 MB.
- **English names that say what is seen**, not NASA's numbers; the numbers
  are in the list of sources.
- **The board gets them from `tools/flash.py`**, like every sample since
  item 77; the PC program and the web page carry them
  (`fs::add_backgrounds`); the tests' drives of 1 or 2 MB do not.
- **A GIF of our own comes with them**, `mascot blinks.gif`: the mascot of
  `mascot.bmp` that shuts its eyes every three seconds, so that a new
  drive has an animation to show.
- **Nothing uses them as a background yet.** That is a later item.
