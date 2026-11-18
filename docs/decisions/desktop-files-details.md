---
title: "Chosen for the files on the desktop"
date: 2026-11-16
by: agent
status: to-confirm
area: desktop
summary: "Where file icons go on the desktop, their order, the limit of 20, long and hidden names"
---

# Chosen for the files on the desktop

Heading in the single-file log: "Chosen for the files on the desktop (2026-11-16, to confirm)".


- Icons go in columns down the left, 70 px apart, a new column (76 px to the
  right) when one is full: 5 rows at 640 x 480. Folders first, then files by
  name ignoring case, as in the File manager.
- At most 20 files show (24 icons in all); the rest are not shown.
- Folders in the desktop folder show and open in the File manager.
- Names starting with a dot are hidden. Names longer than 11 characters are
  cut to 9 and "..".
- ~~The folder is read again every 2 s (a few directory reads) instead of
  hooks in every program that writes files.~~ Reversed on 2026-11-18: the
  reading took 35 ms on the board every 2 s. The file system now says which
  folder changed (`change-notices.md`).
