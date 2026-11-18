# One File manager, also for the Trash

2026-11-17. Backlog item 52 (it was 47 until the merge of 2026-11-18, when the window themes had taken that number).

## What was asked

The owner:

> Al abrir la papelera de reciclaje, el entorno de la ventana, el explorador,
> es muy similar al explorador de ficheros, pero no parece ser el mismo. El
> motivo es que, por ejemplo, abajo a la derecha, el explorador de ficheros te
> dice la memoria disponible en megas y el otro la mide en kilobytes. Y
> sospecho que son dos aplicaciones muy similares, posiblemente comparten
> mucho código, pero no son la misma. Me pregunto si podría tener sentido que
> fuera solo la aplicación de explorador la que gestiona ambas vistas y que
> condicionalmente cuando está mostrando el contenido de la carpeta oculta
> trash, pues tiene los diálogos de restaurar fichero y de vaciar papelera
> [...] si el día de mañana añadimos un formato de lista, además del de
> cuadrícula que tenemos ahora, pues ambas vistas [...] se verían igual.

## What was found

He was right. `core/src/shell/file_manager.cpp` held two window classes:

- `FolderWindow`, about 440 lines: Back and Up, the address bar, the icon
  view, the status bar, File and View menus.
- `TrashWindow`, about 200 lines: an icon view and a status bar written a
  second time, a toolbar with only Empty Trash, a File menu of its own.

They were one process ("File manager" in the Task Manager) and shared the
small pieces (the toolbar button, the sunken panel, the folder listing), but
each laid out, painted, refreshed and filled its status bar by itself. The
difference he saw is in those two copies: the folder window writes "MB free"
from 10 MB up and "KB free" below, the Trash window always wrote "KB free".
The other differences nobody had asked for: the Trash had no View menu, no
F5, no address bar, and a folder in it could not be opened.

## The options

1. **Make the two say the same** (share the free-space text). Ten lines, and
   the next difference appears with the next feature.
2. **A common base class** for the two windows. Shares the view, keeps two
   windows, two menus and two layouts to keep alike by hand.
3. **One window; the Trash is a folder it shows** (what the owner proposed).
   Where the window is decides what it offers. The Trash stops being a kind
   of window and becomes a place.

## What was chosen

Option 3, the owner's. It is the only one where a list view added later
cannot exist in one and not in the other, because there is no other.

Smaller choices were the agent's and are in `docs/DECISIONS.md` to be
confirmed: the Trash keeps Back, Up and the address bar (Up goes to `C:\`);
its status bar still says where the chosen entry came from; a folder in the
Trash opens; the Trash is one of the three folder windows.

## What was built

- `TrashWindow` is gone. `FolderWindow` asks `at_trash()` (it shows
  `C:\.trash` itself) in `refresh()`, and there: the title is "Trash", the
  icon is the bin, the File menu is the Trash's table (Open, Restore, Delete,
  Empty Trash, Refresh, Close), the Empty Trash button is placed after Up
  (elsewhere it has an empty place, which hides it), hidden names are listed.
- `open_trash()` is `open_folder("C:\\.trash")`.
- Inside the Trash (also in its folders) New Folder, Rename and F2 do
  nothing, and a file dropped there goes to the Trash, not into the folder
  under the pointer.
- A window's icon could not change after it was made. New call
  `os::ui::set_icon` (`wm::Window::set_icon`, `app::Window::set_icon`).
- `file_manager.cpp` went from 911 lines to about 800.

Tested on the PC only: `test_shell` has a new `test_trash_view` (the same
window going into the Trash, out with Up and back with Back, its title and
icon each time; a folder in the Trash opened; F2 and New Folder refused;
Restore; Empty Trash with the window left empty). The picture tests draw
the new Trash (`37g`, `37i`, `37m`, `37n`); the three Task Manager pictures
change by one digit, the File manager's memory (20K to 21K: the window
carries the second menu table and a longer message text). All 43 tests
pass. The firmware was not compiled and nothing has run on the board.

## What was learned

In the PC pictures the Trash still says "KB free": the test disk is under
10 MB, where the folder window says KB too. On the board's C: both now say
MB.

## What is left

A board turn: open the Trash from the desktop, restore, empty, walk into it
from `C:\` with hidden files shown, and `tools/dbg.py misses`.

## Later the same day: no Back and no Up in the Trash

The board turn passed (PR 47 at `a0e241b`: every check, one File manager
process, memory back on close, no missed refills). The owner then looked at
the choices and changed one:

> La papelera debe ocultar atrás y arriba, o al menos desactivarlas.

The first version kept Back and Up in the Trash, with Up leading to `C:\`.
Hidden was chosen over greyed out: two buttons that can never be pressed
there are only noise, and Empty Trash goes back to the left edge where it
was in the old window. Backspace and Alt+Left do nothing in the Trash
either. Inside a folder of the Trash the two buttons are there again, since
there has to be a way back up; Up from it ends in the Trash.

A window walked into `C:\.trash` from `C:\` therefore cannot walk out
again; it is closed, like the Trash opened from the desktop.

Tested on the PC only (43 of 43; `test_trash_view` and the Trash pictures
changed to match). This second commit has not been compiled for the board
nor run on it.

## 2026-11-18: main merged in

The board passed the second commit too (`3e84ce9`, no missed refills). By
the time the owner asked for the merge, `main` had moved under it: folder
windows are no longer three places in a table but a list with no limit, the
File manager opens on a task of its own (`in_program`), and the window
themes had taken backlog number 47. `main` was merged into the branch:
`open_trash()` stays one line, `open_folder(kTrashFolder)`, now on top of
the new `open_folder`; this item is 52; "the Trash takes one of the three
windows" no longer means anything and was taken out of the decisions.

The merge turned up a fault that was already on `main`. The new list of
folder windows had a way out (the destructor unlinks the window) and no way
in: nothing ever put a window on it. So on `main` no folder window was
found again: a change to the files did not refresh an open folder, and
opening a folder already shown made a second window. Nothing tested either,
and the old Trash window hid it for the Trash, because it was kept apart in
a variable of its own. With the Trash a folder window, `test_trash_view`
failed at once (two Trash windows) and picture `37n` showed the emptied
Trash still holding its file. The constructor now joins the list.

PC tests: 44 of 44. The two Task Manager pictures were redrawn (the File
manager's memory figure). PC only; the merge has not been compiled for the
board nor run on it.

## On the board (2026-11-18, bfcd740)

App-flashed on the BIOS layout, no wipe. Passed: the desktop's Trash icon
opens a window titled "Trash" with the bin icon, a toolbar with only Empty
Trash and the free space in MB, and a second double-click brings the same
window forward. With the Trash open, deleting `del9.txt` in `C:\Documents`
shows it in the Trash at once, without F5; File > Restore takes it out of the
Trash and puts it back in Documents at once in both windows; Empty Trash with
Yes empties the Trash at once and greys the button, and the desktop icon
shows the empty bin. `C:\Documents` opened twice gives one window brought
forward; `pictures` opens a second. Folders of 80 and 200 files open in 1.2 s
and 2.5 s. The Task Manager shows one File manager process (62K) with a folder
window, the Trash and a third window open; closing them all returns internal
memory to the figure before (203,731 bytes) and external memory to within
0.2 KB. In Midnight the Trash's toolbar and status bar read well (the greyed
Empty Trash stays dim but legible). `dbg.py misses`: none.
