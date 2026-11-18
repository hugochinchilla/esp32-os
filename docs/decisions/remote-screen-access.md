---
title: "Remote screen access"
date: 2026-11-16
by: owner
status: decided
area: network
summary: "VNC (5900) and the web page (80) run whenever Wi-Fi is on, with no password"
---

# Remote screen access

Decided by the owner: as built. The VNC server (port 5900) and the browser
page (port 80) start whenever Wi-Fi is on, ask for no password, and give
full mouse and keyboard control of the board to anything on the local
network that can reach it (nothing from the internet unless the router
forwards those ports). An off-by-default switch in C:\.config\remote.ini
and a password were offered and can be revisited.
