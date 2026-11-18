---
title: "Still open"
date: 2026-11-18
by: owner
status: open
area: project
summary: "The table of open questions with the options and the suggestion for each; some rows are answered in place"
---

# Still open

| Question | Options | Suggested |
| --- | --- | --- |
| Window manager details chosen while building step 4, closest to the 98 look; to confirm or overrule | See the list under "Chosen in step 4" below | As built |
| Remote keyboard, mouse and screen for a person | VNC server, web page, debug tool only | VNC server |
| How apps are delivered | Built into the firmware, or loadable from disk | Decided 2026-11-17: both. The built-in programs stay rows in the program table, and a program can also be a file loaded from a drive ("Programs loaded from files" below, backlog item 27) |
| Shell details chosen while building menus, the Start menu and widgets; to confirm or overrule | See "Chosen in the shell milestone" below | As built |
| A RAM disk as well as C: | Drop it, or keep a small one as a separate temporary drive | Dropped with LittleFS, since nothing needs it; the RAM disk code stays for the PC tests and the simulator |
| Details chosen in the files milestone; to confirm or overrule | See "Chosen in the files milestone" below | As built |
| Details chosen in the image viewer milestone; to confirm or overrule | See "Chosen in the image viewer milestone" below | As built |
| Desktop wallpaper from a picture (Settings > Display) | Yes (a picture shrunk to 640x480 in external RAM, 300 KB, repainted from it), later, never | Later: there is no Settings program yet and the desktop would need a picture store; skipped in this milestone |
| Interlaced PNG and progressive JPEG | Support them (a whole-picture buffer for progressive JPEG: 6 bytes a pixel), keep them refused | Keep refused for now: they say "not supported" in an error box |
| SD card in the first version | Yes, later, never | Later (pins are reserved) |
| How two USB sockets are provided | External hub, hub module on the board, shared receiver | One socket now, space for a hub module |
| A PC build of the desktop for testing | Yes, no | Decided: yes. See Decided |
| How the PC build shows the screen | SDL window, image files only | Open |
| Clock chip | DS3231 module, another I2C chip | DS3231 |
| Details chosen for selecting several files (backlog item 55); to confirm or overrule | See "Chosen for selecting several files" at the end | As built |
| VGA pin 5 on the hand-built board, once the firmware reads the hat's monitor sense on GPIO47 | Wire pin 5 to GPIO47 with a pull-up as the hat; tie GPIO47 to ground; leave the board and have the firmware treat it as unknown | Wire it as the hat, when the firmware reads it (`docs/hardware/perfboard-build.md`) |
