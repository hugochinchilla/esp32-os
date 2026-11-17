# The desktop program: Chinchilla in a window on Linux

2026-11-17. Backlog item 32. Built and tested on a PC in the agent's cloud
machine, with a real X server but a virtual screen. It has not yet run on the
owner's desktop. It does not touch the firmware.

## What was asked

The idea is the owner's, from the conversation in
[Chinchilla on a PC](2026-11-17-chinchilla-on-a-pc.md):

> ¿Significa esto que podríamos hacer como una aplicación de escritorio para
> Linux en la que yo puedo ejecutar como si hubiera una máquina virtual con
> Chinchilla OS y probar las aplicaciones y compartirlo con mis amigos?

It became backlog item 32: a window (SDL2) on the same `core/`, the real
mouse and keyboard, sound, a folder of the PC as C:. The browser version is
item 33, later. For the work itself he gave a free hand:

> ve haciendo, no pares hasta terminar el backlog, dejo a tu criterio el
> orden de las tareas y el paralelismo, no tengas más de 4 cosas activas a la
> vez

So the choices below were made by the agent and are written down to be
confirmed or overruled.

## What was found

- Everything the program needs was already behind `core/ports.hpp`: a
  `Display` that is 640x480 bytes of memory, a `Pointer`, a `Clock`, a
  `Volume` for a drive, `Memory`, `System`, and `InputSink` for keys and the
  mouse. `host/host_adapters.hpp` has PC versions of most of them for the
  tests, and `host/folder_volume.hpp` already shows a PC folder as a drive
  (the simulator's D:).
- `wm::Wm::idle_us()` says how long the desktop has nothing to do, and a
  damage listener says what was painted. The board's loop sleeps on the
  first; the remote screen uses the second. The window can use both.
- Keys reach the core as USB keyboard codes ("0x04 is A"). SDL numbers its
  keys the same way, on purpose. No table was needed.
- The pointer is never in the picture. `video::compose_pointer()` mixes it
  into each line on its way to the monitor.
- **There is no sound in the core.** `ports.hpp` has no sound port; the
  only mention is the self-test line, which says "none" on the board too.
  The five system sounds are chosen (`docs/sounds/`) but nothing plays
  them yet.
- The start-up screens draw straight to the screen and wait in a loop that
  calls an `idle` function over and over.

## The options, and what was chosen

### The window library

SDL2 was already named in the backlog. It was checked against the others
anyway: SDL3 is newer but not yet in every distribution; X11 or Wayland
directly is a lot of code for one of the two; GTK or Qt bring a toolkit for
a program that only needs a rectangle of pixels. SDL2 is in every
distribution, and Emscripten ships it, which is what item 33 needs.
**Chosen: SDL2.**

### How 640x480 fills a window of any size

| Option | What it looks like |
| --- | --- |
| Nearest pixel only | Sharp, but at an in-between size some columns are 2 pixels wide and some 1: the dotted desktop pattern shows bands, text looks uneven |
| Smooth only | Even, but soft everywhere, also at exactly 2x |
| Whole multiples only | Always perfect, with a lot of black around it on most windows |
| Hard edges to the next whole multiple, then smooth down | Perfect at 1x, 2x, 3x; in between, every pixel the same size with a slightly soft edge |

**Chosen: the last one.** Two textures and four lines of drawing. The
picture keeps its 4:3 shape and is centred; the rest of the window is
black. The window opens at the largest of 1x, 2x, 3x that fits in nine
tenths of the desktop. `docs/look/desktop-program/window-2x.png` is 2x;
`window-1000x700.png` is an in-between size.

### Whose pointer

1. The PC's own arrow, Chinchilla's hidden. Simple, but the hourglass and
   the resize arrows would be lost, and the arrow would not be ours.
2. Capture the mouse and feed movements, as a virtual machine does. The
   mouse is then trapped until a key frees it.
3. Chinchilla's pointer, mixed in by `compose_pointer` as on the board, with
   the PC's hidden while it is over the window, and positions taken as they
   are.

**Chosen: 3.** The mouse walks in and out of the window freely and what is
shown is what the board shows. A drag that leaves the window keeps
following the mouse (SDL's mouse capture, only while a button is down).

### Keys and letters

The core takes key positions and turns them into characters itself, with a
US layout ("other layouts come with real keyboards", says `window.hpp`).
The alternative was to take the characters from the PC, in the PC's layout,
through `InputSink::text()`. It was not chosen: the core also types the
character for a key that a window does not use, so each letter would arrive
twice unless the core changed, and the program would then behave
differently from the board. **Chosen: key positions, the core's US
layout.** On a Spanish keyboard the letters are where they should be and
some symbols are not, exactly as on the board. Layouts are work for the
core, and both will get them at once.

The PC's own key repeats are dropped, because the core repeats a held key
itself. When the window loses the keyboard, every held key is released, or
Alt would stay down after an Alt+Tab to another program.

Four keys could not go straight through:

- Linux keeps Ctrl+Alt+Del. **Ctrl+Alt+End** stands in for it, as in
  remote-desktop programs.
- Many desktops keep the Super key. Ctrl+Esc already opens the Start menu.
- **F11** switches whole screen and window, **F12** saves a picture of the
  window as a BMP file (which Chinchilla's own Picture viewer opens). These
  two never reach Chinchilla; no program of ours uses them.

### Where C: lives

1. `~/.local/share/chinchilla/C`, where Linux programs keep their files.
2. A folder next to the program.
3. The folder the program is started from.
4. A RAM disk as C: and the folder as D:, as the simulator does.

**Chosen: 1, with `--drive FOLDER` for anything else.** 2 and 3 fail when
the program sits somewhere read-only or is started from a different place
each time; 4 loses everything at exit, which is not what "a folder as C:"
means. The files are ordinary files: a picture copied in from Linux shows
up in the File manager, and a text saved in the Editor can be opened in any
Linux editor. A new, empty folder gets the sample files, as a newly
formatted flash does.

### Sound

Asked for "if practical". **Not built**, and this is the one part of the
item that is missing. There is nothing to play: the core makes no sound and
has no port to send one through. Playing the chosen WAV files from the PC
side at start-up was considered and rejected: it would be sound that the
board does not make, written outside `core/`, to be thrown away later. The
right order is the sound port in the core first (it comes with the audio
output on GPIO13); the PC adapter for it is then a few lines of SDL audio,
and the browser gets it for free.

### Wi-Fi

1. None: the Network panel says it is not available.
2. The simulator's pretend radio, with its three made-up networks.
3. The PC's own connection shown as a network already joined.

**Chosen: 1.** The pretend networks are right for tests and wrong for a
friend who tries to join "Casa Chinchilla". 3 is the honest future answer,
and is a new adapter; nothing needs it yet. The clock is set from the PC's.

### Start-up, shutting down, restarting

- The start-up screens show, as on the board, with the settings of
  `C:\.config\boot.ini`. The 2.5 s wait for the monitor is dropped: a
  window is ready at once. `--fast`, a key or a click skips them. The
  self-test says "Main Processor : this PC (not an ESP32)".
- Closing the window is Shut Down: every program is asked to close, so the
  Editor asks about unsaved text and the window stays. Closing it again
  within 3 seconds leaves at once. The other option, leaving at once every
  time, loses work without a word.
- Start > Shut Down shows the "safe to power down" screen and stays there,
  as the board does, until the window is closed.
- Restart starts the program again from the top (it runs itself again),
  boot screens included.

### How it is built

It is one more target in `host/CMakeLists.txt`, built only when SDL2 is
found, so the cloud machines and anyone without SDL2 still build and run
every test. Built with the tests it has the memory checks on (11 MB,
slower); the README gives the plain build for using it and handing it on
(894 KB). A friend needs Linux, the same kind of processor and SDL2
installed. Packing SDL2 inside the file (an AppImage, a static build) was
left out: the browser version is the real answer to "share it with
friends".

## What was built

- `host/desktop/main.cpp`: the adapters and the loop. One round sleeps
  until input arrives, the desktop's next timer is due or the window needs
  drawing; hands the input to the desktop; lets it run its timers and
  paint; and shows the result, at most 60 times a second and only when
  something changed.
- `host/desktop/screen.hpp`: the parts with no SDL in them: the board's 256
  colours as the PC's (the same table as `tools/dbg.py`: blue's two bits
  give the levels 0, 2, 5, 7), the pointer mixed in, where the picture sits
  in the window, and a place in the window as a place on the screen.
- `README.md`: how to install SDL2, build, run, the options and the keys.
- `ci/github-build-workflow.yml`: installs SDL2 for the PC tests (not
  active until the owner copies it, as before).

`core/` and `firmware/` are not changed.

## How it was tested

- `host/tests/test_desktop.cpp` (new, runs everywhere): colours, the
  picture's place in windows of several shapes, the mouse at the corners,
  the middle and in the black bars, and the pointer mixed in without
  touching the picture.
- `tools/test_desktop_program.py` (new, runs where SDL2 is): starts the
  program on SDL's "dummy" screen, which exists only in memory, with a new
  empty folder as C:. Checks that the samples arrive as files, that every
  pixel in the window is one of the board's 256 colours, that the taskbar
  is there, and that at `--scale 2` every screen pixel is a sharp 2x2
  square.
- By hand, on a virtual X screen (Xvfb, 1600x1100) with `xdotool` playing
  the mouse and keyboard: the start-up screens and skipping them with a
  key; a double click on `welcome.txt`; typing "Hola desde Linux! " (capital
  letters and the exclamation mark included); Alt+F, S to save, and the
  text found in `C/Documents/Desktop/welcome.txt` on the PC; closing the
  window with unsaved text (the Editor's question appears, the program
  stays); Ctrl+Alt+End (Task Manager opens); Ctrl+Esc and the Start menu
  with the mouse; Cactus Patch and Culebra opened, a window dragged by its
  title, Culebra played with the arrow keys and paused with P; the window
  resized to 1000x700; Restart (the program starts again); Shut Down and
  then closing the window (the program ends).
- Idle, the plain build used no measurable processor time in 6 seconds.
- All 24 PC tests pass, with the address and undefined-behaviour checks on.

Not tested: a real desktop with a window manager (the virtual screen has
none, so the frame, the close button and Alt+F4 as the desktop sends them
were stood in for by the same message sent by hand); Wayland; a graphics
card (the virtual screen draws with the processor); a high-density screen;
a keyboard that is not US; the mouse wheel (it is passed on, and the core
does nothing with it yet).

## What was learned

- The split between `core/` and the adapters held. The program is about
  700 lines and the core did not need a single change. The first build
  showed the desktop.
- SDL's key numbers being USB's saved a table of a hundred lines, and a
  class of mistakes with it.
- Task Manager and the self-test show the simulator's pretend memory
  (400 KB, 8 MB). On a PC those are a story, not a limit. They were left,
  because making the PC's gigabytes fit those screens is not this item.

## What is left

- **Sound**, with the core's sound port (no backlog number yet; it comes
  with the audio output).
- **Keyboard layouts**, in the core.
- **Item 33, the browser.** What is ready: SDL2, the one-call loop
  (`Machine::step()`), a drive that is a folder (Emscripten can keep a
  folder in the browser's storage), no threads. What will need work: the
  start-up screens wait inside a loop, which a browser does not allow
  (skip them there, or build with Emscripten's Asyncify); Restart runs the
  program again, which becomes reloading the page; the wait for events
  becomes the browser's own frame call.
- New permanent `static` window objects: none added.
