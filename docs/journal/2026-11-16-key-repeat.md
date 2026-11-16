# A letter typed after a long save from the menu

## What was asked

Nothing: found during the board check of PR 12 (safe saves). Saving a 300 KB
file with File > Save (Alt+F, then S) saved it, and then an "s" appeared in
the Editor's text, marking the document as changed again.

## What was found

The PC test of the same keys passed, because a save there is instant. On the
board the save takes 1 to 2.6 s. `Wm::key()` started the key-repeat timer
(500 ms, then every 33 ms) before handling the press. The menu took the S and
ran Save, which kept the UI task busy for over a second; when the loop came
back, the repeat was overdue and fired before the key's release was read. By
then the menu was closed, so the repeated S went to the Editor as a letter.
It is on main, not in PR 12: any key press whose handling takes longer than
half a second could repeat at once.

## Options considered

- Start the repeat after the press has been handled, so the pause counts from
  the end of its own handling. One move of three lines.
- Never repeat keys that a menu takes: also fixes this case, but arrows in
  menus should repeat, and other slow presses would still leak.

## What was chosen and why

The first: it fixes every slow key, not just menus.

## What was built and tested

`Wm::key()` in `core/src/wm/wm.cpp`. `test_slow_key_does_not_repeat` in
`host/tests/test_wm.cpp`: a window whose key handler takes a second sees the
press once; a key really held still repeats. The test failed before the
change and passes after; all PC tests pass. On the board it goes with the next
flash of main (the PR 12 merge).

## What is left

Nothing.
