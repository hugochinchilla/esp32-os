# Submenus that hand over at once

## What was asked

The owner, 2026-11-17:

> the start menu feels sluggish, if I hover into "Programs" and the list
> opens, and I hover into the program list and accidentally get out of bounds
> the list remains open, which is good because I can be innacurate moving the
> mouse and that gives me a chance to come back before it closes.
>
> But there are a few things to improve, If I get out of bounds of the program
> list towards the first menu panel and hover a different item than
> "Programs", for example I go towards the "Settings" item, I want the program
> list to close inmediatly, the settings item to show the hover effect (blue
> backgruond) and the Settings panel to open right at the same moment.
>
> This must be the general behavior of the nested panels on the start menu,
> not just a case for programs.

## What was found

The wait was put there on purpose the day before
([Slow-pointer submenus](2026-11-16-start-menu-submenus.md)). While a submenu
was open, `Wm::on_move` in `core/src/wm/wm.cpp` did nothing when the pointer
reached another item of the parent except start a 300 ms timer
(`kSubmenuDelayUs`), started again by every move; only when the pointer had
rested for 300 ms did `Wm::switch_due` close the submenu, move the highlight
and open the new submenu. So going from the program list back to Settings
showed nothing at all for as long as the hand kept moving, then 0.3 s more.

That wait had one job: a pointer going from Programs to an entry lower in the
program list travels on a slant and crosses Settings on the way. Without
anything, crossing Settings closes the list, which was the owner's report of
2026-11-16.

## The options considered

- **Hand over at once, always.** The least code: delete the timer. But the
  slanted path closes the list again, the bug fixed yesterday.
- **Hand over at once unless the pointer is heading for the submenu.** The
  rule menus have long used (the "safe triangle"): look at where the pointer
  is going. Coming back from the list, or moving up and down the parent, is
  not heading for the list, so the hand-over is immediate; a slant towards
  the list is, so the items crossed do nothing unless the pointer stops on
  one. About 40 lines.
- **A shorter wait** (100 ms, say). Still a wait started again by every
  move, so still nothing while the hand moves: it does not give what was
  asked.

## What was chosen and why

The second, chosen by the agent as the reading that gives the owner what he
asked for without bringing back what he reported the day before; **the owner
has been asked to confirm it** against the first (pure "at once").

The direction is taken from the pointer's position four moves ago, not one:
a mouse sends many small reports and one of them going straight down in the
middle of a slant would otherwise count as a change of mind. The triangle
runs from that older position to the two corners of the submenu's near side,
taken 8 px wide (`kMenuAimSlack`).

Left as it was: the parent's borders, separators and the space off the menus
change nothing (the grace the owner likes); a submenu opens after a 0.3 s
rest on its entry when no other is open; a click on another item takes over
at once.

## What was built and tested

- `Wm::heading_for` (the triangle), `Wm::switch_to` (close, highlight, open:
  shared by the move and by the rest timer), four trailing pointer positions
  kept in `Wm::mouse`, and the changed branch of `Wm::on_move`, all in
  `core/src/wm/wm.cpp`. It is the window manager's menu code, so menu-bar
  submenus behave the same as the Start menu's.
- `test_submenu_switches_at_once` in `host/tests/test_wm.cpp`, three menus
  deep: from the program list sideways onto Settings, the very first report
  on Settings shows Settings highlighted and its panel open, with no time
  passed; a greyed entry and a plain one close the list at once; the parent's
  border and a point off every menu change nothing after 0.6 s; a slant from
  Programs across Settings into the list keeps the list; one level down,
  coming back from Games' panel onto Tools opens Tools' panel; from there
  onto Run, two levels up, closes both. No stale pixels.
- `test_slow_submenu` (yesterday's) still passes; its "resting hands over
  after the delay" part now checks the hand-over with no wait.
- The PC suite: 40 tests pass (`dbg_tool` failed once in the full run and
  passed twice alone, with nothing changed between; it does not touch
  menus). Four of the tool tests need `pyserial`, which the cloud container
  lacked until it was installed.
- **Not compiled for the ESP32 and not run on the board.** The feel is what
  was asked about, and only a hand on the real mouse can judge that.

## What was learned

Yesterday's fix and today's complaint are the same 300 ms seen from two
sides: the wait cannot tell a pointer passing through from a pointer
arriving. Direction can.

## What is left

The owner's answer on the triangle; a turn on the board (backlog item 40).


## On the board (added by the board thread, 2026-11-17)

Branch tip 072f25a (PR 28) with main c976928 merged in. Main by then had the
window server split (cut 2), which reworked `wm::Wm`; the merge was clean but
for the journal index, and the change sits where it should: the pointer
trail in `Wm::mouse`, `heading_for`, and the hand-over in the menu part of
`on_move`. The backlog item was already 40. PC tests 42 of 42, the first
ESP32 build (0x1616d0), flashed by cable, the app only.

1. Start > Programs open, the pointer inside its list (on Defragmenter),
   then one single move onto Settings. The picture 60 ms after that move was
   still the old one in every respect (the Start menu still highlighted
   Programs): the move had not been handled yet. From 65 to 110 ms, 20
   pictures in 5 ms steps: every one shows Programs closed and Settings open,
   and none shows a moment with both lists open or neither. With the old rule
   Programs would have stayed until the pointer had rested 300 ms.
2. A slow slant (26 moves, 40 ms apart) from the Programs entry across
   Settings to Welcome near the bottom of the list: the Programs list stayed
   open all the way (picture while over Settings), and a click on Welcome
   opened it.
3. Out of the list onto the desktop and back into it: the list stayed open.
4. A click on the desktop closes the menu; Ctrl+Esc opens it.
5. `dbg.py misses`: no missed refill not caused by the debug link. VNC took
   a full screen; telnet answered.
