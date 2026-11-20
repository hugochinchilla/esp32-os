# A picture on the desktop

Backlog item 87. Decision: `docs/decisions/desktop-picture.md`.

## What was asked

The owner, 2026-11-20:

> inicia la feature para permitir que un wallpaper pueda ser una
> fotografía, añadiendo un explorador de ficheros para seleccionarla. Si la
> foto configurada fuera eliminada o movida, se hace fallback al color azul
> metalico que usamos por defecto

The same morning he had asked for NASA photographs in `C:\pictures` at
800x600 and at 640x480 "to be desktop backgrounds later" (item 85, another
thread), and the GIF decoder had just been merged (item 84), so the system
reads four kinds of picture.

## What was found

- The desktop's background was a colour and an 8x8 pattern
  (`wm::Wm::Backdrop`, item 31), set by Display through
  `os::set_backdrop` and painted by `Wm::fill_backdrop`: by the desktop
  panel (`core/src/desk/icons.cpp`) around its icons, and by the window
  server itself where there is no panel.
- The fades (Shut Down's, item 60; the start-up one, item 74) work from an
  8x8 tile of that plain desktop: "every pixel goes towards what the plain
  desktop has there".
- The screen is one picture with no second buffer, so whatever is behind
  the windows is painted again each time a window moves off it. A JPEG
  cannot be decoded for that: it has to be in memory, decoded.
- An Open dialog already exists (`open_file_dialog`, `dialogs.cpp`), used
  by the Editor, the Picture viewer and Vizcacha, and a dialog can open it
  over itself the way Display opens the Colour dialog.
- `image::Converter` makes pictures smaller, never larger.
- `fs::Storage` tells which folder changed (item 66), and the shell
  already turns that into work on the UI task (`settle()` in
  `desktop.cpp`).
- A window could hold 24 controls; Display had 19.

## The options, and what was chosen

**Where the picture stands when it is not the screen's size.** Put to the
owner on a card: (a) centred, made smaller if it does not fit, never
larger, the colour around it (recommended: the converter only shrinks and
the NASA photographs come in both screen sizes); (b) a Position list,
Centre, Tile and Stretch, as on the desktops of the 1990s; (c) always fill
the screen, cropping. He chose (b). So Stretch needed enlarging code.

The rest was chosen while building and is his to overrule (the decision
file lists each):

- **Keep the decoded picture in memory**, one byte a pixel: 300 KB at
  640x480, 469 KB at 800x600. The other ways (decode at each repaint; keep
  the JPEG's bytes) cost seconds each time a window moves.
- **Decode in a background job**, low priority, as the desktop's listing
  is read. On a machine with no tasks (the web page, most tests) the job
  is done inside the call, as every job is there.
- **The colour whenever the picture cannot be shown**, with the name left
  in the settings: the file gone, moved, renamed, not a picture, or no
  memory. If the file comes back so does the picture. The owner's words
  name "the metallic blue we use by default": read here as the desktop's
  colour, which is Steel blue unless he picked another in Display.
- **"Mir 800x600.jpg" stands for "Mir 640x480.jpg"** on the other screen,
  if that file is beside it.
- **The start-up fade waits for the picture**, up to 3 s, on the plain
  colour. Not waiting would show the desktop in its colour and then, a
  second or so later on the board, the photograph popping in.

## What was built

- `gfx::Canvas::copy`: whole rows of a bitmap through the clip list, for
  a large picture (`blit` tests each pixel for transparency).
- `wm::Wm::set_wallpaper(pixels, width, height, tiled)` and
  `paint_backdrop`, which is `fill_backdrop` plus the picture: centred
  with the colour in the up to four rectangles around it, or tiled from
  the corner, each pixel written once. The desktop panel and the window
  server call it where they called `fill_backdrop`, which stays as "the
  plain desktop" for the fades. `set_screen_listener` tells the shell of a
  new mode before anything is painted in it, and `hold_arrival` keeps the
  start-up fade on its rest.
- `core/src/shell/wallpaper.cpp`, the system's half: the job, the fitting,
  the sister file, the look at the folder's change notices, and
  `Enlarger`, a row sink between the decoder and the converter that makes
  rows wider and adds rows between them (bilinear, integers only, three
  rows of working memory). A side that must grow grows there; the
  converter shrinks the other, so a wide low picture stretched to the
  screen goes through both.
- `os::set_wallpaper(path, how)` with `os::Wallpaper::{Centre, Tile,
  Stretch}`.
- Display: a Picture row (the name, Browse..., None) and a Position row
  of three radio buttons under the patterns, whose cells lost 10 lines of
  height to make room: the dialog is 446 lines tall and 640x480 leaves
  452. The small screen shows the picture as the mode picked in
  Resolution would, decoded to that size by a job of the dialog's. A file
  that cannot be shown is refused with the decoder's own sentence.
  `[background] picture` and `position` in `desktop.ini`.
- The limit of controls in a window went from 24 to 32, and radio buttons
  tell the program when one is chosen (they told nobody; the dialog has to
  draw its small screen again).

## How it was tested

On the PC only. 59 of 59 tests pass.

- `test_shell`, `test_wallpaper`: with tasks and a queue as on the board.
  The sister file for each mode; nothing read when the same picture is
  named again or when another file of its folder changes; the picture
  made again for 800x600 and back; Centre of a picture too wide (640x240,
  the colour above and below); a small one centred, tiled (the frames of
  the copies where they should be) and stretched (its black frame on the
  screen's edges); deleted, written again, moved to another folder, a
  text file named as the picture; None asked for while the job reads.
  After each, the Memory port holds one block more than before, or none.
- `render_scenes`, nine new pictures (`38g` to `38o`): Browse..., the
  picked photograph on the small screen, Apply and Escape, OK and the
  settings file, the desktop centred, stretched and tiled, the colour
  again after a rename, a damaged JPEG refused.
- The reference pictures of Display changed (the new rows), and those
  that show a figure of memory by a few pixels.

Not tested: anything on the board. How long a 640x480 or 800x600 JPEG
takes to decode there, what a repaint over a photograph costs against the
frame budget, whether 469 KB more in use leaves Doom its memory, and what
a remote screen makes of a dithered desktop are all for the board turn.

## What was learned

- The fades needed no change: they already treated "the plain desktop" as
  a tile, and a picture is just more pixels that differ from it.
- The first version made the picture for 640x480 at start-up and again
  for 800x600 a moment later, when the saved mode was applied. The mode is
  now set before the picture is asked for, and the service asks for "the
  mode wanted", not the one the screen is in.
- Icon labels are white with a black shadow and stay readable on a
  photograph; on a busy tiled picture they are as hard to read as they
  were in 1995.

## What is left

- The board turn (request in the project files).
- Not built: a preview list of the pictures in `C:\pictures` in Display;
  a filter in the Open dialog that shows only pictures; "Set as desktop
  background" in the Picture viewer and the File manager's menu; giving
  the memory back while a game has the whole screen.
