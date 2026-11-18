---
title: "Chosen for selecting several files"
date: 2026-11-18
by: owner+agent
status: to-confirm
area: programs
summary: "Ctrl+click and a rubber-band rectangle in the File manager (backlog 55)"
---

# Chosen for selecting several files

Heading in the single-file log: "Chosen for selecting several files (2026-11-18, to confirm)".


Backlog item 55, `docs/journal/2026-11-18-selecting-several.md`. The owner
asked for Ctrl+click and a rectangle. He was asked how far it should go (the
File manager only or the desktop too; with or without Shift+click, Ctrl+A and
dragging the group) with the first of these recommended, and the work went
ahead on it while the answer was pending. He chose the first the same day:
the File manager and the desktop, with Shift+click, Ctrl+A and dragging the
group. The scope is decided; the details below are still to confirm or
overrule:

- In a folder window: a click picks one alone; Ctrl+click adds or takes away
  one; Shift+click picks from the last one clicked to this one; a rectangle
  pulled from an empty place picks what it touches by picture or by name, and
  with Ctrl held adds to what was picked; Ctrl+A picks all; Shift with the
  arrow keys stretches the range. A click on one of several picks it alone
  when the button comes up, so that pressing it can start a drag of them all.
- On the desktop: Ctrl+click and the rectangle. No Shift+click and no Ctrl+A
  there: the icons are placed freely and have no order to take a range from,
  and the desktop has no keyboard focus.
- An entry is hit by its picture or its name, not by the whole cell it is in.
  Before, a click anywhere in the 76 by 66 cell picked the entry, which left
  no empty place between files to start a rectangle from.
- The rectangle is a dotted line, with nothing filled in. On the desktop it
  is white with black dots so that it shows on any background.
- What is picked is dragged together. Targets are asked about the one held
  only. Let go, each is dropped in turn; the folders are read again once,
  after the last, and one box names the first that could not be moved and
  says how many more could not.
- Desktop icons dragged together keep their places among themselves. Files
  dropped on the desktop from a folder: the first where it was let go, the
  others in the nearest free cells of the grid, so that none lands on another.
- An icon that stands for no file (My Board, Network, Trash) picked together
  with files moves with them on the desktop and stays behind when they are
  dropped on a window, a folder or the Trash.
- When a folder is read again and holds as many entries as before, what was
  picked stays; otherwise only the one picked last stays. The entries are
  known by their place in the list, and the places have changed.
- A right click on one of several picked entries keeps them, and its menu has
  what goes for all of them: Cut, Copy and Delete (Restore in the Trash). No
  Open and no Paste into one of them. A right click on an entry that is not
  picked picks it alone. File > Open and Rename are greyed while more than
  one is picked. On the desktop the same, for the files among the icons.
- The status bar says "3 objects selected, 30 bytes" (the files among them
  added up; folders are not looked into).
- The window manager asks the Memory port for the list of the other paths
  when a second file joins a drag (1 KB, doubling) and gives it back when
  the drag ends. Rejected: a fixed table (160 paths of 160 bytes is 25 KB of
  RAM held always) and asking the program that started the drag for each
  path at the drop (the folder may be read again halfway through the drops,
  and its list is then another).
- Not done: the view does not scroll by itself while a rectangle is pulled
  past its edge (the wheel works meanwhile); the first 256 entries of a view
  can be picked (a folder shows 160); the outline that follows the pointer
  is one icon's, however many are carried.
