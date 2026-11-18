---
title: "Submenus hand over at once"
date: 2026-11-17
by: owner
status: decided
area: desktop
summary: "Another item of a parent menu takes over from an open submenu at once"
---

# Submenus hand over at once

Asked by the owner: the 300 ms rest before another item of a parent menu
took over from an open submenu made the Start menu feel sluggish. Now, in
every cascading menu and at every depth:

- **Another item of a parent menu takes over at once**: the open submenu
  (and any below it) closes, the item is highlighted and its own submenu, if
  it has one, opens in the same move. No wait.
- **The grace stays**: the parent's borders and separators, and the space off
  the menus, change nothing, so an inaccurate pointer can come back.
- **One exception, chosen by the owner (2026-11-17, over "always at once")**: a pointer on its way *to* the
  open submenu still crosses other parent items on a slant (his report of
  2026-11-16). While it heads for the submenu (its position four moves ago,
  the new one, and the two near corners of the submenu taken 8 px wide make a
  triangle the new position is in), the item it crosses takes over only if
  the pointer rests on it for 300 ms, as before. Coming back from the
  submenu, or moving up and down the parent, is never "on its way".
- A submenu still opens after a 0.3 s rest on its entry when no other
  submenu of that menu is open (unchanged).

test_submenu_switches_at_once and test_slow_submenu in `host/tests/test_wm.cpp`.
