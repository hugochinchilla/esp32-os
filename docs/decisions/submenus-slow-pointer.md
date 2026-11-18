---
title: "Submenus with a slow pointer"
date: 2026-11-16
by: owner
status: replaced
area: desktop
summary: "A submenu stayed open while the pointer crossed its parent; the 300 ms rest was removed by \"Submenus hand over at once\""
---

# Submenus with a slow pointer

Found by the owner: moving slowly from Programs into its submenu closed it.
The submenu opens flush against its parent, and the pointer crossing the
parent's border (part of no item) counted as a change of item, which closed
the submenu at once; a slanted path closed it on the next item. Now, while a
submenu is open, borders, separators and other items of its parent change
nothing until the pointer rests on another item for 300 ms (each move starts
the wait again); a click on another item takes over at once. Menus close only
on a click outside, Escape or choosing an item. The same code serves every
cascading menu (Start menu folders, menu bar submenus). test_slow_submenu
moves 2 px every 20 ms, straight and on a slant.
Changed on 2026-11-17: see "Submenus hand over at once" below.
