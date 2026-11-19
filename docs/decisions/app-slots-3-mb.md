---
title: "App slots of 3 MB"
date: 2026-11-19
by: owner
status: decided
area: boot
summary: "Two update slots of 3 MB, `otadata` at 0xf0000, the BIOS partition 896 KB, C: 9 MB from 0x700000 (backlog 71)"
---

# App slots of 3 MB

Backlog item 71. Journal: `docs/journal/2026-11-19-app-slots-3-mb.md`. The
owner asked for larger firmware partitions and chose the size from three
options on 2026-11-19. Where `otadata` went is the agent's choice.

- **Each of the two update slots is 3 MB** (`ota_0` at 0x100000, `ota_1` at
  0x400000). They were 1.94 MB; the firmware was 1.76 MB on `main` and
  1.95 MB with Bluetooth (96 % of a slot).
- **C: is 9 MB from 0x700000** (it was 11 MB from 0x500000). It is wiped
  once, because it moves.
- **`otadata` is at 0xf0000**, the last 64 KB of what was the BIOS's
  megabyte, so the slots and C: start on whole megabytes. The `bios`
  partition is 896 KB (it was 960 KB; the BIOS is about 300 KB).
- **`nvs`, `phy_init`, the BIOS and the start of `ota_0` did not move**: the
  saved Wi-Fi networks stay, and the BIOS is not written again.
- Not chosen: 4 MB slots with a 7 MB C: (more than is needed, and C: is
  where loadable programs go); 2.5 MB slots with a 10 MB C: (79 % full with
  Bluetooth, so the change would come again soon); one slot (no way back
  after a bad update, rejected before in "BIOS stage: flash and details").

This replaces the sizes in "BIOS stage: flash and details", "Chosen for the
BIOS firmware" and "Chosen for updates over Wi-Fi"; the rest of those three
stands.
