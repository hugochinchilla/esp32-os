---
title: "The File manager's three views"
date: 2026-11-19
by: owner+agent
status: to-confirm
area: programs
summary: "Icons, List and Details from the View menu, one way for every folder window (backlog 56)"
---

# The File manager's three views

Backlog item 56, `docs/journal/2026-11-19-file-manager-views.md`. The owner
asked for a list and a list with details beside the icons, which stay, and a
switcher under the View menu. That much is decided. He was asked how the
List is laid out, with the first of three recommended, and the work went
ahead on it while the answer was pending; everything else below is the
agent's choice and his to overrule.

- **View > Icons, List, Details**, the one in use ticked, over Show hidden
  files. The tick is the menu's ordinary one: menus have no round mark.
- **The List** (asked, pending): small pictures with the names beside them,
  filled down the columns, going on to the right with a scroll bar along the
  bottom, as Windows 98's List. Every column is as wide as the longest name
  asks, and no wider than 220 pixels: a longer name is cut with "...". Not
  recommended: one column that goes on down (it is the Details without the
  details, and shows 8 files in the default window where columns show 16 or more);
  rows filled from left to right (Windows 98's Small Icons: names are read
  across, which is slower to scan than down).
- **The Details**: a line per entry under a row of titles: Name, Size, Type,
  Modified. The size is in bytes with the thousands apart, as the status bar
  and `dir` show it, and empty for a folder. The type is Folder, Text,
  Picture, Program or Web page, by what opens the file, and "BIN file" (the
  extension) or "File" for the rest. The date is the status bar's.
  The widths are fixed, 70, 72 and 110 pixels; the name takes what is left
  and never less than 110. The titles look like buttons and do nothing: no
  sorting by a column and no pulling a column wider. Both are left for the
  owner to ask for.
- **My Board in Details**: Size is what the drive holds, the last column is
  Free. **The Trash in Details**: the same columns as any folder. A column
  for where each thing came from was not added: `trash_origin()` reads the
  Trash's index file once per name.
- **One way for all folder windows**, kept as `mode=icons|list|details` under
  `[view]` in `C:\.config\files.ini`, beside `show_hidden`. Choosing one
  changes every folder window that is open, and the next ones open the same
  way. Not chosen: a view per folder (a file to keep per folder, or an index
  of folders to keep in step with renames and deletes); a view per window
  (gone when the window closes, so every window would open as icons).
- **Picking is the same in all three**: an entry is hit by its picture or
  its name, not by its whole line, so in the Details a rectangle can start
  beside a name. Ctrl+click, Shift+click, the rectangle, Ctrl+A, dragging
  and the right-click menu are the icon view's. What is picked stays picked
  through a change of view and is brought into view.
- **Keys**: in the List, Up and Down go along the column and Left and Right
  to the same line of the next one; in the Details, Up and Down only.
- **The wheel**: one column of the List per notch (it has no other way to
  go), three lines of the Details, one row of icons as before.
- **How it is built**: the icon view of `core/src/os/ui.cpp` shows the three
  ways; it is not three controls. Three OS calls were added:
  `os::ui::set_view_mode`, `set_small_icons` and `set_columns`. The texts of
  the columns are the program's tables, as the names are.
- **What it costs**: a folder window keeps the texts of the three columns
  for its 160 entries, and three tables of pointers to them and one of small
  pictures: 9,920 bytes more per folder window on the board, taken whatever
  view is shown. Not chosen: asking for it only when Details is first shown
  (less memory in the common case, one more thing that can fail when a menu
  entry is chosen).
