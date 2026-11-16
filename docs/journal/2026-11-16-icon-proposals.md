# Icon proposals, and the Editor's new icon

2026-11-16, late. PC only; this has not run on the board.

## What was asked

From the same message as [the inventory](2026-11-16-icons.md): "hacemos
propuestas de iconos más refinados para cada uno de ellos. También, en
especial, no me gusta nada el del editor de texto."

## What was found

The Editor's icon was a white page with navy lines, the same picture as a
text file, and also lent to Welcome and to Run. At 16x16 it was a striped
rectangle: nothing in it said "write".

## The options

Drawn by `tools/mockup/icon_proposals.py` (sheets in `docs/look/icons/`), each
at 32x32 and again, by hand, at 16x16, in the board's 256 colours. The 16x16
is not the large one shrunk: it is drawn for its size.

For the Editor, three ideas:

- **A. Notepad and pencil**: a spiral pad with ruled lines and a yellow pencil
  across it.
- **B. Written sheet and pencil**: a page with a navy capital A, lines of
  text, and the pencil.
- **C. Quill and inkwell**: a feather on cream paper beside a navy inkwell.

For every other program, one proposal each: File manager (an open folder with
papers), Picture viewer (a painting on an easel), Task Manager (a window with
a load graph), Defragmenter (blocks settling in a row above a drive), Culebra
(the coiled snake and the apple), Cactus Patch (a cactus in its pot), Network
(two machines and a cable), Screen Saver (a screen with a night sky), Settings
(a panel with three sliders), Welcome (an open book), Widget gallery (a window
with controls), About (the mascot drawn at 32x32 instead of doubled), Shut
Down (a red power key), Run (a window with a prompt), and a cog for processes
with no window. Also a folder with light and shade, a text file and a picture
file.

## What was chosen

The owner chose **A, notepad and pencil**, on a decision card. It was also the
agent's recommendation: it reads best at 16x16, and it cannot be mistaken for
a text file. B's capital A turns to a blob at 16x16. C is the prettiest large
but the feather is a white smear on a grey title bar.

The owner's words on the rest: "Las propuestas me gustan mucho". He first
took the several views of each icon (enlarged, real size, on three
backgrounds) for variations to choose between; the sheet did not say they
were the same icon. Worth a caption next time.

Asked then, on a second card, whether to put the rest into the system, he
chose "Sí, todos".

## What was built

- `tools/mockup/icon_proposals.py --cpp` writes the chosen drawings out as
  C++ (text rows and inks), pasted into `core/src/gfx/art.cpp`. The drawing
  stays the source: change it there and paste again. 18 programs and kinds
  of file at both sizes, and the mascot at 32x32: 37 pictures, about 28 KB
  of flash.
- Every program in the table has its own 16x16 now, and so has its window:
  Editor, File manager (in the Start menu; a folder window still shows the
  folder, since that is what it is showing), Picture viewer, Task Manager,
  Defragmenter, Culebra, Cactus Patch, Welcome, Widget gallery, Network,
  Screen Saver (window and its Settings entry).
- Start menu: Settings has the sliders, Run a prompt, Shut Down the red power
  key. The Shut Down window has the power key too.
- A process with no window shows a cog in the Task Manager.
- Folder, text file and picture file redrawn at both sizes. A text file no
  longer shares its picture with the Editor.
- About, Shut Down and message boxes show the mascot drawn at 32x32 instead
  of the 16x16 one doubled. In About it sits 2 pixels further left: with the
  new face a black pixel of its outline fell exactly under the middle of the
  screen, where a tool test expects "not black" under the pointer's tip.
- Cactus Patch used to keep its icon in its own file; it is in `art.cpp` with
  the others now.
- The 32x32 program icons are not shown anywhere yet (no shortcuts); they are
  in flash for when there are.
- Not touched: My Board, the drive, the Trash, the blank file, the Wi-Fi bars.
- 64 reference pictures rewritten. Looked at: the Programs menu, a folder of
  pictures, the Editor, the Task Manager with all processes.

Tested: the PC tests, 22 of 22. Firmware not compiled for this change, and it
has not run on the board.

## What is left

- Welcome's 16x16 book is the weakest of the small ones.
- Shortcuts on the desktop, which would show the 32x32 ones (not in the
  backlog yet: the owner's call).
