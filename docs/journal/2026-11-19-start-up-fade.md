# The desktop fades in at start-up

2026-11-19. Backlog item 74. PC only; the firmware part is two lines of
`firmware/os/main/main.cpp`.

## What was asked

The owner, confirming Shut Down's fade (item 60,
[that entry](2026-11-19-shut-down-confirmed.md)):

> Parece que está todo bien así. Mira, me gusta lo del fundido del apagado.
> ¿Podríamos replicarlo para el encendido también? Podríamos leer el color
> de fondo del escritorio y presentar primero ese color y luego hacer un
> fundido del escritorio cargado, de la misma manera que hacemos el fundido
> a la pantalla de apagado.

## What was found

- At start-up the splash stayed on the screen while Wi-Fi and the desktop
  shell started, and `Wm::start()` then painted the screen whole: a cut
  from the splash to the desktop. On the board that first picture has no
  taskbar and no icons yet (they are init's `taskbar` line, a moment
  later), so the desktop arrived in two pieces.
- Shut Down's fade (`Wm::fade_step`, PR 82) works in place: every pixel on
  the screen moves one level of colour per step towards what the plain
  desktop has there. It needs no second picture because what it fades
  *from* is on the screen and what it fades *to* is an 8 by 8 tile.
- A fade in is the other way round: what it fades to, the loaded desktop,
  is not on the screen, and everything in the system paints straight into
  the screen's one picture (`docs/decisions/decided-video.md`).
- Timers of the window server do not run while init reads its start-up
  file: that is one call on the UI task, before its loop.
- On the board a step of Shut Down's fade takes about 20 ms at 640x480 and
  31 to 38 ms at 800x600 (batch 21).

## The options

Where the desktop's picture comes from during the fade:

1. **Paint it once into a second picture kept for the fade**, and work
   each step out from that. 307 KB (480 KB at 800x600) for 420 ms, at a
   moment when no program runs yet. Nothing on the screen is ever wrong.
2. Paint the desktop on the screen seven times and pass a filter over what
   was painted. No memory, but between the painting and the filter the
   screen shows icons at full colour: flicker, on a system built so that
   each pixel is painted once.
3. A filter inside the drawing calls. No memory and no flicker; every
   primitive of `core/gfx` would get a second path.

How the colour arrives:

- A. **A cut from the splash to the colour**, as the owner's words say
  ("presentar primero ese color").
- B. The splash fades into the colour first (the code Shut Down has,
  pointed at the splash). It mirrors Shut Down, which fades twice, and
  costs 420 ms more per start.

## What was chosen

Option 1, by the agent: it is the only one that neither flickers nor
touches the drawing code, and the memory is free at that moment. A, by the
agent as the reading of the owner's words, with B built too and put to him
with a preview of each; switching is one word in the two `main.cpp`. The
rest (300 ms of rest, seven steps of 60 ms, no input meanwhile, nothing on
a fast start, the chime where it was) is Shut Down's, backwards;
`docs/decisions/start-up-fade.md` lists each as `to-confirm`.

## What was built

- `core/wm`: `Wm::start(Arrival)` with `AtOnce` (the default, as before),
  `FromColour` and `FromPicture`. For the last two the screen is held as
  for Shut Down, and a timer of 60 ms walks through the stages: the
  picture fading to the plain desktop (`FromPicture` only), the rest, the
  desktop painted into the kept picture, seven steps up, and the screen
  given back with one whole repaint. `Wm::arriving()` says it is under
  way. The painting code asks `paint_pixels()` where to paint, which is
  the kept picture for that one painting and the screen otherwise.
  `nearer()` got a form that moves several levels at once.
- `host/desktop/main.cpp` and `firmware/os/main/main.cpp` ask for
  `FromColour` unless the start is a fast one.
- `host/tests/test_wm.cpp`, `test_arrival`: the colour shows at once and
  for five steps; each of the seven pictures on the way up is, byte for
  byte, the picture Shut Down's fade shows on the way down; the end is the
  desktop as `start()` paints it; clicks do nothing meanwhile and work
  after; the kept picture is given back (also when the machine goes away
  mid-fade); with no memory the desktop shows at once; from a picture, the
  picture fades first.
- Previews for the owner, made from the test pictures with the same
  arithmetic: `look/startup-fade/` in the project files
  (`a-colour-then-fade.gif`, `b-splash-fades-too.gif`, `today-cut.gif`,
  `fade_preview.py`).

Tested: the PC tests (`tools/gate.sh`). The firmware had not been compiled
when this was written (an addition below says if it has since). Nothing has
run on the board: how long the first painting into the kept picture and a
step take there, and whether a refill is missed, are for the board turn.

## What was learned

- The first try of the test asked a white screen to need all seven steps
  to become the default desktop. It needs five: Steel blue is five levels
  of red from white. A fade of this kind is as long as the farthest
  colour, not seven steps always.
- On the board the "first full paint" figure in the start-up log is now
  the plain colour's when the desktop fades in; on a fast start it is the
  whole desktop's, as before.

## What is left

- The board turn: timing of the steps, missed refills, 800x600, the
  remote screen.
- The owner's answer on A or B, and on the other readings.

## Added the same day: the firmware compiles

The firmware of this branch compiles and links in the cloud container
(ESP-IDF v5.5, `tools/setup-cloud-idf.sh`). It has still not run on the
board. The request for the board turn is
`board-requests/item-74-start-up-fade.md` in the project files.
