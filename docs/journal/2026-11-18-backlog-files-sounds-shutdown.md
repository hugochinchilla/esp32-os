# Nine requests for the backlog: files, transmit power, sounds, Shut Down

2026-11-18. Backlog items 54 to 60. Nothing built.

## What was asked

The owner, in the project chat:

> for the backlog:
>
> * right click  on file should have dialog, copy, cut, delete
> * right click on folder, the explorer blank space or desktop shows paste as an option
> * explorer menu needs entries for copy, paste, cut and delete too for selected items
> * allow multiple file selection with ctrl+click or drawing a rectangle with the cursor while clicking
> * implement explorer view as list and list with details with a mode switcher under "view" menu, keep current mode too
> * add a setting on the network app to modify the tx power of wireless under and "advanced..." button
> * implement sounds, using a null sound driver on the board for now, but real implementations on linux and browser.
> * desktop welcome chime, shutdown short melody
> * on shutdown, in order, close all apps (let them wait, force kill after timeout of 20 seconds), fade desktop to background color (icons and taskbar disappear), then the "safe to turn down" shows, replace that screen background color with full black

## What was found

- The highest number on `main` was 52; 53 is Vizcacha on PR 45, still open.
  The new items start at 54.
- The File manager (`core/src/shell/file_manager.cpp`) has Open, Rename and
  Delete (to the Trash) in its File menu, for one selected item. Files move
  by drag and drop. There is no copy, no clipboard of files and one view.
- The transmit power is already a setting, `tx_power_dbm` in
  `C:\.config\network.ini`, 2 to 20 dBm, default 20 since 2026-11-18
  (`DECISIONS.md`). The Network program does not show it.
- The system sounds were chosen on 2026-11-16 and are in `docs/sounds/`
  (`boot.pcm`, `close.pcm`, `alert.pcm`, `error.pcm`, `bell.pcm`); the
  options heard are in the project files under `look/sounds/`. No firmware
  plays them and the core has no sound port. The beeper is another matter:
  item 48, PR 51, parked by the owner, with a handoff note in
  `docs/handoff/beeper.md` on branch `claude/project-thread-l7amx9`.
- Shut Down today asks every program to close; an unsaved file calls it off
  and it has to be chosen again. The last screen is the mascot asleep under
  a night sky. There is no fade and no timeout.

## What was written

Nine requests as seven items, grouped where one cannot be built without the
other:

| Item | The owner's lines |
| --- | --- |
| 54 | The three about Copy, Cut, Paste and Delete: on a file, on a folder, the blank space or the desktop, and in the menu bar |
| 55 | Selecting several files |
| 56 | List and details views |
| 57 | Transmit power under "Advanced..." |
| 58 | Sounds, with a null driver on the board |
| 59 | Welcome chime and Shut Down melody |
| 60 | The Shut Down sequence |

Two readings made while writing, to confirm when each item is taken up:

- "should have dialog" in the first line is read as the menu a right click
  opens, with Copy, Cut and Delete in it.
- "safe to turn down" is the "safe to power down" screen.

## What is left

Everything: these are entries only. Each item has design choices that are
the owner's when its thread starts, among them: whether Delete from the
menu goes to the Trash as today, what Paste does with a name that already
exists, which details the details view shows, whether the sounds of
2026-11-16 are still the chosen ones, and what the last screen keeps of
its drawing once its background is black.
