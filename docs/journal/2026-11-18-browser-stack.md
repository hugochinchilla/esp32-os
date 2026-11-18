# Vizcacha's stack: 1.4 KB left was too little

Follows [Vizcacha, a web browser](2026-11-17-web-browser.md), backlog item 53. Branch `claude/project-thread-w6qa7m`, continued from `main` after the browser was merged (408c7c7).

## What was found

The board turn that let the browser be merged measured the stack of its task: of 12 KB, 1,424 bytes were never used while a Wikipedia page (through FrogFind) was loaded and scrolled. It passed, but a page a little deeper would not have, and a stack overflow restarts the board.

GCC's `-fstack-usage` on the PC named the frames. Two stood out:

| Function | Stack |
| --- | --- |
| `www::Document::load` | 3,808 bytes |
| `BrowserWindow::trouble` (the "could not be opened" page) | 2,992 bytes |
| `www::resolve` | 1,328 |
| `www::Fetch::poll` | 1,136 |

`load` held two `Reader` objects, one to count and one to fill, each close to 2 KB (its buffer for a word and its lists of open tags), both alive for the whole call. `trouble` wrote its page into three arrays on the stack and then called `load` on top of them: 6.8 KB between the two, the deepest path in the program.

## The options

- **Raise the stack to 16 KB**, what the UI task has. One number; 4 KB more of internal RAM held while the browser is open, and the cause stays.
- **Take the big frames off the stack** (chosen): the same memory, borrowed from the Memory port (external RAM on the board) for the moment it is needed.
- Both. Not needed if the second gives room enough; the board says.

## What was built

- `Document::load` makes its reader in memory from the Memory port, one at a time, and keeps only the counts. If there is no memory for it the page is not loaded, as when there is none for the page.
- `trouble` writes its page in memory from `os::allocate` and gives it back before returning.
- The stack stays at 12 KB.

After: `load` and `trouble` are no longer among the frames over 600 bytes; the largest is `resolve` with 1,328. The path that measured 1,424 bytes free went through `load`, so about 3.7 KB more should be free there. That is arithmetic from the PC's compiler, not a measurement: the board's figure is the one that counts.

## How it was tested

The PC tests pass (50), with the browser's ten pictures unchanged and its check that all memory is given back. The firmware compiles and links. Not run on the board.

## What is left

A board turn to read the Vizcacha task's unused stack again on the same Wikipedia page and on an error page; then the 12 KB can be trimmed or kept.
