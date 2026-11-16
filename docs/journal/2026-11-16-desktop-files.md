# Files on the desktop, and one Documents folder

## What was asked

> Stop opening "About chinchilla" and "Welcome to chinchilla" by default, make
> the welcome to chinchilla a file in the desktop (desktop shows files stored
> under "Documents/Desktop") named welcome.txt

Asked back, the owner kept the four system icons (My Board, Documents,
Network, Trash) and said a deleted welcome.txt must stay deleted. Later he
decided that the Documents icon opens `C:\Documents` and that the old
`C:\docs` moves into it, so there is one documents folder with Desktop inside.

## What was found

The desktop had a fixed list of four icons, at most 12, in one column. The
sample documents lived in `C:\docs`, and the Documents icon opened that.

## Options considered

- Telling the desktop about every change (hooks in each program that writes
  files) against reading the folder again every 2 s. Reading again was
  chosen: a few directory reads, and files put there any way (a save, the
  File manager, `tools/dbg.py put`) appear.
- For the old folder: leave it, point the icon at it, or move its files.
  The owner chose to move them.

## What was chosen and why

- The desktop shows the files in `C:\Documents\Desktop` after the system
  icons: a picture by type, the name below (cut to 9 characters and ".." past
  11), double-click opens with the program for that type, folders open in
  the File manager. Columns down the left, 5 rows at 640 x 480, folders first
  then names ignoring case, at most 24 icons. Names starting with a dot are
  hidden. These layout details are listed in `DECISIONS.md` as provisional.
- welcome.txt is written once; `C:\.config\desktop.ini` `[welcome]
  created` remembers it.
- Existing drives: at start-up `C:\docs` moves into `C:\Documents` once, a
  clashing name numbered ("notes (2).txt"), nothing overwritten, `C:\docs`
  removed when empty, `[documents] moved` recorded.

## What was built and tested

`core/src/shell/desktop.cpp`, the icon columns in `core/src/wm/wm.cpp`, the
samples in `core/src/fs/fs.cpp`. PC tests: the desktop at start, the move of
an old `C:\docs` with a name clash, every path updated (dialogs, tests,
pictures). On the board: a file copied into the folder appeared within 2 s
and opened in the Editor; on the owner's live drive `C:\docs` held notes.txt
(209 bytes) and español.txt (43 bytes), both arrived in `C:\Documents` byte
for byte the same, and `C:\docs` was gone.

## What was learned

When icons come and go the others move: the selection has to be cleared, or a
different icon looks selected (found on the board, fixed).

## What is left

The provisional layout choices in `DECISIONS.md`.
