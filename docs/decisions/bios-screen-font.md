---
title: "BIOS screen font"
date: 2026-11-17
by: owner
status: decided
area: boot
summary: "The BIOS screen is true text, 80 by 30 characters in Spleen 8x16"
---

# BIOS screen font

Decided by the owner: the BIOS's screen is a true text screen, 80 by 30 characters of 8x16 on
640x480, in Spleen 8x16 (already in the repo and the firmware), chosen from
five free fonts drawn in `docs/look/bios-text/`. The mascot sits on top as a
picture over cells left empty, so the serial port can mirror the text
exactly. That the text screen replaces the earlier drawing
(`docs/look/boot/boot-b-self-test.png`) is the agent's reading of the font
choice, to be confirmed. Not chosen: Terminus, Unscii (the agent's
recommendation), Tamzen, GNU Unifont.
