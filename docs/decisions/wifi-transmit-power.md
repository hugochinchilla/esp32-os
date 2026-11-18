---
title: "Wi-Fi transmit power"
date: 2026-11-18
by: owner
status: decided
area: network
summary: "Default 20 dBm, `tx_power_dbm` in network.ini; a board without the supply capacitor sets 2"
---

# Wi-Fi transmit power

Decided by the owner, 2026-11-18: the radio's transmit power defaults to
the maximum, 20 dBm, adjustable as `tx_power_dbm` in C:\.config\network.ini
(2 to 20). From 2026-11-16 until then the default was the minimum, 2 dBm,
because at full power the transmit bursts put lines on the picture while it
connected (see Found on the bench). A capacitor across 3.3 V cured the
lines (Decided, "Supply capacitor"), and both boards carry it. A board
without the capacitor sets `tx_power_dbm = 2`. The Network program sets it
under "Advanced...": a slider, and OK both writes the file and changes the
radio at once (backlog item 57; the owner's choice, 2026-11-18, over three
presets and over applying it at the next start). Joining goes straight to the remembered
network's channel and access point (kept in NVS with the networks) instead
of scanning every channel first, so the radio transmits less; a full scan
follows only if it is not found there.
