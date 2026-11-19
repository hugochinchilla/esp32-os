---
title: "The files of C: are copied when the board is flashed"
date: 2026-11-19
by: owner+agent
status: decided
area: system
summary: "The firmware carries no programs or pictures; tools/flash.py copies them and DOOM1.WAD to C: over the debug link, only what changed (backlog 77)"
---

# The files of C: are copied when the board is flashed

Backlog item 77. [Journal](../journal/2026-11-19-files-copied-when-flashing.md).

- **The firmware does not carry the files of C:** (the owner's request,
  2026-11-19: "sacar los ficheros del firmware y copiarlos a C:/ con el
  script de flasheo"). The programs kept in files (`programs/*`) and the
  sample pictures are copied to the board by the computer that flashes it,
  with `tools/flash.py`. This replaces "carried in the firmware and
  installed" in [Programs loaded from files](programs-loaded-from-files.md),
  which the owner chose on 2026-11-17: `shell::install_programs`,
  `tools/gen_bundle.py` and `C:\Programs\installed.txt` are gone.
- **Doom's game data comes the same way**: `C:\Programs\DOOM1.WAD`
  (shareware 1.9, 4,196,020 bytes) is fetched to the computer once, checked
  by its SHA-256 and copied, when `programs/doom` is in the repository or
  `--wad FILE` is given. It is not in the repository: it may be passed on
  unchanged but is not freely licensed.

- **Copied over the debug link after the firmware is written, only what
  changed** (the owner's choice on a card, 2026-11-19: "Copiar por el
  enlace de depuración"). `C:\system\installed.txt` lists what the tool copied (16 hex
  digits of the SHA-256, the size, the path). A file is sent when it is not
  in the list with that mark, or is not on the board with that size. A file
  in the list that left the set is deleted. Nothing else on C: is touched.
  Not chosen: a LittleFS image written at 0x700000 with esptool (fast, but
  every flash wipes the settings and the documents, and the laptop needs a
  tool to build the image); both, with the image behind `--fresh` (two ways
  to keep working and to test).
The rest is the agent's, to confirm:

- **What the firmware still writes on a new C:** the folders (`Documents`,
  `pictures`, `system`) and the text files, under 1 KB in all
  (`fs::add_first_files`). The tool makes `C:\Programs`. The PC programs, the browser version and the
  tests have nobody to copy files to them and keep the pictures built in
  (`fs::add_samples`).
- **The tool is the only way to flash by cable**: it writes 0x0, 0x8000,
  0xf0000 and 0x100000 and refuses a build that asks for any other
  address. It does not write the BIOS (`tools/flash-bios.sh`).
- **What this costs.** A C: that was wiped has no programs and no pictures
  until the tool runs again (`tools/flash.py --files-only`). An update over
  the network (`tools/ota.py`) brings the firmware alone: the programs on
  C: stay those of the last cable flash, and one that asks for a call the
  new firmware does not have, or the other way round, is refused when it
  is opened. The software store (backlog 78) is where that would be solved.
