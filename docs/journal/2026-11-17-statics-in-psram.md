# Our static objects in external RAM

## What was asked

The owner chose "Move our objects to external RAM" on the internal RAM card
(`2026-11-16-internal-ram.md`): our own static objects used about 113 KB of
internal RAM, and only about 98 KB was free with Wi-Fi on. The task was to
move the large ones that no interrupt or timing-critical path touches,
keeping `core/` free of ESP-IDF headers, and to measure before and after.

## What was found

From the map file, the core's `.bss` (windows, dialogs, listings, the
starfield, rectangle lists, settings buffers) was about 96 KB of internal
RAM; the firmware's own big statics in `main.cpp` and `wifi.cpp` (the
remote-screen server 7.3 KB, the window manager 5.1 KB, the debug protocol
4.1 KB, the Wi-Fi scan records 4.3 KB, the start-up timeline 3.1 KB, the web
gate 2.3 KB, the Wi-Fi manager 1.9 KB, network.ini 2 KB) about 31 KB more.
The video interrupt's own data (pointer position and shapes) was already in
firmware variables marked `DRAM_ATTR`, not in the core. With code running
from PSRAM (`CONFIG_SPIRAM_XIP_FROM_PSRAM`), PSRAM stays readable during
flash writes, so objects there are safe while saving.

## The options considered

- `EXT_RAM_BSS_ATTR` on each object: the core cannot include `esp_attr.h`.
- Allocating the objects from the PSRAM heap at start-up: every static
  window would need a pointer and an allocation; many changes in `core/`.
- The `extram_bss` linker scheme for the whole of libcore, from the
  linker fragment the core already has (`core/linker.lf`), with exceptions
  by object file: no change to any `.cpp` in `core/`. Chosen.
- Making the three static `fs::Ini` buffers (display.cpp, screensaver.cpp,
  boot.cpp) locals, as the icons thread suggested: once the core's statics
  are in external RAM they cost no internal RAM, and as locals they would sit
  on the internal UI stack. Left as they are.

## What was chosen, and what stayed internal

- libcore's `.bss` in external RAM, except `pointer` (the video interrupt's
  compose code, `noflash` as before) and `canvas` (the 12-line paint band,
  7.5 KB, written and copied on every repaint).
- In the firmware, `EXT_RAM_BSS_ATTR` on the remote-screen server, the debug
  protocol, network.ini, the Wi-Fi manager, the web gate, the scan records
  and the start-up timeline.
- The window manager object stays internal. With it in external RAM the
  repaints were about 2 % slower and one refill was missed (483 us late) once
  in three runs; kept internal, 0 in three runs. It is 5.3 KB.
- Untouched: the bounce buffers, the pointer state, task stacks, Wi-Fi's and
  lwIP's own buffers, ESP-IDF's placements.

This sits with the owner's rule of the same day (CLAUDE.md, "dialogs as
programs", `2026-11-17-dialogs-as-programs.md`): new dialogs become programs
and new permanent static windows owe their memory. Moving the existing fixed
objects to external RAM does not pay that debt, it only makes the existing
ones cheaper for internal RAM.

## What was built and how it was tested

`core/linker.lf`, one line in `firmware/os/sdkconfig.defaults`, attributes in
`firmware/os/main/main.cpp` and `wifi.cpp`. Measured on the board with the
same script before and after (a 300 KB test file on the desktop, Wi-Fi
joined, fast starts):

| | main | core statics out | + firmware statics, Wm too | final (Wm internal), 3 runs |
| --- | --- | --- | --- | --- |
| Free internal RAM | 97,963 | 193,555 | 224,247 | 218,799 |
| Heap total, internal | 255,235 | 350,827 | 381,947 | 376,499 |
| Largest free block | 31,744 | 114,688 | 147,456 | 139,264 |
| Frame rate | 59.60 | 59.55 | 59.50 | 59.45 to 59.60 |
| Repaint, empty desktop | 14.74 ms | 14.79 | 15.07 | 14.74 to 14.92 |
| Repaint, maximised Editor (300 KB file) | 44.07 ms | 44.35 | 44.99 | 44.31 to 44.74 |
| Repaint, bricks pattern | 15.47 ms | 15.53 | 15.81 | 15.33 to 15.68 |
| Late / missed refills | 0 / 0 | 0 / 0 | 1 / 1 (once) | 0 / 0 |
| K held 1.5 s | 31 letters | 31 | 31 | 31 |

Unchanged main measured again in the same session: repaints 14.75, 44.5 and
15.46 ms, so the final placement is within 1 % of main. The 300 KB save took
1.0 s on some runs and 3.2 to 3.6 s on others, on unchanged main as well:
that is LittleFS (erasing flash blocks), not the RAM placement. Saves showed
0 late, 0 missed refills. Wi-Fi joined, VNC, the web page and `dbg.py`
answer, and a normal start's self-test passes (the internal memory test now
covers 232K instead of 124K). PC tests 22 of 22 (the PC build ignores the
linker fragment).

## What was learned

The window manager is the one object worth keeping close: everything goes
through it. The rest of the UI's state does not notice where it lives.

## What is left

Nothing for this. Internal RAM free with Wi-Fi on is now about 219 KB.
