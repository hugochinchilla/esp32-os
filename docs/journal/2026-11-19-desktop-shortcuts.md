# Two keys for the desktop: Alt+F4 and logo+D

2026-11-19. Backlog item 76.

## What was asked

The owner, in the project chat:

> más tareas:
>
> * alt+f4 en el escritorio abre dialogo de shutdown
> * win+d minimiza todo y muestra el escritorio

## What was found

All system keys are handled in one place, `Wm::handle_key` in
`core/src/wm/wm.cpp`, before a key reaches a window: Ctrl+Alt+Del (handed
to the shell through `set_attention_key`), the Start key, Alt+F4, Alt+Tab
and Alt+letter for a menu bar.

- Alt+F4 asked the window in the focus to close. With no window in the
  focus it was swallowed and did nothing. A click on the desktop already
  takes the focus from every window (`set_active(nullptr)` in `on_press`),
  so "the desktop has the focus" was already a state the window server
  knows: `active_ == nullptr`.
- The logo key opened the Start menu the moment it went down. A logo+D
  could not be told from a logo key alone: by the time D arrived the menu
  was open and D went to the menu as an access letter.
- `Wm::minimize` puts one window away and gives the focus to the next one
  showing. Called for each window in turn it would activate and repaint
  every window below on the way down.
- The Shut Down dialog is a program that opens once
  (`shell::shut_down_dialog`, `app::bring_forward`) and holds the whole
  desktop while open.

## The options

Alt+F4:

1. With no window in the focus (chosen). It includes a desktop on which
   windows show but none is active, after a click on the background. That
   is what 98 does.
2. Only with no window showing. Simpler to say, but then a click on the
   desktop and Alt+F4 does nothing, which reads as a broken key.

Logo+D:

1. Minimise everything, and bring it back at the second press (chosen).
   One flag per window, `put_away_`, says the key minimised it; no list
   and no pointer is kept.
2. Minimise only. Less code by about fifteen lines, but the way back is a
   click per taskbar button.

The logo key:

1. Act on the release, if nothing else was pressed meanwhile (chosen).
2. Keep acting on the press and close the menu when D follows. The Start
   menu would be painted and taken away at every logo+D, a flash on the
   screen and two repaints for nothing.

## What was chosen

The first option of each, by the agent; the owner asked for the two keys
and nothing more, so all three are recorded as `to-confirm` in
`docs/decisions/desktop-shortcuts.md`.

## What was built

- `Wm::set_leave_key`: a second handler the shell gives the window server,
  beside the one for Ctrl+Alt+Del. Alt+F4 calls it when no window has the
  focus, no dialog holds the whole desktop and the key is not a repeat.
  The desktop shell registers the function the Start menu's "Shut Down..."
  already calls (`core/src/shell/desktop.cpp`).
- `Wm::show_desktop`: closes any open menu; if a window shows, marks and
  minimises every showing window in one pass and leaves no window in the
  focus; if none shows, restores the marked ones in stack order and
  activates the top one. A window restored or minimised by hand loses its
  mark. Nothing happens behind a dialog that holds the whole desktop.
- The logo key: `logo_alone_` is set when it goes down and cleared by any
  other key press; the Start menu toggles when the key comes up with the
  flag still set. Ctrl+Esc is unchanged.
- `repeating_` marks a key press that comes from the repeat timer, so that
  a held logo+D does not flip the desktop back and forth at 30 a second
  and a held Alt+F4 asks once.

No port, adapter or firmware file changed: the keys arrive as the USB
usage codes they always had (the logo key is 0xE3 and 0xE7), from the USB
keyboard, the debug link and the remote screen alike.

Tested on the PC: `test_desktop_shortcuts` in `host/tests/test_wm.cpp`
(focus and no focus, the repeat, the stack and focus after the return, a
window brought back or minimised by hand, a maximised window, the Start
key on release, an open menu, a dialog holding the desktop, no stale
pixels) and the real dialog in `host/tests/test_shell.cpp`. All 54 PC
tests pass. The firmware was not compiled for this change and nothing has
run on the board.

## What was learned

- `tools/dbg.py` and the remote screen's logo button send the press and
  the release together, so nothing that drives the Start menu with them
  had to change.
- Ctrl+Alt+Del stops its own repeat inside `handle_key`, but `Wm::key`
  arms the timer again right after, so a held Ctrl+Alt+Del still reaches
  the handler a second time after the pause. It is harmless (the Task
  Manager comes forward) and was left alone; `repeating_` would fix it in
  one line.

## What is left

- The board check: both keys from a real keyboard. A keyboard over
  Bluetooth (item 51) sends the same usage codes once that is merged.
- The owner's word on the three choices above.
- A Show Desktop button on the taskbar, if wanted.

## Confirmed by the owner

2026-11-19, later the same day. Asked whether the three choices were right
(Alt+F4 with no window in the focus, a second logo+D bringing the windows
back, the logo key acting on release), the owner answered "si, son
correctas". The decision is now `decided`. The coordinator reports that the
board checks passed; the board thread writes its own figures here when it
merges.

## On the board (2026-11-19, f622f5a)

App-flashed on the 3 MB layout (`esp32-os.bin` 1,758,416 bytes), keys sent with
`dbg.py`. The logo key: one tap opens the Start menu and a second closes it;
`key down gui` alone opens nothing and `key up gui` then opens it; Escape closes
it. Alt+F4 on the empty desktop opens "Shut Down Chinchilla" and Escape closes
it with the desktop as before. Alt+F4 with the Editor in the focus closes the
Editor and opens no dialog. After a click on an empty place of the desktop,
Alt+F4 opens the dialog and the Editor stays open behind it. Logo+D with the
Editor, My Board and Culebra open: the three go to the taskbar minimised, the
bare desktop shows with no Start menu; logo+D again brings the three back at the
same positions. Ten logo+D in a row: no missed refill; five of the keys took 29
ms (about 190,000 pixels painted each, one frame is 16.8 ms) and nothing else
went over a frame. Logo+D with the Shut Down dialog open changes nothing. Free
RAM before opening the three programs and after closing them: 188,503 internal
and 5,953,968 / 5,954,008 external. A real USB keyboard was not tried. The owner
then confirmed the three readings.
