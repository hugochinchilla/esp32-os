---
title: "Decided: video"
date: 2026-11-16
by: owner
status: decided
area: video
summary: "640x480 at 60 Hz, 256 colours, one picture in memory, the pointer mixed in at scanout, dithered colours"
---

# Decided: video

Rows of the "Decided" table of the single-file log. Topics: Resolution, Colour depth, First code, Mouse pointer, Screen buffers, Colours that the board cannot show exactly.

| Topic | Decision | Why |
| --- | --- | --- |
| Resolution | 640x480 at 60 Hz | Basic VGA mode; fits the PSRAM bandwidth with room to spare |
| Colour depth | 256 colours (red 3 bits, green 3, blue 2), with 64 colours (2 bits each) as a fallback | Enough for the look; one byte per pixel sent straight to the pins, no palette step |
| First code | A VGA test program in ESP-IDF (C) showing 64-colour and 256-colour test cards | Proves the wiring before anything else is built on it |
| Mouse pointer | Mixed into each line as it goes to the monitor, never painted into the picture in memory. Screenshots mix it in with the same code, and can leave it out | Moving the mouse costs nothing and touches no picture memory; no saving and restoring what is under the pointer |
| Screen buffers | One picture in memory, no double buffering | Saves 300 KB of PSRAM and the copying between buffers; drawing is fast enough that tearing has not been an issue |
| Colours that the board cannot show exactly | Made from a fixed 4x4 dot pattern of the two nearest colours (ordered dither), per colour channel. Used for the desktop colour, title bars and their fade. Lines one pixel wide always use exact colours | Blue has only four levels, so navy, teal and mid-grey do not exist as plain colours |
