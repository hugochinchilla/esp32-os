---
title: "File system on the flash"
date: 2026-11-16
by: owner
status: decided
area: storage
summary: "LittleFS as C:, names ignore case, code runs from PSRAM so that flash writes do not shake the picture"
---

# File system on the flash

Decided by the owner: LittleFS (chosen over FAT), as C:, so files stay across
restarts. The joltwallet/littlefs component (pinned to 1.23.1) behind the same
storage port; a new or unreadable drive is formatted and gets the sample files.
Names ignore case on it too, as on the RAM disk (the drive code looks a name
up without regard to case when it does not exist as written). Code and
constants run from PSRAM (CONFIG_SPIRAM_XIP_FROM_PSRAM), because writing the
flash otherwise turns the cache off and the picture shakes (see Found on the
bench).
