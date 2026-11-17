# Window themes: two alternatives drawn, nothing built

## What was asked

The owner, 2026-11-17:

> Estoy pensando que, bueno, tenemos un estilo muy tipo Windows 95, pero no
> hemos explorado otros estilos gráficos de la época. Y me pregunto si sería
> fácil tener una galería de estilos y que el usuario en el menú de display
> pudiera configurar si quiere el estilo Windows 95 o algo tipo un Mac de la
> época o una Amiga o algo así. Pero no quiero que cambie radicalmente el
> funcionamiento del escritorio. [...] Solo a nivel del decorador de ventanas,
> las barras de título y un poco la iconografía [...] ver una o dos
> alternativas me haría ilusión para ver si hay algo más bonito que podemos
> explorar.

Step 1 is pictures to choose from, and a word on how hard a switch would be.
Nothing is to be built before he picks.

## What was found

Where the look lives today:

- The bevels are five functions of `gfx::Canvas` (`core/src/gfx/canvas.cpp`):
  `raised`, `sunken`, `button`, `button_frame` and `checker`. Windows, menus,
  buttons, fields, lists, scroll bars and the taskbar all draw through them.
- The title bar is one function, `Wm::paint_frame` (`core/src/wm/wm.cpp`),
  with its two fades as constants at the top of the file and the three
  buttons placed by `Wm::title_button`, which the hit test uses too.
- The sizes are constants: `Window::kBorder` 4, `kTitleHeight` 18,
  `kClientTop` 24, title buttons 16 by 14.
- The face colour is `gfx::kGrey`, a compile-time constant, named about 200
  times in `core/src` outside the icons: 51 times in `wm/widgets.cpp`, 24 in
  `os/ui.cpp`, 18 in `wm/wm.cpp`, 12 in `desk/taskbar.cpp`, and in the
  programs that fill their own background.
- The selection colour, `{0, 0, 128}`, is a private constant repeated in five
  files (`wm.cpp`, `widgets.cpp`, `text_edit.cpp`, `desk/icons.cpp`,
  `os/ui.cpp`), always with white text over it.
- The small shapes (close, maximise, arrows, tick, radio button) are string
  patterns in `wm.cpp` and `widgets.cpp`.
- The board has only four exact neutral greys (blue has four levels), so a
  look made of black, white and those greys costs no dither.

## The pictures

Drawn by the real drawing code, not by a paint script: a scratch patch
(`tools/mockup/themes-prototype.patch`, 215 lines added over 12 files, not
part of the build) adds a global theme number, and `render_scenes` was run
three times with `THEME=0`, `1`, `2`. Theme 0 came out byte for byte equal to
the reference pictures. The pictures are in `docs/look/themes/`: for each
theme the widget gallery at twice the size, and a sheet of four scenes (Start
menu, File manager, Save As, Display).

- **A, the current look.** Grey faces, two-pixel bevels, a blue fade in the
  title bar.
- **B, one bit**, after the black and white desktops of the mid 1980s. White
  faces, one-pixel black outlines, a second black line below and right of
  each window as its shadow, thin black stripes across the active title bar
  with the title centred in a gap, no stripes and no boxes on an inactive
  window, buttons as boxes with cut corners (a thick outline for the default
  and the pressed one), a black and white weave in the scroll tracks, black
  as the selection colour.
- **C, workbench**, after the grey, black, white and blue workstation
  desktops of around 1990. Grey faces, one-pixel white and black bevels, the
  whole frame of the active window in a steel blue (board colour 3, 4, 2,
  exact), black title text at the left, title-bar buttons as full-height
  boxes with their own shapes, fields with a ridge (up, then down), the
  scroll thumb and the pressed buttons in the same blue, blue selection with
  black text.

All three keep the same sizes and the same places for everything: only what
is painted changes. The shapes and colours are our own; nothing was copied
from Apple's or Commodore's artwork, fonts or logos.

What the pictures do not show well, because the prototype is rough: in B the
disabled text, the tray box and the menu separators are not designed yet;
the small window inside the Display dialog's monitor still shows the current
look in all three; the program and desktop icons are the same in all three.

## How hard a switch would be

Small to medium, and most of it is tidying that the code wants anyway:

1. A theme table: about ten colours (face, light, shadow, dark, selection
   and its text, title bar active and inactive and their text), a bevel
   style, a title-bar painter and a set of small shapes.
2. `gfx::kGrey` stops being a constant and becomes "the theme's face
   colour". In the prototype, making it a variable broke the build in one
   place besides the icons, so the 200 uses need no edit each; the icons
   keep a fixed grey.
3. The five bevel functions and `Wm::paint_frame` read the table. That is
   where the prototype's 215 lines went.
4. The five copies of the selection colour become one.
5. The Display dialog gets a list, the choice goes in its settings file, and
   a change repaints the whole screen.
6. Picture tests: the 107 references stay for the current look; each other
   theme gets a handful of its own.

What costs more: a theme that changes sizes or moves the title buttons (the
left side, say). The constants in `window.hpp` and `Wm::title_button` would
have to come from the table, and every reference picture of that theme
would differ in layout too. Icon themes are a separate piece of work: the
icons are drawn art, not code.

Not themed in any case: what a program paints for itself (the Terminal, the
games, the picture viewer) and the font (Spleen is the only one).

On the board: reading a colour from a table instead of a constant is one
memory read per call, and the paint paths stay "each pixel once". Not
measured, and nothing here has run on the board or been compiled for it.

## What was chosen

Nothing yet. The owner is to pick a direction from the pictures (B, C, both,
or neither), and to say whether themes may move the title buttons and
whether icons are part of a theme.

## What is left

Backlog item 47.
