---
title: "No splash screen"
date: 2026-11-19
by: owner
status: decided
area: boot
summary: "The OS's splash screen is removed with its timer, its setting and its fonts; the desktop fades in after the BIOS's screen (backlog 74)"
---

The owner, 2026-11-19, on seeing the previews of the start-up fade, which
began on the splash: "fuera el splash, no aporta nada, elimina todo, el
timer, la config, etc. No tendremos splash, un fundido de escritorio desde
la pantalla de bios es suficiente." The story is in
`docs/journal/2026-11-19-no-splash.md`.

- Removed: `shell::show_splash`, `splash_ms` in `C:\.config\boot.ini` (a
  file that still has the key is read as before and the key ignored), the
  Spleen 32x64 and 12x24 letters only it used, screen 1 of the debug
  link's Boot screen request (`dbg.py boot splash`), its picture test and
  `00b-boot-splash.png`.
- Kept: the BIOS's own first screen (`bios::run_splash`, the one that waits
  for the monitor: `bios-first-screen.md`), which is what the owner means by
  the BIOS's screen; the OS's self-test screen for a board with no BIOS;
  the approved drawings in `docs/look/boot/` as the record.
- Start-up is now: the BIOS's screens, the restart into the OS, the
  desktop's plain colour, the desktop fading in (`start-up-fade.md`). The
  owner expects that the monitor, which loses the signal at that restart,
  may show little or nothing of the fade, and does not mind.
