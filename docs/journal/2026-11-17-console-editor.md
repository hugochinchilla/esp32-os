# ted, a text editor for the console

Backlog item 22. Branch `claude/project-thread-8q7ksu`.

## 1. What was asked

The backlog line is "Console text editor". The desktop has an Editor; a
console (the Terminal window today, a serial line or telnet later) had no
way to change a file except `echo text >> file`. The owner's instruction
for this run of work is "ve haciendo, no pares hasta terminar el backlog,
dejo a tu criterio", so the choices are the agent's and are marked "to
confirm" in `docs/DECISIONS.md`.

## 2. What was found

- The TTY has a raw mode made for this ("a console editor, top"): keys
  arrive as bytes, arrows as the usual escape sequences, nothing is echoed.
  Ctrl+C still interrupts in raw mode.
- The Terminal window's grid understands "erase the screen", "cursor to
  row and column" and "erase to the end of the line", and ignores colours.
- A session has 2 KB of working room. A file needs more, and large memory
  (the Memory port) is the UI task's to hand out.
- The Terminal window kept Page Up and Page Down for its own history.
- `edit` already exists at the prompt: it opens the desktop's Editor.

## 3. The options, and what was chosen

**What kind of editor.**

- *Chosen: a full-screen editor with no modes*, in the family of the DOS
  editor and of the small Unix ones: what is typed goes into the text, the
  arrows move, a line at the bottom says what the control keys do.
- Not chosen: *a line editor* (`ed`, `edlin`). Works on anything, even a
  printer; nobody wants to use one.
- Not chosen: *modes, as in vi.* Loved by those who know it, a wall for
  everyone else, and more to build.

**Its name.**

- *Chosen: `ted`* ("text editor"), with `edit` falling back to it where
  there is no desktop to open the Editor on. So `edit notes.txt` does the
  sensible thing on a serial console and in a Terminal window alike.
- Not chosen: the name of an existing editor. It would promise that
  editor's keys.

**The keys.**

- *Chosen: Ctrl+S saves, Ctrl+Q leaves*, as in the desktop's programs;
  Ctrl+O and Ctrl+X do the same, for hands used to other editors. Ctrl+F
  finds, Ctrl+G finds again, Ctrl+K cuts the line out, Ctrl+L redraws.
  Ctrl+A, Ctrl+E, Ctrl+Y and Ctrl+V are Home, End, Page Up and Page Down
  for terminals that keep those keys to themselves.
- Ctrl+C does not close it: in an editor that would throw the text away.
  It says "Ctrl+Q leaves" and goes on.
- Leaving with changes asks: y saves and leaves, n leaves, Esc goes back.
- Left out for now: undo, copy and paste, replace, opening a second file.

**How it draws.**

- *Chosen: it remembers what each row of the terminal shows and sends
  only the rows that changed.* A typed letter costs one line of text, which
  matters on a serial line (a full screen is 2,000 characters, a sixth of a
  second at 115,200 baud). What it remembers is kept after the text, in the
  same block of memory (at first it was in the session's 2 KB, which is 80
  by 25 and no more; changed with telnet, whose windows are bigger).
- Not chosen: *redraw everything at every key.* Simplest; fine in the
  Terminal window, sluggish on a wire.
- Not chosen: *insert and delete line sequences* for scrolling. Faster
  still, but the Terminal's grid would have to learn them; later if wanted.
- A long line is not folded: the view moves sideways with the cursor.

**The text's memory.**

- *Chosen: one block of 128 KB from the Memory port*, asked for when ted
  starts and given back when it ends, both on the UI task, and counted as
  the Terminal's own memory (it shows in the Task Manager while ted is
  open). The session's environment has a new entry for it (`sh::Env::memory`).
- Not chosen: *the desktop Editor's 512 KB.* A console is for small files
  and each insert moves the text after the cursor.
- Not chosen: *a gap buffer or a list of lines.* Moving at most 128 KB
  per key is well under a millisecond on the board; not worth the code.

## 4. What was built

- `core/src/sh/ted.cpp`: the editor, about 500 lines. Latin-1 inside (as
  files are), UTF-8 to and from the terminal. Files with CR LF line ends
  are shown as plain lines and written back as they came. Saving is the
  safe way (a temporary file, then its name). Tab puts spaces up to the
  next fourth column; a tab already in a file is shown as one space.
- `ted [FILE]` in the shell's table; `edit` uses it where there is no
  desktop.
- The Terminal window: `SessionMemory` (program memory in the Terminal's
  name), and Page Up and Page Down go to the program while it has the keys.

Tested on the PC, on a session with a task of its own whose output is
followed on a grid as a terminal would show it: opening a file, typing,
arrows, End, Enter, a letter beyond ASCII, Ctrl+K, save (and the CR LF
kept), find and not found, Backspace joining lines, the three answers to
"Save the changes?", Ctrl+C, a file with no name, a new file not made
unless saved, Page Down and Page Up on 60 lines, `edit` without a desktop,
a folder, two names, the memory given back. In the Terminal window: the
grid, the Terminal's memory up by 128 KB and back, Page Down reaching the
program, the file on the disk. The picture in `docs/look/ted/` was written
by that test (`CHINCHILLA_SHOT=file`). 31 tests pass. The firmware compiles
and links in the cloud; it has not run on the board.

## 5. What is left

- To check on the board: `ted notes.txt` in a Terminal with the USB
  keyboard: typing keeps up, Ctrl+S and Ctrl+Q work, the session task's
  stack (12 KB asked) has room left (`tools/dbg.py tasks`).
- In the Terminal window the status line is plain text: the grid ignores
  the "inverse" sequence that marks it on a real terminal.
- A terminal that changes size while ted is open is only noticed at
  Ctrl+L.
- Undo, copy and paste, replace.
