# Copy, Cut, Paste and Delete for files

2026-11-18. Backlog item 54. Built and tested on the PC. The firmware compiles
and links with this change (ESP-IDF v5.5 in the cloud container, no
warnings). Nothing here has run on the board.

## 1. What was asked

The owner, in a list of nine requests for the backlog
(`2026-11-18-backlog-files-sounds-shutdown.md` has all of it):

> * right click  on file should have dialog, copy, cut, delete
> * right click on folder, the explorer blank space or desktop shows paste as an option
> * explorer menu needs entries for copy, paste, cut and delete too for selected items

("dialog" is the menu that opens under the pointer.) The next request in the
same list is selecting several files at once, item 55, which another thread
started the same hour on top of this branch. So the commands here were to be
written for a list of files from the start, although the File manager can
only pick one today.

## 2. What was found

- The File manager (`core/src/shell/file_manager.cpp`) had File > Delete and
  the Delete key, for the one entry picked, through `delete_asking(path)` in
  `trash.cpp`. Files moved only by drag and drop (`move_to_folder`), on the
  same drive, and a name taken at the target was an error.
- Nothing copied a file but the command shell's `cp` (`core/src/sh/
  files.cpp`), which works with a shell session's buffers and cannot be
  called from a window.
- The clipboard (`wm::clipboard`, in `core/src/wm/text_edit.cpp`) was 64 KB
  of text, given once at start-up, used by the Editor.
- A right click reached a program only in a picture that asked for the
  mouse (`os::ui::What::RightClick`); the icon view, which is what a folder
  window shows, did not get it. A program could open a menu at a point of
  the screen (`os::open_menu`) but had no call that tells it where its
  window is, so it could not open one where the pointer was.
- The desktop's icons (`core/src/desk/icons.cpp`) answered a right click
  only away from the icons, with the menu that has Snap, Realign and
  Display.
- The question a Delete asks kept the path in one fixed buffer, and each
  file sent to the Trash read and wrote the Trash's index
  (`C:\.config\trash.ini`) once.

## 3. The options considered

**Where the copied files are remembered.**

- *In the clipboard the text uses* (chosen): it gets a kind (text, files,
  cut files), and the files are their full paths one after another. No new
  memory, and one clipboard, as on the systems this one looks like: copying
  a file replaces the copied text and the other way round.
- *A list of its own in the File manager*: the desktop is another process
  and would not see it, and a closed File manager keeps no memory (the
  owner's rule of 2026-11-17).
- *A second system clipboard for files*: another block held for good, and
  two things called "the clipboard".

**Where the copy is done.**

- *On the UI task under the hourglass*: simplest, but a folder of a few
  hundred KB takes seconds on the flash, the whole desktop stands still
  meanwhile, and the watchdog restarts the machine after 15 s of that.
- *A background job* (chosen): the way the Editor saves. The job belongs to
  whoever asked for the Paste and works only with a block made for it: the
  paths, the target and a 2 KB piece of file. Each file is written the safe
  way (`begin_save`): it gets its name when it is whole.

**A name that is taken where the files are pasted.**

- *Number it* (chosen): "notes (2).txt", as the Trash and the move of the
  old `C:\docs` do. Nothing is ever replaced, and no question interrupts a
  paste of many files half way.
- *Ask whether to replace*: what Windows did. A question per clash, from a
  job, and a way to lose a file with one key.
- *Refuse*, as drag and drop does today: then a file could not be copied
  into its own folder, which is the quickest way to duplicate one.

**Cut.** Nothing happens to the files until they are pasted; then they are
moved. Windows drew the icons of cut files paler: not done, the icon view
has no such state, and it is item 55's widget this week.

**Where the commands go in the menu bar.** A new Edit menu with Cut, Copy
and Paste, Delete staying in File (built), or all of them in File. The
owner was asked; the entry is updated if he prefers File.

## 4. What was chosen, and what was built

The list of small choices is in `docs/DECISIONS.md`, "Chosen for Copy, Cut
and Paste of files". The pieces:

- `wm::clipboard` holds text or files: `Kind`, `begin_files`, `add_file`,
  `file_count`, `file`, `clear`. `size()` and `data()` are of the text, so
  the Editor's Paste does nothing while files are there.
- New OS calls, in `docs/os-calls.md`: `os::clipboard::set_files`,
  `file_count`, `file`, `files_cut`, `clear`; `os::ui::menu_at` (a menu at
  a point of a control); and the icon view now tells of a right click.
- `core/src/shell/file_ops.cpp`, a new file that uses only the OS calls:
  `copy_files(selection, cut)`, `can_paste(folder)` and
  `paste_files(folder, window)`. Paste copies what the clipboard names into
  a block of its own and starts the job; the window that asked shows the
  hourglass on demand (item 49) and stays in use. The job stops at the
  first failure, and the message says which file and why. A folder is not
  put inside itself; folders more than 12 deep are refused, as when they
  are deleted.
- `shell::Selection` (`internal.hpp`): a folder and a list of names. Copy,
  Cut and Delete take one.
- `trash.cpp`: `delete_asking(selection)` asks once ("these 3 items") and
  keeps what it asked about in a block that goes back with the answer; the
  old `delete_asking(path)` is the same thing for one. The index of the
  Trash is read and written once for the whole list.
- The File manager: the Edit menu, Ctrl+X, Ctrl+C and Ctrl+V, and the
  right-click menu, filled in each time for what is under the pointer.
  Everything that acts on "what is selected" goes through one function,
  `FolderWindow::for_selected`, agreed with the thread of item 55, which
  will make it give several.
- The desktop: `desk::Icons` selects the icon under a right click and tells
  the shell which it was; the shell opens the icon's menu, or the desktop's
  own, which now starts with Paste (into `C:\Documents\Desktop`).

Pictures, among the reference pictures the tests compare
(`host/tests/pictures/`): `70-file-right-click`, `71-folder-right-click`,
`72-folder-blank-right-click`, `73-folder-edit-menu`, `74-desktop-paste`,
`75-desktop-icon-right-click`, `76-desktop-documents-right-click`.

## 5. How it was tested

On the PC only: 50 of 50 tests pass, and `tools/check_os_calls.py` says 23
of 23 programs keep to the OS calls.

- `host/tests/test_shell.cpp`, `test_copy_cut_paste`: a file of 3,000 bytes
  and a folder with a folder in it copied, pasted twice (numbered the second
  time), pasted where they came from; cut and pasted (moved once, clipboard
  empty after); a folder into itself, copied and cut (refused, with the
  message); a file gone since it was copied; a disk that fails after the
  first write (no part of a file left); text taking the files off the
  clipboard; Delete of three (one question, all in the Trash with where
  they came from) and of two inside the Trash (for good); every block given
  back. Then with keys and the mouse: Ctrl+C, Ctrl+V, Ctrl+X, Edit > Paste,
  and each right-click menu with what it offers and what is greyed, in a
  folder, in the Trash and in My Board.
- `host/tests/test_job.cpp`, `test_paste`: with real threads. While the job
  is held back the window is not locked, the pointer over it is the
  hourglass and the copy is not there; then it is, and the arrow is back.
  A File manager closed while a paste runs goes only when the copy is
  whole, and leaves no memory behind.
- `host/tests/render_scenes.cpp`: the seven pictures above, with the
  desktop's Paste and Delete done through the menus.

Not tested: anything on the board. What the board would tell that the PC
cannot is how long a paste of a real folder takes on the flash, whether the
job's 10 KB stack is enough under LittleFS (the Editor's save has 8 KB), and
whether the picture stays still while it writes (`tools/dbg.py misses`).

## 6. What was learned

- A job needs a process to belong to. The first test called
  `paste_files` from outside any program and every Paste answered "too much
  is going on": `Jobs::start` refuses when nobody is asking. In the machine
  the asker is always the File manager or the taskbar.
- 39 reference pictures changed that had nothing to do with
  menus. Two reasons, both wanted: every File manager window has "Edit" in
  its menu bar now, and a right click on the bare desktop takes the
  selection off the icon that had it, as a left click always did.
- The numbering stacks: a copy of "one (2).txt" beside itself is
  "one (2) (2).txt". That is what the Trash does too; left alone.

## 7. What is left

- Item 55 (several files at once) changes `for_selected` and nothing else
  here.
- A check on the board, in the queue.
- Not done, to ask for if wanted: cut files drawn paler; Paste on the
  desktop putting the icon where the pointer was; Rename in the right-click
  menu; a way to stop a long paste; progress shown while it runs.
