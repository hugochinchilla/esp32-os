---
title: "Chosen for updates over Wi-Fi"
date: 2026-11-17
by: agent
status: to-confirm
area: network
summary: "Two app slots of 1.94 MB and `otadata`; how an update arrives and is confirmed (backlog 25)"
---

# Chosen for updates over Wi-Fi

Heading in the single-file log: "Chosen for updates over Wi-Fi (2026-11-17, to confirm)".


Backlog item 25. Journal: `docs/journal/2026-11-17-updates.md`. Chosen by
the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **The app area is now two slots of 1.94 MB** (`ota_0`, `ota_1`) and
  `otadata`, as "Flash layout" foresaw. C: has not moved. The first flash
  with this table is by cable.
- **The PC sends the update**: `tools/ota.py <address>`, port 3232, one line
  of text, the image, one line back. Rejected: the board fetching it; an
  upload page; HTTP.
- **Checked twice** (CRC-32 of what arrived, then ESP-IDF's image check)
  before the next start changes.
- **A new version is on trial for ten seconds**; if it does not last them,
  the bootloader starts the one before at the next reset. Rejected:
  confirming at once; confirming by hand.
- **Off unless `[update] enabled = true` in `C:\.config\network.ini`.**
  No password, by the owner's rule. Rejected: always on.
