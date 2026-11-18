---
title: "One documents folder"
date: 2026-11-16
by: owner
status: decided
area: storage
summary: "Documents opens C:\\Documents, which holds the sample documents and the Desktop folder"
---

# One documents folder

Decided by the owner: the Documents icon opens C:\Documents, which holds the
sample documents (C:\docs before) and the Desktop folder. A drive formatted
before keeps its files: at start-up its C:\docs moves into C:\Documents
once, a clashing name numbered ("notes (2).txt"), nothing overwritten, and
C:\docs removed when empty; `[documents] moved` in C:\.config\desktop.ini
records it. If something cannot be moved it stays in C:\docs and the move is
tried again at the next start.
