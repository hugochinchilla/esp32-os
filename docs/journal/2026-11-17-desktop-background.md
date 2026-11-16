# The desktop's colour and pattern

2026-11-17, just after midnight. PC only; this has not run on the board.

## What was asked

The owner, while the icons were going in:

> Extenderemos el alcance de esta PR. También me gustaría poder seleccionar
> el color de fondo del escritorio entre colores sólidos y diferentes
> patrones de textura aplicados a los colores sólidos. No hace falta
> muchísimos patrones. No sé, un selector de colores sólidos y a lo mejor 10
> patrones, entre ellos la opción de no patrón. Para acceder a este menú se
> llegaría por el panel de control o haciendo clic derecho sobre el área
> vacía del escritorio y se abriría un diálogo con opciones en las cuales
> habría la configuración de la pantalla.

## What was found

- The desktop was one constant in `core/src/wm/wm.cpp`: `kDesktop = {58,
  110, 165}`, painted with `fill_rgb`, which dithers a 24-bit colour into the
  board's 256 with a 4x4 pattern fixed to the screen.
- A right click on the desktop did nothing: `Wm::on_right_press` returned
  when no window was under the pointer.
- There is no Control Panel: Start > Settings is a menu, with "Control
  Panel" greyed out. The Screen Saver dialog's own comment says it is "to
  become a tab of a Display window once there is a Control Panel".

## Options considered

**Which colours.**
1. A fixed list of named colours. Small, readable in the settings file, and
   each can be checked by eye on the board's 256 colours.
2. Three sliders for red, green and blue. Any colour, but most positions
   dither into something muddy at 3-3-2 bits, and it needs a slider widget
   that does not exist.
3. All 256 board colours in a grid. No dithering, but most of them are
   garish and a 16x16 grid of 12-pixel cells is hard to hit.

**What the pattern is drawn with.**
1. Black, as the systems of the time did. Invisible on dark colours and
   harsh on light ones.
2. A darker shade of the same colour (lighter when the colour is very
   dark). Every pattern works on every colour and reads as a texture, which
   is what was asked for.
3. A second colour chosen by the user. Another picker, 160 combinations
   most of which are ugly.

**How to get there.** The right click could open the dialog at once, or open
a small menu with one entry. A menu is where "New folder", "Arrange icons"
or "Refresh" would go later.

**Where it is kept.** `desktop.ini` already exists and belongs to the
desktop; a new `display.ini` would be a second file for the same thing.

## What was chosen

By the agent, as defaults to be shown to the owner (all easy to change):

- A list of 16 named colours. The first, "Chinchilla blue", is the desktop
  as it always was, so nothing changes until the user asks. Not sliders: no
  widget, and too many bad results. Not the 256: garish.
- 10 patterns of 8x8, the first of them "(None)", as he said: Dots,
  Diagonal, Grid, Bricks, Weave, Diamonds, Waves, Zigzag, Crosses. All
  original.
- The pattern in a darker shade of the colour (five eighths of each
  channel), or 88 lighter per channel when the colour's three channels add
  up to less than 160. Not black, for the reasons above.
- A right click on the desktop away from the icons opens a menu with
  "Display..."; the same entry is in Start > Settings. Not the dialog at
  once, to leave room for more entries.
- A dialog called Display with a preview on a small monitor, the two grids,
  and OK, Cancel, Apply. Apply shows the choice on the real desktop; only
  OK keeps it; Cancel, Escape or the close button put back what was there.
- Kept in `C:\.config\desktop.ini`, `[background]`, by name (`colour =
  teal`, `pattern = bricks`), as the other settings are.

## What was built

- `gfx::Canvas::fill_pattern`: like `fill_rgb`, with an 8x8 tile worked out
  once that holds both the pattern and the dither. One write per pixel, the
  same cost per pixel as the plain fill.
- `wm::Wm::set_backdrop` and `Wm::Backdrop` (colour, ink, eight rows), and
  `Wm::set_desktop_menu` for the right click. The window manager knows
  nothing of the lists of colours and patterns: those are the shell's.
- `core/src/shell/display.cpp`: the dialog, a `Picker` widget (a grid of
  cells, one chosen, arrow keys when it has the focus) and the preview.
  Alt+C and Alt+P go to the two grids, Alt+A applies.
- A Display icon (a screen with a small desktop) at both sizes.
- The patterns in the grid are shown in the colour picked, so the grid is
  the real result.

Tested on the PC: 22 of 22. The picture test opens the menu with a right
click, picks Teal and Bricks, and checks that nothing changes before Apply,
that Apply shows it, that Escape takes it back, that OK keeps it and that
`desktop.ini` holds `sand` and `diamonds`; five new reference pictures
(`37p` to `37t`). One older picture changed by itself, `34e`: the Display
dialog is a process while open, so later PIDs are three higher.

Not tested: the board. Things to look at there: how each of the 16 colours
looks through the resistor DAC (they were chosen on a PC screen), whether a
full-screen patterned repaint is as fast as the plain one, and the right
click with a real mouse. The firmware was not compiled for this change.

## What was learned

A picture of an 8x8 pattern, shrunk by an image viewer, shows a different
pattern or none at all. The first look at the applied desktop seemed to have
lost its bricks; at four times the size they were there. Screenshots of
patterned desktops have to be looked at enlarged by a whole number.

## What is left

- Entries for the desktop's menu (New folder, Refresh).
- The Screen Saver dialog as a second part of Display, as its comment says.
- A picture as wallpaper (not asked for).
