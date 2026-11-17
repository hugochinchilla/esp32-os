# One File manager, also for the Trash

2026-11-17. Backlog item 47.

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
