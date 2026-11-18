---
title: "Decided: hardware"
date: 2026-11-18
by: owner
status: decided
area: hardware
summary: "The board, the adapter boards and their pin map, the supply capacitor, USB, the clock chip, nothing is bought"
---

# Decided: hardware

Rows of the "Decided" table of the single-file log. Topics: Board, Adapter board, Two boards, one pin map, Supply capacitor, VGA wiring, Blue's third pin, Video pins, Keyboard and mouse, Two USB devices, Clock, No purchased modules, Piezo beeper's job.

| Topic | Decision | Why |
| --- | --- | --- |
| Board | ESP32-S3-N16R8 (16 MB flash, 8 MB octal PSRAM), two USB-C sockets, kept plugged into a computer for flashing and testing | The board on hand |
| Adapter board | One 5x7 cm perfboard carrier holds the VGA resistors, the audio output filter and the beeper |  It all fits beside the dev board |
| Two boards, one pin map | The hand-built perfboard board and the manufactured hat are separate designs with separate documents (the owner, 2026-11-18): `docs/hardware/` is the perfboard, `hardware/hat/` the hat. Both keep the same pin map so that one firmware serves both. What is tried on the perfboard feeds the hat (`docs/hardware/perfboard-build.md`) | Owner's choice |
| Supply capacitor | An electrolytic capacitor across 3.3 V next to the dev board is a permanent part of both boards (the owner, 2026-11-18): 100 to 470 µF on the hand-built board (470 µF fitted), and one on the hat's 3.3 V, whose value is recorded with the hat in `hardware/hat/` | On the hand-built board it removed the lines seen at high Wi-Fi transmit power, with the colour returns still open: the radio's bursts were dipping the 3.3 V supply that the video voltage is divided from. One look by eye on one board (`docs/hardware/perfboard-build.md`) |
| VGA wiring | Resistor DAC on a 5x7 cm perfboard carrier: 510, 1k, 2k ohm per colour, 100 ohm on each sync line, 1/4 W | Simple to build; every signal is on the left header next to its resistor |
| Blue's third pin | GPIO8 repeats blue's top bit | Keeps white and four greys neutral with only two blue bits in the byte |
| Video pins | Red 4, 5, 6. Green 7, 15, 16. Blue 17, 18, 8. HSYNC 9. VSYNC 10 | See `docs/hardware/README.md` |
| Keyboard and mouse | USB host on GPIO19 and GPIO20, later. Until then, input is injected over serial or the network | No USB breakout boards on hand yet |
| Two USB devices | Through a hub (external, or a hub module on the board), or one receiver shared by keyboard and mouse | The chip has a single USB port |
| Clock | NTP when online. A battery-backed clock chip (I2C on GPIO1, GPIO2) only if it can be built from parts already at home | The chip has no battery input for its own clock; no new purchases |
| No purchased modules | Nothing is bought for this project. Extras are built from parts on hand or not at all | Owner's choice |
| Piezo beeper's job | The beeper on GPIO14 is used only for the modem. System sounds go to the audio jack only (the owner, 2026-11-16) | Owner's choice |
