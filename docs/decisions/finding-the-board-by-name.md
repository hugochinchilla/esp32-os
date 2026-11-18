---
title: "Chosen for finding the board by name"
date: 2026-11-16
by: agent
status: to-confirm
area: network
summary: "mDNS: the board answers to `chinchilla.local` and announces VNC and HTTP"
---

# Chosen for finding the board by name

Heading in the single-file log: "Chosen for finding the board by name (asked 2026-11-16, to confirm)".


- The owner asked whether the remote screen can be announced over mDNS. It
  can: the board answers to `chinchilla.local` and announces `_rfb._tcp`
  (5900) and `_http._tcp` (80), both named "Chinchilla", so VNC viewers that
  browse the network list it and `http://chinchilla.local/` opens the page.
  Done with Espressif's `mdns` component (1.15.1), in the Wi-Fi adapter.
- The name is fixed in the code for now. Put to the owner: another name, or
  a name he can change (in the Network panel, kept in
  `C:\.config\network.ini`). Two boards on one network would be told apart
  by the component itself, which adds a number to the second.
- Firmware 1.28 MB with it (1.21 MB before).
