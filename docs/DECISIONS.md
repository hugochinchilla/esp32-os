# Decisions

A running record of what this project will and will not do, so that anyone
picking it up (person or agent) does not have to ask again. Newest decisions
are added to the tables; nothing is deleted, only marked as replaced.

Last updated: 2026-11-15.

## Decided

| Topic | Decision | Why |
| --- | --- | --- |
| Goal | A 90s-style (Windows 95/98 look) desktop OS for the ESP32-S3 | The point of the project |
| Board | ESP32-S3-N16R8 (16 MB flash, 8 MB octal PSRAM), two USB-C sockets, kept plugged into a computer for flashing and testing | The board on hand |
| Resolution | 640x480 at 60 Hz | Basic VGA mode; fits the PSRAM bandwidth with room to spare |
| Colour depth | 256 colours (red 3 bits, green 3, blue 2), with 64 colours (2 bits each) as a fallback | Enough for the look; one byte per pixel sent straight to the pins, no palette step |
| VGA wiring | Resistor DAC on a 5x7 cm perfboard carrier: 510, 1k, 2k ohm per colour, 100 ohm on each sync line, 1/4 W | Simple to build; every signal is on the left header next to its resistor |
| Blue's third pin | GPIO8 repeats blue's top bit | Keeps white and four greys neutral with only two blue bits in the byte |
| Video pins | Red 4, 5, 6. Green 7, 15, 16. Blue 17, 18, 8. HSYNC 9. VSYNC 10 | See `docs/hardware/README.md` |
| Debug link | UART0 through the USB-C socket marked UART | Leaves the chip's own USB port free for a keyboard and mouse |
| Keyboard and mouse | USB host on GPIO19 and GPIO20, later. Until then, input is injected over serial or the network | No USB breakout boards on hand yet |
| Two USB devices | Through a hub (external, or a hub module on the board), or one receiver shared by keyboard and mouse | The chip has a single USB port |
| Clock | NTP when online. Optional battery-backed clock chip on I2C (GPIO1, GPIO2) | The chip has no battery input for its own clock |
| First code | A VGA test program in ESP-IDF (C) showing 64-colour and 256-colour test cards | Proves the wiring before anything else is built on it |

## Rejected

| Option | Why not |
| --- | --- |
| 32,768 or 65,536 colours (15 or 16 bit) | Overkill for this project. Twice the memory traffic, sits above Espressif's tested PSRAM limit at 640x480, and needs 15 to 16 colour pins |
| RGB565, RGB555 and RGB444 wiring | Follows from the above |
| An "upgradeable" 15-bit board with empty resistor slots | Replaced by the 9-resistor board |
| USB ports made from ordinary GPIO pins in software | Only works with slow devices and its timing would compete with the video signal |
| Powering a keyboard from the dev board's own USB-C socket | That socket cannot supply 5 V to a device |
| LVGL for the desktop | No overlapping window manager, and its widgets are built for touch panels |

## Still open

| Question | Options | Suggested |
| --- | --- | --- |
| Remote keyboard, mouse and screen for a person | VNC server, web page, debug tool only | VNC server |
| Language and framework for the OS | ESP-IDF with C++, ESP-IDF with C, Rust | ESP-IDF with C++ |
| How apps are delivered | Built into the firmware, or loadable from disk | Built in first, behind an app interface |
| SD card in the first version | Yes, later, never | Later (pins are reserved) |
| How two USB sockets are provided | External hub, hub module on the board, shared receiver | One socket now, space for a hub module |
| A PC build of the desktop for testing | Yes, no | Yes |
| Clock chip | DS3231 module, another I2C chip | DS3231 |
| Look and name | 95 style, 98 style, a mix; name of the OS | Open. All art must be original or freely licensed |
| Pixel clock | 25.175 MHz (fractional divider), 25, 24 or 26.67 MHz | Pick on the bench with the test program |

## To confirm on the bench

- The picture is stable at 640x480 with the framebuffer in PSRAM.
- One LCD data signal can drive two pins (GPIO17 and GPIO8).
- Which pixel clock setting gives the cleanest picture on the monitor in use.
- The picture stays stable with Wi-Fi active and during flash writes.

## Planned order of work

1. Bench test of the VGA wiring (`firmware/vga-test`).
2. Debug protocol and PC tool, so screenshots and injected input work early.
3. Video driver with a cursor merged in at scanout.
4. Window manager and widget toolkit.
5. Desktop shell, file system and file manager.
6. Text editor, then image viewer.
7. Wi-Fi, NTP, remote display.
8. Clock chip, SD card, USB host board.
