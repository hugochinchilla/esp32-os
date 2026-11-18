# The mouse wheel

2026-11-18. Backlog item 61. Built and tested on the PC. The firmware has not
been compiled for this change, and nothing here has run on the board.

## 1. What was asked

The owner:

> Allow the use of the scrollwheel or gesture on a trackpad to scroll windows
> on chinchilla

## 2. What was found

The wheel already travelled most of the way and then stopped. `ports::MouseEvent`
had a `wheel` field; the debug link filled it (`tools/dbg.py mouse wheel N`),
the remote screen turned a VNC viewer's buttons 4 and 5 into it, the Linux
window passed SDL's wheel on, and the message queue carried it from the
services task to the UI task. `Wm::mouse` never read it. Nothing scrolled.

Three inputs lost part of it:

- The viewer page for a browser (`core/src/net/viewer_page.cpp`) sent one
  notch for every `wheel` event of the page, whatever its size. A mouse wheel
  sends one event a notch; a trackpad sends dozens of small ones a second, so
  two fingers would have flown through a file.
- The Linux window used SDL's whole numbers (`e.wheel.y`), which lose the
  small movements of a trackpad on some systems.
- Sideways movement was dropped everywhere.

There is no USB mouse on the board yet (`firmware/os/main/main.cpp` says "no
USB host yet"), so on the board the wheel can only come from the remote
screen or the debug link today.

Who scrolls: lists (`wm::ListBox`), the Editor's text (`wm::TextEdit`) and a
folder's icons (`IconView` in `core/src/os/ui.cpp`) have a scroll bar inside
them. The Terminal, the Picture viewer and the Widget gallery put a scroll
bar control beside a picture and move the picture when the bar says it
changed. Vizcacha (pull request 45, not merged) does the same.

## 3. The options considered

**Whose wheel it is.**

- A. What is under the pointer, without bringing its window forward. What
  every desktop does today. Costs nothing more than B.
- B. The window in use, wherever the pointer is. What Windows 95 did when a
  wheel driver was added. A window behind cannot be scrolled to read it.

**How a window with a picture and a scroll bar gets it.**

- A. The window gives the wheel to its scroll bar when what is under the
  pointer does not scroll. No program changes.
- B. A new event for programs (`What::Wheel`), and each program moves its
  own bar. A new OS call, and four programs to change, to do the same thing
  in each.

**How far a notch goes.** Three lines is the usual figure. A bar that counts
pixels (the Picture viewer's) would move three pixels, so the step has to
depend on the bar.

## 4. What was chosen, and why

All three are the agent's choices, put to the owner to confirm; they are in
`docs/DECISIONS.md`, "The mouse wheel".

- Under the pointer, the window left where it is (A).
- The window's scroll bar as the fallback (A). No program draws something
  that scrolls without a bar, so the event would have no user yet.
- A notch is three of the bar's steps, or an eighth of the view if that is
  more, and never more than a view: 3 lines in a 25-line Terminal, 4 in a
  maximised one, 50 pixels of a picture 400 pixels high. A folder moves one
  row of icons, since its rows are tall.
- Sideways comes from a trackpad or a tilting wheel, or from the wheel with
  Shift held.

## 5. What was built

- `ports::MouseEvent` has `wheel_x` beside `wheel`. The message queue
  carries it in the spare byte of the mouse message.
- `Wm::on_wheel` finds the window under the pointer and calls
  `Window::wheel`, as the process that owns the window. Not while a button
  is held, a menu is open, or the window is locked or under a dialog. The
  wheel counts as input for the screensaver: it wakes it and scrolls nothing.
- `Widget::wheel` is new: `ScrollBar`, `ListBox`, `TextEdit` and the folder's
  `IconView` answer it. `Window::wheel` asks the topmost widget under the
  pointer, then the window's scroll bars (`Widget::window_bar`).
- Inputs: the debug link's Mouse request takes an optional eighth byte, the
  wheel sideways (`tools/dbg.py mouse wheel N --x M`). The remote screen
  reads buttons 6 and 7 as left and right. The viewer page adds a
  trackpad's small movements up and sends a notch for each 100 pixels. The
  Linux window and the browser build use SDL's exact values and add them up
  the same way (`desktop::WheelNotches`). When the direction turns, the
  part kept is dropped.

Tested on the PC: the 46 tests pass. New checks in `test_wm` (under the
pointer, the window not brought forward, the fallback to the window's bar,
the ends, a held button, the title bar, a dialog over the window, sideways
and Shift), `test_edit`, `test_terminal`, `test_saver`, `test_rfb`,
`test_dbg`, `test_services` and `test_desktop`.

Tried by hand in the cloud container, not kept as tests: the Linux window
on a virtual X screen with real wheel clicks (`xdotool click 5`) scrolled
welcome.txt in the Editor six lines for two notches; the viewer page in
Chromium, with 40 wheel events of 5 pixels, scrolled it two notches, and one
event of 100 pixels the other way brought it back one.

Not tested: a real trackpad (the container has none); the browser build
(no Emscripten here: the code is the same SDL event as the Linux window);
the firmware, which was not compiled; the board.

## 6. What was learned

The first try of the viewer page added the movements as fractions of a
notch. Forty times 0.05 is a hair under 2 in floating point, so the second
notch never came, and the notch back was swallowed by what was left over.
The page now counts whole pixels, and both adders forget the part kept when
the direction turns.

## 7. What is left

- The board: the wheel through the remote screen and `dbg.py mouse wheel`.
- A USB mouse's wheel, when the USB host is built: the report's wheel byte
  goes in `MouseEvent::wheel`.
- An event for programs, if one ever draws something that scrolls without a
  scroll bar.
- Scrolling with the wheel while a selection is being dragged.

## On the board (2026-11-18, d65676e)

App-flashed on the BIOS layout. `dbg.py mouse wheel` scrolls what is under the
pointer: the Widget gallery's list (three notches down moved it to "Lemon" at
the top, two up brought it back), a 200-file folder window's icons while the
window was behind the Editor (the folder scrolled, the Editor stayed in
front: the order did not change), the Terminal's text back through its
history (up is positive), and the Picture viewer's scroll bars at actual
size (vertical and, with `--x`, sideways). `--x 4` scrolled the Editor's
300 KB text sideways. `dbg.py misses`: none. The remote screen's wheel was not
tried.
