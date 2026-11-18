---
title: "Program lifecycle"
date: 2026-11-17
by: agent
status: to-confirm
area: system
summary: "Windows are made when they open and destroyed when they close; `make_window`, `one`, `open_one` (backlog 19, 34)"
---

# Program lifecycle

Backlog items 19 and 34. Chosen without asking first, under the owner's
"work through the backlog" of 2026-11-16; each can be overruled. The options
and reasons are in `docs/journal/2026-11-17-program-lifecycle.md`.

- **A program's window is made when it opens and destroyed once it has
  closed**, with its widgets and whatever buffers it asked for. No fixed
  window objects are left in `core/src/shell/`. Of a closed program only the
  code and, for one that runs once at a time, one pointer remain.
- **The memory is external RAM**, through the Memory port
  (`ports::Memory::allocate` and `release`). It is where the same objects
  were as fixed ones since the day before, so internal RAM is not touched
  and its heap is not cut up by programs coming and going.
- **The window manager destroys a closed window at its safe point** (the end
  of the event or tick, where process rows go and signals take effect), never
  while the window's own code is running. `wm::Window::set_dispose` marks a
  window for it; the shell's `make_window`, `one` and `open_one`
  (`core/src/shell/internal.hpp`) are how programs use it. A new window or
  dialog is written this way; it is no longer possible to "owe" the memory.
- **The Editor can run several times**, each a process with
  its own window, text and dialogs; a file already open in one comes to the
  front. The File manager keeps its one process, with a window for every
  folder opened. (Until 2026-11-17 there were four Editors and three folder
  windows at most; the owner had both caps removed: "No window limit of
  the system's" below.)
  Every other program runs once at a time (`shell::App::many` switches it).
- **Open, Save As and the text question are made per asker**; two programs
  can each have one up. The message box stays one at a time.
- **Settings tools are processes of their own** (Display, Screen Saver,
  Network, Task Manager, and now Shut Down). **A dialog a program asks for**
  (message box, Open, Save As, Options, the Wi-Fi password) is made and
  destroyed the same way but belongs to the process that asked: "Save
  changes?" is the Editor's, which End Task and Shut Down rely on.
- **Closing a program ends it.** What should outlive that goes in its
  settings file: Culebra's best score and speed, Cactus Patch's size. A game
  half played, the Defragmenter's pretend disk and Task Manager's graph are
  not kept.
