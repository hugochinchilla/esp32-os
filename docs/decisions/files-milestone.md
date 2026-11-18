---
title: "Chosen in the files milestone"
date: 2026-11-16
by: agent
status: to-confirm
area: programs
summary: "Clock format, key and caret timings, modal dialogs, Latin-1 text and file names, File manager and Editor limits"
---

# Chosen in the files milestone

Heading in the single-file log: "Chosen in the files milestone (to confirm)".


- The taskbar clock is 12-hour ("12:46 PM"), as in the target picture. It
  counts from 12:00 AM at start-up until set (`tools/dbg.py time set`).
- Timings: caret blinks every 0.53 s; held keys repeat after 0.5 s, then 30
  times a second; held scroll arrows after 0.4 s, then 20 a second; a
  submenu opens after resting 0.3 s on its entry.
- Modal: a message box is modal to the window that was in use when it
  opened; Shut Down holds the whole desktop. Dialogs that belong to a window
  have no taskbar button; the owner's button shows pressed instead.
- Text files are Latin-1, as 98 wrote them; file and folder names are kept
  in UTF-8. Files read with CRLF line ends are written back with CRLF; new
  files get CRLF too.
- Names: up to 63 characters, compared without regard to case, none of
  \ / : * ? " < > |, no space or dot at the end.
- The RAM disk is 2 MB of external RAM, labelled "RAM disk", with at most
  256 files and folders. Its readme.txt says that it is lost at restart.
- The File manager opens folders in the same window (Back and Up), at most
  three File manager windows. Back has a blue arrow pointing left and Up a
  folder with a green arrow rising from it (they shared the folder icon
  until 2026-11-16). My Board lists the drives. Deleting a folder
  deletes everything in it, after a question that says so. New Folder and
  Rename ask for the name in a small dialog (98 edits the name in place).
  Icons per extension: text (.txt .log .ini .md), picture (.png .jpg .bmp
  .gif), other files a blank page. Only text files open (in the Editor);
  others say there is no program yet. (Since the image viewer milestone:
  .bmp .png .jpg .jpeg open in the Picture viewer.)
- The Editor is one window (98 Notepad opens one per file). Files up to
  512 KB; Undo is one step that a second Undo redoes, as in Notepad. The
  title shows a * after the name while there are unsaved changes. No
  Ctrl+S (Notepad 98 had none); Save is in the File menu. No word wrap.
  Menus show no shortcut keys yet.
- The text field and the list box do not use the clipboard yet; the Editor
  does (Ctrl+X, C, V, Shift+Delete).
