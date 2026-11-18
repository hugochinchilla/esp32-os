---
title: "Chosen for the BIOS's text screen"
date: 2026-11-17
by: agent
status: to-confirm
area: boot
summary: "One grid of 80 by 30 cells mirrored to serial as plain text (backlog 46, step 1)"
---

# Chosen for the BIOS's text screen

Heading in the single-file log: "Chosen for the BIOS's text screen (2026-11-17, to confirm)".


Step 1 of backlog item 46, built on the PC only
(`docs/journal/2026-11-17-bios-text-screen.md`).

- One grid of 80 by 30 cells with a cursor: what is printed goes to the
  screen and, as plain text, to serial, so the two cannot differ.
- A line that changes in place (the memory count, the countdown) is sent to
  serial as carriage return, "erase line" and the new text. No colour on
  serial.
- "Detecting keyboard" instead of "Detecting mouse"; the memory figures
  bright; a bottom line with an identity string given by whoever starts the
  BIOS.
- Not shown until they are true: a "Boot device" line and a boot menu key.
- The reference picture `docs/look/bios-text/bios-post.png` is drawn by
  `tools/mockup/bios_text.py` from the font's BDF file; the PC test compares
  the screen with it pixel for pixel.
