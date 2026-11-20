---
title: "A picture on the desktop"
date: 2026-11-20
by: owner+agent
status: to-confirm
area: desktop
summary: "Display names a picture file shown over the desktop's colour: Centre, Tile or Stretch; the colour is back whenever the file is gone (backlog 87)"
---

# A picture on the desktop

Backlog item 87. Journal: `docs/journal/2026-11-20-desktop-picture.md`.

The owner's, 2026-11-20:

- **The desktop's background can be a photograph, picked with a file
  browser**: Display > Appearance has a Picture row with Browse..., which
  opens the Open dialog every program uses, and None.
- **If the file is deleted or moved, the desktop falls back to its colour**
  (Steel blue unless another was chosen). Nothing is asked or said.
- **A Position list: Centre, Tile, Stretch** (his choice on a card, over
  the agent's recommendation of centred only). Centre: at its own size in
  the middle, the colour around it. Tile: side by side from the top left
  corner. Stretch: made the size of the screen whatever its shape, larger
  if need be. Not chosen: centred only (no enlarging code needed); always
  filling the screen by cropping.

The rest is the agent's, to confirm:

- **The picture is decoded once and kept** in the screen's colours, one
  byte a pixel, from the Memory port: 300 KB for a whole screen at
  640 x 480, 469 KB at 800 x 600, less for a small or tiled picture.
  Uncovering the desktop is then a copy of rows. Not chosen: decoding the
  file at each repaint (seconds for a JPEG on the board), or keeping the
  file's bytes and a smaller cache.
- **Centre and Tile never enlarge, and make smaller what does not fit**,
  keeping the shape: a 1600 x 1200 photograph centred is the whole screen,
  not its middle.
- **A file named for a screen stands for its sisters.** "Mir 800x600.jpg"
  on a screen of 640 x 480 shows "Mir 640x480.jpg" from the same folder if
  it is there (a space, `-` or `_` before the size). The settings keep the
  name that was picked. This is for the photographs of backlog item 85,
  which come in both sizes.
- **The setting keeps the name when the file is gone**, and the picture
  comes back if the file does. The folder's change notices
  ([change notices](change-notices.md)) say when to look; nothing polls.
  A file that is there but cannot be shown (not a picture, a progressive
  JPEG, too large, no memory) leaves the colour too. Display says why when
  such a file is picked, and does not take it.
- **Read by a background job** of the desktop's, at low priority, so the
  desktop goes on. The old picture stays until the new one is ready; if
  there is no room for both, the old one goes first. At start-up the
  desktop's fade-in waits for the picture on its plain colour, 3 s at
  most ([start-up fade](start-up-fade.md)), so the desktop comes up with
  its picture and not a second after it.
- **The fades go to and come from the plain colour**, as before: Shut Down
  fades the picture into the colour with the rest of the desktop.
- **Floyd-Steinberg dither**, the Picture viewer's default. An animated GIF
  shows its first picture. Clear parts of a PNG or GIF show the colour.
- **The pattern still shows** around a centred picture.
- **A change of screen mode makes the picture again** for the new size.
- **Display's small screen shows the picture** as that screen would,
  decoded small by a job of the dialog's.
- **Settings**: `[background] picture` (the full path, empty for none) and
  `position` (`centre`, `tile`, `stretch`) in `C:\.config\desktop.ini`.
- **One OS call**, `os::set_wallpaper(path, how)`, beside
  `os::set_backdrop` ([settings tools](settings-tools-os-calls.md)); the
  system's half is `core/src/shell/wallpaper.cpp`.
- A window may now hold 32 controls (it was 24): Display has 27. Radio
  buttons tell the program when one is chosen, as check boxes do.
- A photograph is dithered all over, which a remote screen (VNC) packs
  worse than the flat colour: not measured yet.
