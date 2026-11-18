---
title: "Chosen for Copy, Cut and Paste of files"
date: 2026-11-18
by: agent
status: to-confirm
area: programs
summary: "One clipboard for text or files; Copy, Cut and Paste in the File manager and on the desktop (backlog 54)"
---

# Chosen for Copy, Cut and Paste of files

Heading in the single-file log: "Chosen for Copy, Cut and Paste of files (2026-11-18, to confirm)".


Backlog item 54. The owner asked for the commands and where they show; the
rest is the agent's, each easy to change. The reasons and what was not taken
are in `docs/journal/2026-11-18-file-commands.md`.

- **One clipboard.** The clipboard the Editor uses holds text or files,
  never both: copying files takes the text away, and copying text takes the
  files away. Files are kept there as their full paths, with whether they
  were cut. No new memory: it is the 64 KB the system already holds.
- **Paste never replaces.** A name taken in the folder is numbered,
  "notes (2).txt", as the Trash and the old `C:\docs` move do. A copy pasted
  into the folder it came from is such a numbered copy. Not taken: asking
  whether to replace.
- **Cut is a move at Paste.** Until then nothing changes on the disk, and
  the cut files are not shown any differently. On the same drive it is a
  rename; to another drive the files are copied whole and then removed.
  Pasting what was cut into the folder it is in does nothing. After a Paste
  of cut files the clipboard is empty; copied files can be pasted again.
- **Paste is a background job** of the program that asked (the File
  manager, or the taskbar for the desktop): the desktop goes on, the File
  manager's window shows the hourglass on demand and stays in use, and
  closing it does not stop the copy. It stops at the first thing that goes
  wrong and says which file and why. A folder is not put inside itself.
- **The menus.** In the File manager: Edit, between File and View, with
  Cut, Copy and Paste, and File keeps Delete (the owner was asked on
  2026-11-18 whether he would rather have them all in File; built this way
  meanwhile); Ctrl+X, Ctrl+C, Ctrl+V. A right click picks the entry under the
  pointer first. On a file: Open, Cut, Copy, Delete. On a folder the same
  with Paste, which pastes into that folder. On the blank part: Paste, New
  Folder, Refresh. On the desktop: the same for the icons of files and
  folders; Open and Paste on Documents; Open alone on the other system
  icons; Paste, into `C:\Documents\Desktop`, at the top of the desktop's own
  menu. A right click away from the icons takes the selection off, as a left
  click does.
- **The Trash and My Board.** Nothing is cut or copied out of the Trash or
  pasted into it (Restore is its way out; its right-click menu has Open,
  Restore and Delete, and Empty Trash on the blank part). My Board's entries
  are drives: Open only.
- **Delete takes a list** and asks once: by name for one, "these 3 items"
  for several. The index of the Trash is written once however many go.
- **Selections are lists.** The commands take a folder and a list of names
  (`shell::Selection`); the File manager fills it through one function,
  `FolderWindow::for_selected`, which gives one entry today and is the only
  thing item 55 (several at once) has to change.
