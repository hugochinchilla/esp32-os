---
title: "To confirm on the bench"
date: 2026-11-16
by: agent
status: open
area: hardware
summary: "Checks still owed on the real board: picture stability, Wi-Fi, remote screen, finding by name"
---

# To confirm on the bench

- The picture is stable at 640x480 with the framebuffer in PSRAM.
- The colours and the four greys look right in both modes.
- Safe saves and the hourglass (PC only so far): the six checks in section 7
  of `docs/journal/2026-11-16-safe-saves-and-hourglass.md`.
- The picture stays stable with Wi-Fi active and during flash writes.
- Wi-Fi (never run on the board yet): the radio starts and how long it
  takes; a scan lists the networks around; a wrong password is reported as
  such and a right one connects; the board joins by itself after a restart;
  the clock is set from the network; free internal RAM with Wi-Fi up; the
  picture stays stable and the frame rate holds during a scan and while
  connected.
- Remote screen (compiled and linked for the board, never run there): a VNC
  viewer connects to the board's address, and a browser opens
  `http://<address>/`; the picture is right; keys and mouse work; how long a full-screen update takes and how the desktop feels
  meanwhile; the VGA picture stays stable with a viewer connected. (The
  remote screen runs on the services task since item 8b; the UI loop's pace
  no longer limits it.)
- Finding by name (compiled and linked, never run on the board): the log
  says `answers to chinchilla.local (mDNS)`; `ping chinchilla.local` from
  the laptop answers; `http://chinchilla.local/` opens the viewer; the two
  services show in a browse (`avahi-browse -art` or `dns-sd -B _rfb._tcp`);
  free internal RAM with it (`tools/dbg.py memory`); the picture stays
  steady.
