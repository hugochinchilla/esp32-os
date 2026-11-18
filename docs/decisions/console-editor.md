---
title: "Chosen for the console editor"
date: 2026-11-17
by: agent
status: to-confirm
area: console
summary: "`ted`, a full-screen editor with no modes, for sessions without a desktop (backlog 22)"
---

# Chosen for the console editor

Heading in the single-file log: "Chosen for the console editor (2026-11-17, to confirm)".


Backlog item 22. Journal: `docs/journal/2026-11-17-console-editor.md`.
Chosen by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **`ted`: a full-screen editor with no modes.** `edit` opens the desktop's
  Editor where there is a desktop and `ted` where there is none. Rejected:
  a line editor; modes as in vi; the name of an existing editor.
- **Keys**: Ctrl+S (or Ctrl+O) saves, Ctrl+Q (or Ctrl+X) leaves and asks
  first if there are changes, Ctrl+F and Ctrl+G find, Ctrl+K cuts the line
  out. Ctrl+C does not close it. No undo, copy or replace yet.
- **It sends only the rows that changed**, with three escape sequences
  (erase the screen, place the cursor, erase to the end of the line).
- **The text is one block of 128 KB** from the Memory port, taken while ted
  is open and counted as the Terminal's; that is the largest file it opens.
- In the Terminal window, Page Up and Page Down go to a program that has
  taken the keys (ted), and to the history otherwise.
