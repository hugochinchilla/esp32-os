# Chinchilla in the browser

2026-11-17, the same night as the [desktop program](2026-11-17-desktop-program.md).
Backlog item 33. Built in the agent's cloud machine and tested there in
Chromium. It is **not published yet**, and it has not been opened in Firefox,
Safari or on a phone. It does not touch the firmware or `core/`.

## What was asked

Item 33 as the owner left it: "Chinchilla in a web browser (WebAssembly): a
link anyone can open, C: kept in the browser, the page published somewhere
(GitHub Pages?)". It follows from his question in
[Chinchilla on a PC](2026-11-17-chinchilla-on-a-pc.md) about showing it to
friends. The choices are the agent's, under the same free hand as item 32
("dejo a tu criterio"), written down to be confirmed or overruled.

## What was found

The desktop program had been written with this in mind, and its journal
entry listed three things that would need work. All three were right:

1. The start-up screens wait in a loop (`wait_until` in `boot.cpp` calls an
   idle function over and over). A browser runs a page's program and the
   page itself on one thread: while the program loops, nothing is drawn and
   no key arrives.
2. Restart ran the program again with `execv`. A page has no such thing.
3. The loop slept until the next event. In a browser the browser calls you,
   once per frame.

One thing was not foreseen: Emscripten's compiler is Clang, and with
warnings as errors it stopped on three leftovers in `core/` that GCC (the
compiler of the board and of the PC tests) accepts: an unused constant
`kLines` in `boot.cpp`, a variable `g_file_timer` in `desktop.cpp` that is
set and never read, and an unused field `held_` in `widgets.hpp`.

## The options, and what was chosen

### One source or two

A second `main.cpp` for the browser, or the same one with the differences
marked. **Chosen: the same one.** The differences are about 90 lines out of
700, each marked `__EMSCRIPTEN__`: where the window's size comes from, the
drive, the wait in the start-up screens, the loop and Restart. Everything
else (colours, the pointer, scaling, the mouse, the keys) is the same code,
so a fix to one is a fix to both. The build is its own small file,
`web/CMakeLists.txt`, because Emscripten wants its own compiler and none of
the PC tests.

### A program that waits, in a browser

| Option | Cost |
| --- | --- |
| No start-up screens in the browser | Nothing to build, and the visitor never sees the self-test |
| Rewrite the start-up screens in `core/` as steps that return | A change to the core, for the board too, to suit the browser |
| Asyncify: Emscripten rewrites the program so it can be put aside in the middle of a function and picked up later | A larger and somewhat slower program; no change to our code beyond one call |
| JSPI, the browsers' newer way to do the same with no rewriting | Not in every browser yet |

**Chosen: Asyncify.** The start-up screens are a good part of the charm, a
link "anyone can open" rules JSPI out for now, and the PC that opens the
page is so much faster than the board that the slowdown does not show. The
size it adds was not measured on its own.

### Where C: lives

1. In memory only: a fresh C: on every visit.
2. IndexedDB, the browser's own database, through Emscripten's IDBFS.
3. The browser's newer private file system (OPFS), through Emscripten's
   newer file layer.
4. A real folder of the visitor's PC, which Chromium can offer after asking.

