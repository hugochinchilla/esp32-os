# The hourglass on demand

2026-11-18. Backlog item 49. Built and tested on the PC. The firmware has not
been compiled for this change, and nothing here has run on the board.

## 1. What was asked

The owner, in the message that also asked for the chess game
(`2026-11-18-chess-design.md` has the whole of it):

> **The hourglass can be show on demand by apps.** An app blocked or
> thinking can set the cursor to show the hourglass while the cursor it's
> over the app. The app knows when it's entering a locking or slow path and
> requests it, then on completion goes back to normal cursor. The chess
> game will show the hourglass while the engine calculates a move.

## 2. What was found

The pointer's shape is chosen in one place, `Wm::update_pointer_shape` in
`core/src/wm/wm.cpp`, each time the mouse moves. Before this change a program
could get the hourglass (`video::busy_pointer()`, the drawing chosen on
2026-11-16) in two ways:

- `os::ui::set_locked(window, true)`: the hourglass over the whole window
  except its sizing borders, and the window takes no keys, no clicks in its
  client area or menu bar, no dropped files and no request to close. The
  Editor uses it while a save runs on another task.
- `os::Busy` (`wm::Wait` underneath): the hourglass over the whole screen
  until the end of a block of code, for work done on the UI task while
  nothing else can happen.

Neither is what a game needs while its engine thinks: the window must stay
alive (the user may open the menu, start a new game, close it), and the rest
of the screen is not waiting for anything.

Two smaller things were found on the way. The shape was worked out only when
the mouse moved or a lock changed, so a window that closed or was minimised
under a pointer at rest left its hourglass on the screen until the next
move. A locked window cannot be closed, so nobody had seen it; a window with
the hourglass on demand can.

## 3. The options considered

Put to the owner on a card:

1. **An on/off switch, the hourglass inside the window only.** Over the
   client area the pointer is the hourglass; over the title bar, its buttons
   and the menu bar it is the arrow, because they still answer. One flag in
   the window, one test more where the shape is chosen.
2. **An on/off switch, the hourglass over the whole window**, as a locked
   window shows it today. The same cost. The pointer would say "wait" over a
   Close button that works.
3. **Any pointer shape for a window**: a wider call where the program names
   the shape. More than was asked, shapes to keep valid, and more to test;
   no program needs it today.

Not put on the card because it does not fit: a block-scoped object like
`os::Busy`. An engine thinks across many events (on a job, or a slice at a
time on a timer), not inside one call, so there is no block to scope it to.

## 4. What was chosen, by whom, and why

The owner chose option 1, which was the agent's recommendation. The window
is alive, so the arrow over the things that still work tells the truth; the
hourglass over the board says the program is busy with it.

Smaller choices, the agent's, each easy to change:

- The name is `set_hourglass`, not `set_busy`: "busy" already means the
  whole-screen wait (`os::Busy`), a late program (`proc::Table::busy`) and
  processor time (`os::busy_us`).
- It is a switch, not a count: two calls with `true` and one with `false`
  leave it off. One window has one program behind it, which knows whether it
  is working; a count that came out wrong would leave the hourglass on for
  good.
- Nothing is kept past the window: a window that closes with the hourglass
  on takes it with it, and a window opened again starts with the arrow.
- A menu that is open, a drag under way and a sizing border keep their own
  pointers over a window with the hourglass on, as they do everywhere.
- The whole-screen wait and a locked window win over it where they apply.

## 5. What was built

- `wm::Window::set_hourglass(bool)` and `hourglass()`
  (`core/include/core/wm/window.hpp`), and in `Wm::update_pointer_shape` the
  hourglass when the window under the pointer has it on and the pointer is
  over its client area (`Part::Client`). Turning it on or off changes the
  pointer at once, without waiting for the mouse to move.
- The OS calls `os::ui::set_hourglass(window, on)` and
  `os::ui::hourglass(window)` (`core/include/core/os/ui.hpp`), and
  `set_hourglass(on)` and `hourglass()` in `app::Window`
  (`core/include/core/app/app.hpp`). Listed in `docs/os-calls.md`. A program
  kept in a file gets them like any other OS call: the table of exports is
  written from the core library by `tools/gen_exports.py`.
- `Wm::close` and `Wm::minimize` work the pointer's shape out again, so an
  hourglass does not stay behind over a window that has gone.
- Something to try it with until Chess exists: Widget gallery > Options >
  Hourglass, ticked while it is on. The gallery's Options menu is one row
  longer for it.

Tested on the PC:

- `test_wm`, `test_hourglass`: the hourglass at once when asked for with the
  pointer at rest; over the corners of the client area; the arrow over the
  menu bar, the title bar, Close, outside, and over another window; the
  sizing arrow over a border; the window's menu still opens and runs, with
  the arrow while it is open; a window in front covers it; moved by its
  title bar; on twice and off once; minimised and restored; under the
  whole-screen wait; closed with it on.
- `test_os`, in `test_ui`: the same through the OS calls by number, the
  program still getting its keys, a number of no window, and a window closed
  with it on.
- The picture tests: `10-gallery-menu` changed (the new row) and
  `10b-gallery-hourglass` is new, with the pointer drawn in.
- The whole suite: 44 of 44 pass. `tools/check_os_calls.py`: 20 of 20.

Not tested: the firmware was not compiled for this change (no adapter was
touched; everything is in `core/`), and nothing has run on the board.

## 6. What was learned

- The first run of the picture tests showed 16 pictures changed, not 2. The
  new scene moved the mouse with the helper that also moves the clock on by
  0.3 s, and every later picture with a clock or a game in it shifted. The
  scene now moves the pointer without touching the clock.
- The four tool tests (`dbg_tool`, `vnc_tool`, `ota_tool`,
  `serial_console`) fail in a fresh cloud container for want of `pyserial`;
  `pip install pyserial` and they pass.

## 7. What is left

- A check on the board: the Hourglass row of the gallery, by a screenshot
  with the pointer over the window and beside it, and `tools/dbg.py misses`.
- Chess (item 50) turns it on while the engine thinks and off when the move
  is made.
