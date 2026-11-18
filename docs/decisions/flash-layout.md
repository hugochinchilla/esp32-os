---
title: "Flash layout"
date: 2026-11-16
by: owner
status: decided
area: boot
summary: "The first partition table: one app area and C: above it. Changed since by the update slots and the BIOS partition"
---

# Flash layout

Decided by the owner. The app area is 0x10000-0x400000 (3.94 MB, one app now;
it can later be split into two over-the-air update slots of about 1.9 MB
without moving C:). C: is LittleFS on the 12 MB above it. A larger C: with no
room kept for updates was the alternative.
