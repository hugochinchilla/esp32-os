---
title: "Decided: architecture"
date: 2026-11-16
by: owner
status: decided
area: project
summary: "The goal, C++ on ESP-IDF and FreeRTOS, ports and adapters, the PC build, repository layout, testing, the debug link and tool"
---

# Decided: architecture

Rows of the "Decided" table of the single-file log. Topics: Goal, Language and base, Code structure, PC build, Repository layout, Testing, Networking order, Debug link, Debug protocol, Debug tool.

| Topic | Decision | Why |
| --- | --- | --- |
| Goal | A 90s-style (Windows 95/98 look) desktop OS for the ESP32-S3 | The point of the project |
| Language and base | C++ on ESP-IDF, with FreeRTOS underneath. Our own kernel layer (app model, app scheduling, messages, file system layer) sits on top of it | Every Espressif driver works directly, which carries the least risk for video, USB and Wi-Fi |
| Code structure | Ports and adapters. The core (drawing, window manager, widgets, shell, apps) depends only on interfaces such as display, input, clock, storage, network and debug link. Adapters implement them for the ESP32, for a PC, and in memory for tests. Interfaces are coarse (framebuffer access, changed rectangles), never one call per pixel; the scanout interrupt sits below them | One seam gives host tests, a desktop that runs on a PC, and a small hardware-bound layer. Applies whichever language is chosen |
| PC build | Yes. The core builds and runs on a PC | Follows from the code structure; lets the desktop be developed and tested without the board |
| Repository layout | `core/` portable C++, `firmware/os/` ESP-IDF project with the ESP32 adapters, `host/` PC tests and a simulated board | Follows from the code structure |
| Testing | Three layers: unit tests of the core on the PC, test-first where practical; framebuffer snapshots compared with stored images; a small number of on-device tests through the serial debug protocol. Signal timing is checked on the bench, not by tests | Fast feedback for most of the code, and checks that can run without hardware |
| Networking order | 1. Serial only, for debugging and screen dumps. 2. Wi-Fi, once a basic OS boots. 3. Later: network over the second USB-C socket, and dial-up over serial | Wi-Fi leaves the serial port free for debugging while the system is in use |
| Debug link | UART0 through the USB-C socket marked UART | Leaves the chip's own USB port free for a keyboard and mouse |
| Debug protocol | Framed packets (COBS, CRC-16) on UART0 at 921,600 baud, with 115,200 as fallback. Log text travels inside the protocol on the same port. Described in `docs/debug-protocol.md` | A screenshot takes seconds, not half a minute; logs cannot corrupt a transfer |
| Debug tool | `tools/dbg.py`, in Python | Already installed with ESP-IDF, quick to change, easy for an agent to drive |
