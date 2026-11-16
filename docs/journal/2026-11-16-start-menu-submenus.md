# Submenus that closed under a slow pointer

## What was asked

The owner reported (through the coordinator) that in the Start menu, moving
the mouse slowly from the main menu into the Programs submenu closed the
menu, so a program could only be picked by moving the pointer very fast.

## What was found

A PC test moving the pointer 2 px every 20 ms from Programs into the submenu
failed, straight across and on a slant. The cause: the submenu opens flush
against its parent, and the pointer crossing the parent's border (part of no
item) counted as a change of item, which closed the submenu at once. A
slanted path also closed it on the next item it crossed (Settings). A fast
pointer jumped from the item straight into the submenu, which is why speed
worked.

## Options considered

- Overlapping the menus by a pixel: fixes the straight path only.
- A "safe triangle" from the pointer to the submenu's near corners: the
  classic rule, more code.
- A delay: another item takes over only after the pointer rests on it. Simpler,
  and it passes both paths.

## What was chosen and why

The delay: while a submenu is open, the parent's borders, separators and other
items change nothing until the pointer rests on another item for 300 ms (each
move starts the wait again); a click on another item takes over at once.
Menus close only on a click outside, Escape or choosing an item. The same code
serves every cascading menu.

## What was built and tested

`Wm::on_move`, `Wm::switch_due` and the press handling in
`core/src/wm/wm.cpp`; `test_slow_submenu` in `host/tests/test_wm.cpp`
(failed before, passes after); the keyboard menu tests and the single-pass
paint test still pass. On the board: 2 px every 20 ms into Start > Programs,
straight to Editor and on a slant across Settings to Task Manager, the
submenu stayed open with the item highlighted (filmed); a slower path and a
click opened the Editor.

## What was learned

The bug only shows at human speed; the earlier tests moved the pointer in one
jump.

## What is left

Nothing.
