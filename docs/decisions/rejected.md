---
title: "Rejected"
date: 2026-11-16
by: owner
status: decided
area: project
summary: "The table of options turned down and why: colour depths, software USB, Ethernet, own kernel, LVGL, sample files, registry, logins"
---

# Rejected

| Option | Why not |
| --- | --- |
| 32,768 or 65,536 colours (15 or 16 bit) | Overkill for this project. Twice the memory traffic, sits above Espressif's tested PSRAM limit at 640x480, and needs 15 to 16 colour pins |
| RGB565, RGB555 and RGB444 wiring | Follows from the above |
| An "upgradeable" 15-bit board with empty resistor slots | Replaced by the 9-resistor board |
| USB ports made from ordinary GPIO pins in software | Only works with slow devices and its timing would compete with the video signal |
| Powering a keyboard from the dev board's own USB-C socket | That socket cannot supply 5 V to a device |
| Ethernet module (W5500, ENC28J60) | Would have to be bought |
| Writing our own kernel from scratch, without FreeRTOS | Would mean porting or rewriting Wi-Fi support, USB host, display and network code, and the chip cannot isolate apps from each other anyway |
| LVGL for the desktop | No overlapping window manager, and its widgets are built for touch panels |
| The "Lenna" test photo as a sample picture | A copyrighted magazine photograph; the person in it has asked for it to be retired, and journals no longer accept it. It does not go in the repository or the firmware. The owner can still copy any file of his own onto his board with `tools/dbg.py put` |
| "Tom's Diner" as a sample sound | A commercial recording, not ours to distribute. Same as above: not in the repository or the firmware; `tools/dbg.py put` works for his own copies |
| Kodak Lossless True Color Image Suite as sample pictures | Its usual source says only that it is "my understanding" Kodak released it for unrestricted use; no licence statement from Kodak could be found, so its status was not confirmed (2026-11-16). Can be added if a clear statement turns up |
| A central settings registry, as in Windows | The owner does not want one (2026-11-16). Settings are separate plain text files, one per program |
| One FreeRTOS task for every app, now | A rewrite of the window manager and every widget, and 4 to 8 KB of internal RAM per app. Kept as a later step, app by app (2026-11-16) |
| Staying cooperative only (no tasks besides the UI task) | The shell and telnet would have to be hand-sliced state machines and nothing could run in the background (2026-11-16) |
| The serial console switching into the debug protocol by a command | The agents' tool would be locked out whenever the shell is stuck. Both share the wire instead (2026-11-16) |
| Changing the FreeRTOS scheduler for priorities | Not needed: fixed priorities are built in (2026-11-16) |
| Typing while a save runs (the job saves a copy) | Up to 512 KB more memory and more states to get wrong, for saves of a few seconds. The window is locked instead (2026-11-16) |
| Users, logins and passwords (telnet and VNC included) | The owner does not want them: security is not a goal of a recreational OS (2026-11-16) |
| Artificial delays to make start-up look layered | The screen shows what the machine is really doing (2026-11-16) |
