---
title: "Our static objects in external RAM"
date: 2026-11-17
by: owner
status: decided
area: system
summary: "The core's static objects live in external RAM (`extram_bss`); what stays internal"
---

# Our static objects in external RAM

Decided by the owner, on the internal RAM card ("Move our objects to external
RAM"). The core's static objects (windows, dialogs, listings, rectangle
lists, settings buffers) live in external RAM: `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`
and the `extram_bss` scheme for libcore in `core/linker.lf`, so `core/` needs
no ESP-IDF header. In the firmware, the remote-screen server, the debug
protocol, the Wi-Fi manager, network.ini, the web gate, the Wi-Fi scan
records and the start-up timeline are marked `EXT_RAM_BSS_ATTR`. Kept in
internal RAM: the pointer's code and data (the video interrupt composes it),
the paint band (`canvas.cpp`, written and copied on every repaint), the
window manager object (every event and repaint goes through it; with it in
external RAM one refill was missed once in three runs), the bounce buffers,
and everything ESP-IDF places itself. Free internal RAM 97,963 -> 218,799
bytes, largest block 31.7 -> 139 KB; repaints within 1 %; 0 late, 0 missed
refills. New permanent static windows still owe their memory (see "dialogs
as programs").
