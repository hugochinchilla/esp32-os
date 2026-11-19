---
title: "Bluetooth keyboard and mouse"
date: 2026-11-19
by: owner+agent
status: to-confirm
area: system
summary: "BLE HID host with NimBLE: pairs by itself when a keyboard or mouse is missing, no code, a switch in bluetooth.ini (backlog 51)"
---

# Bluetooth keyboard and mouse

Journal: `docs/journal/2026-11-19-bluetooth-keyboard-mouse.md`. Backlog item 51.

Put to the owner as options on 2026-11-19. He chose the first, how a new
device is paired, the same day (pairing by itself when one is missing, and
a window for the rest). The others are built on the recommended option
until he answers.

- **How a new device is paired** (the owner's choice). When no keyboard is connected (or no
  mouse), the board pairs by itself a device of that kind that is in
  pairing mode; a device that was paired is connected again whenever it
  calls. `auto_pair` in `C:\.config\bluetooth.ini` turns the first half
  off. Reason: the board has no USB host yet, so without this the first
  keyboard could only be paired from a PC. Not chosen: pairing only when
  asked (the first device needs the debug link or the remote screen); by
  itself only in the first minutes after power-on (a keyboard bought later
  needs a restart to be found). The cost: somebody else's device in pairing
  mode nearby can be taken; it is forgotten from the Bluetooth program
  or with `dbg.py bt forget`.
- **A switch.** `[bluetooth] enabled` in the same file, on by default, read
  at start-up. Off: the stack is not started and Wi-Fi has the radio to
  itself as before. Measured on the board (2026-11-19): on costs 58 KB of
  internal RAM, off still 21 KB, the controller's code that sits in
  internal RAM whether it runs or not; only a firmware built without
  Bluetooth costs nothing. On: the two take turns at the
  radio. ESP-IDF v5.5 does not ask for Wi-Fi's power saving to be turned on
  for this (older versions did), so it stays off as decided in the Wi-Fi
  milestone; what the sharing costs the remote screen is to be measured.
- **No code at pairing** ("Just Works"), with the keys kept (bonding). Not
  chosen: a six-digit code shown on the screen to type on the keyboard;
  it protects against somebody listening during the pairing, which this
  machine has no use for.
- **The device's own report layout is read**, not the simpler "boot"
  layout: the wheel is only in the first, and many Bluetooth mice have no
  boot mode. The reading is portable code (`core/input/hid.hpp`) with PC
  tests, and a USB host port can use it later.
- **Where things are kept.** The pairing keys in NVS, by NimBLE, like the
  Wi-Fi passwords. Which devices are remembered, with their names and what
  they are, in `C:\.config\bluetooth.ini`, `[devices]`, at most eight; a
  ninth pushes out the one paired longest ago.
- **Three connections** at most: a keyboard, a mouse and one more.
- Bluetooth LE only: the ESP32-S3 has no Bluetooth Classic, so a keyboard
  or mouse that speaks only that will not be found.
- **The Bluetooth program** (Start > Settings > Bluetooth; its own
  process, as every settings tool): the devices connected, remembered and
  in range, Pair, Forget, and the two switches as tick boxes. While it is
  open the radio listens hard, so a device shows at once. `tools/dbg.py
  bt` does the same from a PC.
