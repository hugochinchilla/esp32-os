# The File manager's List and Details

2026-11-19. Backlog item 56. Built and tested on the PC. Whether the firmware
compiles is said at the end; nothing here has run on the board.

## 1. What was asked

The owner, 2026-11-18, in the list that became backlog items 51 to 60: file
manager views, "a list, and a list with details, beside the current view,
which stays. A mode switcher under the View menu". On 2026-11-19: "actualiza
el backlog primero, despues arranca con 51, 56, y el fallo de dbg_tool."

## 2. What was found

- A folder window (`core/src/shell/file_manager.cpp`) shows what it holds in
  one control, the icon view (`IconView` in `core/src/os/ui.cpp`): a grid of
  32 by 32 pictures, 76 by 66 pixels per entry, with a scroll bar down the
  side. Everything item 55 gave it (several picked at once, the rectangle,
  dragging the group, the right-click menu) lives in that control.
- The View menu had one entry, Show hidden files.
- `Listing` already has what the details need for every entry: size, when it
  was written, and a 16 by 16 picture (`Listing::icon(i, false)`), which the
  file dialogs use. Nothing has to be read from the drive again.
- A file with no picture of its own has a blank sheet at 32 by 32 and the
  text file's picture at 16 by 16: there is no small blank sheet. It shows
  now that both sizes can be seen in the same window (data.bin in the
  pictures). Left as it is.
- The Trash's "where it came from" reads the index file each time it is
  asked (`trash_origin()` in `trash.cpp`), which is why it was not made a
  column.

## 3. The options considered

How the List is laid out, put to the owner on a card:

1. Columns, as Windows 98's List: filled from top to bottom, going on to the
   right, a scroll bar along the bottom. Recommended.
2. One column, going on down: the Details without the details.
3. Rows filled from left to right with a scroll bar down the side, which
   Windows 98 called Small Icons.

How it is built:

- A. One control that shows the entries three ways. The picking, the
  rectangle, the dragging and the keys are written once.
- B. A second control for the lines (or the existing list box, grown), and
  the folder window swapping one for the other. Everything item 55 did would
  be written a second time, or pulled out into something both share.

Where the choice is kept: one for all folder windows, in `files.ini`; one
per folder; one per window.

## 4. What was chosen, by whom, and why

- The List: option 1 was built while the owner's answer was pending. It is
  what the look of the desktop is taken from, it shows the most files in a
  window (16 whole names of the test folder in the default window against 8 for one column),
  and it is clearly not the Details.
- Built as A, the agent's choice: the entry's place is the only thing that
  differs between the views, so `IconView` now asks one function where an
  entry is (`cell()`), and one for where its picture and its name are
  (`parts()`), and the rest of the code does not know which view it is in.
- One view for all folder windows, the agent's choice, to confirm: it is one
  line in `files.ini`, where Show hidden files already is, and nothing has
  to follow folders as they are renamed.
- The rest (the columns, their widths, titles that do nothing, the wheel,
  the keys) is in `docs/decisions/file-manager-views.md`, all to confirm.

## 5. What was built

- `core/src/os/ui.cpp`, `IconView`: a mode (`os::ui::ViewMode`), a second
  scroll bar that lies along the bottom for the List, the small pictures and
  the columns. `first_` is the first row, column or line in view, where it
  was `top_row_`. The rectangle keeps its corners in the whole of what is
  shown, sideways as well now.
- Three OS calls, `os::ui::set_view_mode`, `set_small_icons`, `set_columns`
  (`core/include/core/os/ui.hpp`, `docs/os-calls.md`), and their lines in
  `app::IconView`.
- `file_manager.cpp`: View > Icons, List, Details; the mode read from
  `files.ini` when the first folder window opens and written when it is
  changed; `details()` fills the texts of Size, Type and Modified (for My
  Board: what each drive holds and has free).
- A folder window is 9,920 bytes larger on the board (on the PC, with
  pointers twice the size, 12,480): the texts for 160 entries and four
  tables. Task Manager's memory figure for a folder window moved with it,
  which is why three of its reference pictures changed (21 pixels each, the
  one figure).

Tested on the PC: the 54 tests pass. `host/tests/render_scenes.cpp` has a new
part, sixteen pictures (80 to 89): the menu, the List, the List scrolled to
its end, the Details, all picked, My Board, the Trash, and the List and the
Details in each of the three other themes. Its checks: the mode is kept in
`files.ini`; a click on a name picks it; Down goes along the List's column
(told by which file Delete then throws away); the last entry stays picked
from the List to the Details; a double click on a folder's line opens it; a
click on a size picks nothing; Icons comes back.

Not tested: the rectangle and the wheel in the two new views (the code is
the icons', with the view's own places); a folder with more than fits in the
Details being dragged from; anything on the board.

## 6. What was learned

- The test wrote 2,200 bytes from a 1,234 byte string, and the address
  checker of the PC build stopped it on the first run. It was the test's
  mistake, not the File manager's.
- The Modified column was first 104 pixels wide and showed "2026-11-17 1...":
  sixteen characters are 96 pixels and the column keeps 6 on each side. It
  is 110 now, and the name has 128 in the default window, which cuts "a
  longer name here.txt" to "a longer nam...". A wider default window, or
  columns that can be pulled, would be the cure.

## 7. What is left

- The owner's answer on the List's layout, and on what is marked to
  confirm.
- The board: how long a folder of many files takes to paint as Details
  (every line is a picture and four texts), and that nothing is missed.
- Not asked for and not built: sorting by a column, pulling a column wider,
  a small blank-sheet picture.
