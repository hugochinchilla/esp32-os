---
title: "The Trash is the File manager on C:\\.trash"
date: 2026-11-17
by: owner
status: decided
area: storage
summary: "There is no Trash window: a File manager window that shows C:\\.trash is the Trash"
---

# The Trash is the File manager on C:\.trash

Heading in the single-file log: "The Trash is the File manager on C:\.trash (the owner's request, 2026-11-17)".


Asked for by the owner: one File manager that also shows the Trash, so that
whatever the folder view gains (a list view, say) the Trash gains too. There
is no Trash window of its own any more.

- A folder window that shows `C:\.trash` is the Trash: titled "Trash", with
  the bin as its icon (title bar, taskbar, address bar), an Empty Trash
  button in the toolbar, and Restore and Empty Trash in its File menu
  where New Folder and Rename are elsewhere. Everything else is the folder
  window's: address bar, icon view, status bar, View menu, F5.
- The Trash has no Back and no Up (the owner, 2026-11-17: "hide them, or at
  least disable them"; hidden was chosen): Empty Trash is alone in the
  toolbar, and Backspace and Alt+Left do nothing there. Inside a folder of
  the Trash they are there again, and Up returns to the Trash.
- It is so however the window got there: the desktop's Trash icon, or a
  folder window taken to `C:\.trash` with hidden files shown.

Chosen by the agent, to confirm:

- The free space reads as in every folder window: MB from 10 MB up, KB
  below. (The Trash window always said KB; that was the difference the
  owner saw.)
- With an entry chosen, the status bar of the Trash says where it came from
  ("From C:\Documents"), not its size and date.
- A folder in the Trash opens like any other (it did not open before).
  Inside the Trash nothing is made or renamed, hidden names are always
  shown, and Delete is for good, after asking. Restore and Empty Trash are
  only at the top of the Trash.
- The Trash is a folder window like the others. Opened when it is already
  shown, that window comes forward.
- The Trash shows also when `C:\.trash` does not exist (nothing thrown away
  yet, or just emptied); any other folder that goes away shows My Board.
- New OS call: `os::ui::set_icon` (`app::Window::set_icon`), since a
  window's icon was fixed when it was made.
