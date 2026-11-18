# Selecting several files

2026-11-18. Backlog item 55. Built and tested on the PC. The firmware has not
been compiled for this change, and nothing here has run on the board.

## 1. What was asked

The owner, in a list of nine requests for the backlog:

> allow multiple file selection with ctrl+click or drawing a rectangle with the
> cursor while clicking

and, about the commands of item 54 (Copy, Cut, Paste, Delete), "for selected
items". Item 54 is being built at the same time on another branch
(`claude/project-thread-f0mrc9`).

## 2. What was found

- The File manager's icon view (`IconView` in `core/src/os/ui.cpp`) held one
  number, `selected_`. A click anywhere in an entry's 76 by 66 cell picked it,
  so a folder with files in it had almost no empty place: a 10 pixel margin
  and what is past the last entry.
- A mouse message carries no modifier keys (`wm::MouseMsg`); the window
  manager knows them (`Wm::modifiers()`), and a widget can ask it.
- A drag carried one path, in a 160 byte buffer of the window manager
  (`Wm::begin_drag`). While a file was dragged over a folder window, the
  window showed the folder it would go into by *selecting* it
  (`view_.select(i)`), which threw away what was selected.
- Every target reads the folders again after each drop (`files_changed()`),
  and opens a box for each file it cannot move.
- The desktop icons (`core/src/desk/icons.cpp`) held one number too, and an
  icon let go is put with its picture under the pointer.

## 3. The options considered

**How far it goes.** The owner asked for two gestures. Put to him as a
question with three answers:

1. The File manager and the desktop, with Shift+click, Ctrl+A and dragging
   what is picked together (recommended).
2. The same in the File manager only.
3. Only what was asked: Ctrl+click and the rectangle in the File manager, and
   a drag still moves one file.

**Carrying several files in a drag.**

1. The window manager keeps a list of paths for the length of the drag, in
   memory asked of the Memory port.
2. A fixed table in the window manager: 160 paths of 160 bytes is 25 KB held
   always, for something that happens now and then.
3. The window manager keeps the one path and asks the program that started
   the drag for the others when they are let go. No memory, but the desktop
   reads the folders again after each drop, the File manager's list is then
   another, and "the third picked entry" is no longer the same file.

**What a click picks.** Keep the whole cell, or the picture and the name
only. With the whole cell there is nowhere to start a rectangle in a folder
that has files in it.

**The look of the rectangle.** A dotted line, or a filled, see-through one.
The second is from later systems and costs a repaint of everything under it
at each movement.

## 4. What was chosen

The owner had not answered when the work was done, so it follows the
recommendation and is his to confirm or overrule (`DECISIONS.md`, "Chosen for
selecting several files"): the first answer, because picking five files and
then being able to drag one feels broken, and the desktop is where files are
dragged to and from.

The rest was chosen while building: the list of paths in memory for the length
of the drag (the first option; 1 KB, doubling, given back when the drag ends),
the picture and the name as what a click hits, and the dotted line.

## 5. What was built

- `IconView` keeps a bit for each of its first 256 entries. A click picks one
  alone; Ctrl+click adds or takes away one; Shift+click picks from the last
  one clicked to this one; a rectangle pulled from an empty place picks what
  it touches, and with Ctrl adds to what was there; Ctrl+A picks all; Shift
  with an arrow key stretches the range. Only the entries that change and the
  four sides of the rectangle are painted again at each movement.
- Three OS calls: `os::ui::selected_count`, `is_selected` and `highlight`
  (`app::IconView` has them). `selected()` is the one picked last. `highlight`
  shows the folder a dragged file would go into without picking it, so what is
  picked survives being dragged over its own window.
- `Wm::add_to_drag(path, desktop_only)` and `Wm::drops_to_come()`, and the OS
  calls `os::add_to_drag` and `os::drops_to_come`. Targets are asked about the
  first path only; let go, each path is dropped in turn.
- The File manager drags what is picked, moves each file dropped on it, reads
  the folders once after the last and opens one box: the first that could not
  be moved, and "2 more could not be moved either". Its status bar says
  "3 objects selected, 30 bytes".
- The desktop icons: Ctrl+click and the rectangle. Dragged together they each
  go as far as the one held. Files dropped from a folder take the place where
  they were let go and the nearest free cells. An icon that stands for no
  file stays on the desktop when the files it was picked with go into a
  window, a folder or the Trash.

- Item 54's branch was merged in here once its commands worked (commit
  `a769b00`). Its Cut, Copy, Delete and Restore go through one function of
  the folder window, `for_selected`, which now walks all that is picked;
  Delete asks once ("these 2 items"). A right click on one of several picked
  entries keeps them and gives a menu of what goes for all (Cut, Copy,
  Delete; no Open, no Paste into one of them); a right click anywhere else
  picks that entry alone, as before. The same on the desktop, where the
  commands act on the files among the picked icons. File > Open and Rename
  are greyed while more than one is picked.

