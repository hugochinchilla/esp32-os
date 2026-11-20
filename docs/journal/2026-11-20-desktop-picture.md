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

On the PC, 59 of 59 tests pass. The firmware compiles and links in the
cloud (ESP-IDF v5.5; 12 programs checked, 0 names missing); it has not run.

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

## On the board

The board turn of 2026-11-20 (commit f5669c7; a batch with items 85 and
86). Passed what was asked; not merged, the owner has not seen it yet.

- The firmware is 1,659,504 bytes (7,408 more than `main`).
- Display fits above the taskbar at 640x480. A 640x480 JPEG picked with
  Browse... shows on the small screen; after Apply it is on the desktop
  in 1.5 to 2 s, with the desktop usable meanwhile.
- With that picture up: external RAM 311,280 bytes lower, internal the
  same, no "wallpaper" task left. A full repaint takes 15.9 to 16.4 ms
  (14.5 to 15.1 ms without a picture on the same boot).
  `desktop_frames.py`: 1,909 frames, 35 over the budget, the worst
  37.6 ms; no refill missed. Not compared with `main` in the same turn.
- A normal restart shows the desktop with its picture already in it; the
  plain colour was shorter than the camera could see. `reboot --fast`
  left the monitor black for about 4 s and then showed the desktop with
  the picture.
- 800x600: the file named for that screen is shown, centred. Stretched
  to 800x600: external RAM 483,308 bytes lower, repaint 26.1 to 27.4 ms,
  nothing over that mode's budget. Doom starts and runs at 35 pictures a
  second with it up, with 566,144 bytes of external RAM left free: a
  narrow margin. The desktop and the memory come back whole after.
- Tile and Centre as drawn on the PC.
- The file removed (`dbg.py rm`): the plain colour within 3 s, no
  message, the memory back. Put again: the picture back in about 6 s. A
  restart without the file: the plain desktop, no message.
- A text file picked: refused with the decoder's sentence.
- Display opened and closed ten times: no drift. No panic, no watchdog
  line, no missed refill.
- Not done in the turn: watching a window dragged over the picture by
  eye; the mode was changed with `dbg.py mode`, not through Resolution
  and Keep.

## Brought up to `main`

The owner said yes to merging, 2026-11-20. `main` had taken the NASA
photographs (item 85) and Pincel (item 86) meanwhile: the scenes now use
`earthrise.jpg`, which took `landscape.jpg`'s place among the samples
(renamed for the test and back, not copied), the reference pictures were
made again, and the PC tests are 61 of 61.

## What was learned

- The fades needed no change: they already treated "the plain desktop" as
  a tile, and a picture is just more pixels that differ from it.
- The first version made the picture for 640x480 at start-up and again
  for 800x600 a moment later, when the saved mode was applied. The mode is
  now set before the picture is asked for, and the service asks for "the
  mode wanted", not the one the screen is in.
- The board's compiler turned down what the PC's let pass: `snprintf` with
  `"%s"` from one 160-byte path into another "may be truncated". The
  copies are a small `keep_text` now.
- Icon labels are white with a black shadow and stay readable on a
  photograph; on a busy tiled picture they are as hard to read as they
  were in 1995.

## What is left

- The board turn (request in the project files).
- Not built: a preview list of the pictures in `C:\pictures` in Display;
  a filter in the Open dialog that shows only pictures; "Set as desktop
  background" in the Picture viewer and the File manager's menu; giving
  the memory back while a game has the whole screen.