**Chosen: 2.** It works in every current browser, and `host/folder_volume.hpp`
sits on top of it unchanged, because to the program it is still a folder.
The stored copy is read back before anything starts, and every change is
written back a moment later by itself (IDBFS's `autoPersist`), so there is
no "save to browser" step to forget. 3 needs threads or a different file
layer. 4 is Chromium only and asks a question before the desktop appears.

The files never leave the visitor's computer: there is no server behind the
page. In a private window, where a browser refuses the database, C: lasts
until the page is closed. The self-test's drive line and My Board call the
drive "in the browser" instead of "PC folder".

### How the picture fills the page

A fixed 640x480 canvas stretched by the page's style sheet, or a canvas as
large as the page with the program scaling into it. **Chosen: the second**,
because it is the code the desktop program already has (hard edges to a
whole multiple, then smoothed), and the browser's own stretching gives
either blur or uneven pixels. The canvas is given as many real pixels as
the page has, so it is also sharp on a dense screen.

### One file or three

Emscripten normally makes a page, a script and the program (`.html`, `.js`,
`.wasm`). It can also put all three in the page. **Chosen: one file**,
`index.html`, 1,741,148 bytes (615,055 as a web server sends it,
compressed). It is about a third larger than three files would be, and in
exchange it cannot be copied incompletely, and it works when opened
straight from a folder, so it can be sent to a friend as a file. That was
tried: it starts and has its C:.

### How it gets published

| Option | What it needs |
| --- | --- |
| GitHub Pages, built by a workflow on every change to `main` | The workflow file in `.github/workflows/` and Pages turned on in the repository's settings; both are the owner's to do, because agent sessions cannot push workflow files |
| The built page committed to the repository (`docs/`, or a `gh-pages` branch) | 1.7 MB added to the history at every change, and a page that is stale whenever someone forgets to rebuild |
| Another host (itch.io, Netlify) | An account and a manual upload |

**Chosen: the workflow**, as `ci/github-pages-workflow.yml`, next to the
build workflow that waits in the same folder for the same reason. The
repository is public, which Pages needs on a free account. The address
will be `https://hugochinchilla.github.io/esp32-os/`. Until it is turned
on, the page exists only as the file.

### Keys

The browser keeps some keys and a page cannot have them: Ctrl+W, Ctrl+T,
Ctrl+N, F5, F11 and a few more. The desktop program's F11 and F12 are left
out here (the browser has its own whole screen and its own tools on those
keys). Ctrl+Alt+End still stands in for Ctrl+Alt+Del.

## What was built

- `host/desktop/main.cpp`: the browser's differences, marked.
- `host/folder_volume.hpp`: the drive's label can be given (default as
  before, "PC folder").
- `web/CMakeLists.txt` and `web/shell.html`: the build and the page around
  the canvas.
- `ci/github-pages-workflow.yml`: builds with Emscripten 7.0.0 and
  publishes on Pages. **It has never run.**
- `tools/test_web_page.js`: the test below.
- `README.md`: how to build, try and publish it.

The three Clang findings are let through as warnings in `web/CMakeLists.txt`
and left in `core/` for whoever is next in those files, since `desktop.cpp`
is being worked on in other branches.

## How it was tested

`tools/test_web_page.js` opens the page in Chromium (through Playwright)
with a real profile on disk and uses it:

- the start-up screens appear and a key skips them;
- a new C: has the sample files;
- a double click opens `welcome.txt`; "Hola desde el navegador! " is typed
  with Shift held for the capital and the exclamation mark; Alt+F, S saves
  it, and the text is in the file;
- the page is loaded again and the text is still there;
- the page is made 1000x700 and a click on Start, through the scaled
  picture, opens the Start menu;
- Start > Shut Down > Restart loads the page again;
- the page reported no errors.

Also by hand: a dense screen (2 real pixels per page pixel: the canvas was
1800x1200 for a 900x600 page, and a drag selected text where it pointed),
and the file opened straight from a folder.

All 24 PC tests still pass; the desktop program is the same file.

Not tested: Firefox, Safari, a phone or tablet (it needs a mouse and a
keyboard; a touch should arrive as a click and was not tried), a real
server, the Pages workflow, and how much a browser lets C: grow.

## What was learned

- The first build showed the start-up screens and the second key press the
  desktop. Everything the browser needed had already been asked of the
  desktop program, which is what building 32 first was for.
- The cloud machine could not download Emscripten's SDL2 the usual way (its
  network refused that one address) and was given a copy fetched with git
  instead. GitHub's own machines have no such limit.
- A second compiler is a free review: Clang found three leftovers.

## What is left

- **Turning the publication on** (the owner): copy the workflow, turn Pages
  on, and see the first run pass.
- The three leftovers in `core/`.
- Sound and keyboard layouts, as for the desktop program: both come from
  the core and will reach the page with no work here.
- New permanent `static` window objects: none added.
