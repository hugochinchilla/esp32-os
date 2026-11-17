# Ctrl+Alt+Del, Alt+Tab and Alt+F4 from the browser viewer

2026-11-17. Backlog item 44. Built and tested on the PC; not yet on the board.

## What was asked

The owner, using the board's own page at `http://chinchilla.local/`:

> I can't send alt+tab nor alt+f4 from the vnc, nor ctrl+alt+supr

## What was found

The board is not at fault. `net::Server::key_event` (`core/src/net/rfb.cpp`)
turns the VNC key numbers for Ctrl, Alt, Tab, F4 and Delete into the same key
codes a USB keyboard gives, and the window manager (`Wm::handle_key`,
`core/src/wm/wm.cpp`) acts on them wherever they come from: Ctrl+Alt+Del calls
the shell (the Task Manager), Alt+F4 closes the active window, Alt+Tab brings
the bottom window to the front. Sent by hand through the simulator's VNC
port, all three worked before any change.

The keys never leave the owner's computer. A web page is only told about a
key after the operating system, the desktop and the browser have each had
their turn:

| Keys | Who keeps them | What the page sees |
| --- | --- | --- |
| Alt+Tab | The desktop: it switches windows | Alt going down, then it loses the keyboard. The page lets Alt go on the board when that happens, so nothing sticks |
| Alt+F4 | The desktop: it closes the browser window | Alt going down, then nothing |
| Ctrl+Alt+Del | The operating system (Windows' security screen, the log-out dialog on Linux desktops) | Ctrl and Alt going down |
| The logo key | The desktop (its own menu). The page also ignored it on purpose: a key pressed with it is a shortcut of the PC | Nothing |
| Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Tab | The browser: tabs and windows. A page may not refuse them | Nothing |

Everything else arrives: letters, F1 to F12, Alt+letter for the menus,
Ctrl+C, Escape, Tab alone.

Browsers have one way for a page to ask for those keys, the Keyboard Lock
(`navigator.keyboard.lock()`), and it does not help here:

- It exists in Chromium browsers only (Chrome, Edge); not in Firefox or
  Safari.
- It is given only to pages that came with a certificate (https) or from
  `localhost`. The board serves plain http, so at `http://chinchilla.local/`
  the browser does not even have `navigator.keyboard`. Checked in Chromium:
  `isSecureContext` is false there and `navigator.keyboard` is missing; the
  same page from `127.0.0.1` has both.
- It works only in full screen.
- It never gives Ctrl+Alt+Del, on any system.

## The options

1. **Buttons on the page** that send the keys. Works in every browser, on a
   phone too, needs nothing from the owner's computer. Costs a strip of
   buttons on the page and about 1.5 KB more page in the firmware.
2. **Keyboard Lock in full screen.** The real keys under the fingers, but
   only in Chrome, only after the owner switches on a browser flag for the
   board's address (or the board learns https, which is a certificate
   nobody trusts and a lot of RAM), and still no Ctrl+Alt+Del.
3. **Stand-in keys**, as Windows' Remote Desktop has: Ctrl+Alt+End for
   Ctrl+Alt+Del, Alt+Page Up for Alt+Tab. Nothing to see on the page, but
   they have to be remembered, there is no usual one for Alt+F4, and they
   take those keys away from the programs on the board.
4. **A VNC program instead of the browser** (port 5900 is there): those take
   the whole keyboard in full screen and have a menu for Ctrl+Alt+Del. No
   change to Chinchilla, but it is not the page.

## What was chosen

Option 1 was built, as the one that works everywhere and that the other two
would need beside them anyway (neither gives all three keys). Whether to add
2 or 3 on top is the owner's to say; asked in the thread.

Chosen while building, to confirm:

- Four buttons: Ctrl+Alt+Del, Alt+Tab, Alt+F4 and Start (the logo key, which
  the page cannot have either; Ctrl+Esc does the same on the board but
  Windows keeps that one too).
- Where they go: the picture keeps its size and its place. The buttons take
  the side with more spare room: down the right edge in a wide window, along
  the top in a tall one. In a window with no room at all they lie on the
  top right corner of the picture, faint (40%) until the pointer is on them.
- A button sends its keys down in order and up in reverse, at once. One
  press of Alt+Tab is one step, as one press of the real keys is on the
  board (there is no list of windows to hold Alt over).
- A button never takes the keyboard: typing goes on to the board after a
  click.

## What was built

- `core/src/net/viewer_page.cpp`: the buttons, each with its keys written in
  the page (`data-k`), their placing in `fit()`, and the few lines that send
  them. The board's side did not change.
- `tools/test_vnc_sim.py` (in ctest, `vnc_tool`): reads the buttons and
  their keys out of the page the simulator serves and sends exactly those
  through the WebSocket. Alt+F4 closes one window each time, Start opens
  and closes the Start menu, Ctrl+Alt+Del opens the Task Manager, Alt+Tab
  swaps two windows and swaps them back.
- `tools/test_viewer_page.js` (not in ctest; needs Playwright): the page in
  Chromium against the simulator. Each button sends the bytes expected, the
  board does it (pictures), a key typed afterwards still reaches the board,
  the buttons are in a row in a tall window and faint in a 640x480 one, and
  the Keyboard Lock finding above.

Tested: all 41 PC tests pass; the browser test passes in Chromium
(headless). Not tested: the firmware was not compiled with this change (it
is one string in portable code), and nothing has run on the board. Firefox
and a phone were not tried.

## What was learned

- The complaint read like a board bug and was not one: the first thing
  done was to send the three combinations through the VNC port by hand,
  and they all worked.
- The Keyboard Lock looked like the proper answer until the address bar was
  considered: `http://` rules it out before anything else does.
- The simulator serves the page before its desktop is up and ends a viewer
  that comes that early; the page's "click to connect again" covers it.

## What is left

- The check on the board, from the owner's own browser.
- The owner's answer on options 2 and 3.
- Ctrl+W, Ctrl+T and Ctrl+N still close and open the browser's tabs
  instead of reaching the board. No program on the board uses them today.
