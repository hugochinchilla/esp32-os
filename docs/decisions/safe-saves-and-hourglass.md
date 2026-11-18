---
title: "Chosen for safe saves and the hourglass"
date: 2026-11-16
by: owner+agent
status: to-confirm
area: storage
summary: "Saves write a hidden temporary file and swap it in; the sand-timer pointer (the owner's pick)"
---

# Chosen for safe saves and the hourglass

Journal: `docs/journal/2026-11-16-safe-saves-and-hourglass.md`.

- Decided by the owner: the busy pointer is drawing A, the sand timer (15x22,
  own art), out of four (`docs/look/hourglass-options.png`). Not chosen: a
  round glass, the arrow with a small glass beside it, a timer with
  chinchilla ears.
- To confirm: the temporary file sits beside the document under a hidden
  name, `.~<name>.tmp` (not a visible `name.tmp`, not one `C:\.tmp` folder).
- To confirm: every whole-file write saves this way, the settings files
  included, not only the Editor.
- To confirm: the hourglass shows for every save and load in the Editor,
  however short, and does not move or animate. A temporary file left by a
  power cut stays hidden until that document is saved again.
- A save needs room for both copies until its last step; a disk too full
  refuses the save and keeps the old document.
- `Volume::rename()` still refuses an existing name (the Rename command);
  `Volume::replace()` is the one that takes a file's place.