Tested on the PC: `test_wm` (`test_drag_several`: the order of the drops, the
count still to come, Escape, the desktop's own icons moving together, files
from elsewhere not landing on one another, the rectangle growing and
shrinking, no stale pixels) and eleven scenes in `render_scenes`
(`host/tests/pictures/77a` to `77k`), each of which checks on the drive which
files moved or went to the Trash. One older picture changed, `37l-dragging-to-folder`: the file
being dragged stays picked and the folder under it is lit as well. 50 of 50
tests pass.

Not tested: the firmware was not compiled, and nothing ran on the board. How
fast the rectangle follows the pointer there is not known.

## 6. What was learned

- The tests found two things. A press on the desktop followed by a drag begun
  by something else left the rectangle switched on, since the panel is never
  told that the button came up; the next press now clears it. And a name that
  stands for no file, dropped on the desktop when no icon has it, was handed
  on as a file.
- The first version kept the list of paths for good once it had grown. The
  leak check of the PC tests objected, rightly: the block now goes back when
  the drag ends.

## 7. What is left

- This branch holds item 54's commit as well, so it goes into `main` after
  that one, or with it.
- The scenes cover the right-click menu and Delete of several. Cut, Copy and
  Paste of several are covered by item 54's own test (`test_copy_cut_paste`,
  lists of 2 and 3), not by the mouse. The menu for several picked icons on
  the desktop has no test of its own.
- The view does not scroll by itself while a rectangle is pulled past its
  edge.
- A board turn: the rectangle's speed, and `tools/dbg.py misses` after it.

## 8. The owner's answer

At 18:24 UTC the same day, after the pull request was open, the owner chose
the first answer: the File manager and the desktop, complete. Nothing in the
code changes for it.

## 9. The first board turn found a defect

The board turn of 2026-11-18 (commit `1960963`) passed every check but one. On
the desktop a folder icon and a file icon (`big` and `welcome.txt`) were picked
with a rectangle and the folder was dragged to an empty place. Neither icon
moved, and `welcome.txt` was moved into `big`.

Why: the drops come one after the other, the one held first. The folder was
put down under the pointer; then the file was dropped at the same point, the
desktop asked which icon is there that takes files, and the answer was the
folder that had just arrived. The move made the desktop read its files again,
which put the folder back where its saved place was. Two plain files worked
because a file's icon takes no drops.

The fix (`Icons::dropped`): whether the drag went onto an icon is decided by
the first drop alone; when it went to a place on the desktop, the others
follow it there and no icon is asked. `test_drag_several` now drags an icon
that takes files together with a file; without the fix it fails on the same
two things the board showed. 50 of 50 on the PC. Not compiled for the board
and not run there yet.

Two more things were noted in that turn and not reproduced, and are not
explained: external RAM 187,668 bytes lower at the end of the session with all
windows closed, and once the Display dialog left open with the Sky theme saved
after a right click on the desktop, Escape and a click. Nothing in this branch
was found that would do either; the list of a drag (1 KB) goes back when the
drag ends, which the PC leak check covers.
