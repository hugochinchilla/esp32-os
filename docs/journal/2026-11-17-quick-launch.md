# Quick-launch icons that launch something

Backlog item 30. Branch `claude/project-thread-8q7ksu`.

## 1. What was asked

The backlog line is "Quick-launch icons by the Start button that launch
something". The first desktop drew three small icons there, as in the
target picture (`docs/look/target-98.png`), and they did nothing; the owner
had them taken out on 2026-11-16 ("the taskbar shows only what works") with
the note that they come back when they launch something. The owner's
instruction for this run of work is "ve haciendo, no pares hasta terminar
el backlog, dejo a tu criterio", so the choices are the agent's and are
marked "to confirm" in `docs/DECISIONS.md`.

## 2. What was found

- The taskbar belongs to the window manager (`core/src/wm/wm.cpp`), which
  knows nothing of programs. It already takes one thing from the desktop in
  the same way: the tray icon and what a click on it does.
- Every program has a 16x16 icon of its own since item 29, and the desktop
  starts a program by name (`shell::find_app`, `launch`), as the Start menu
  and Run... do.
- The window buttons started at a fixed x = 72.

## 3. The options, and what was chosen

**Which programs.**

- *Chosen: File manager, Editor, Terminal.* The three things one opens
  most on this machine: look at the files, write something, type a command.
- Not chosen: *"show the desktop"* as the first icon, as in Windows 98.
  There is no "minimise everything" yet; it would be new behaviour, not a
  shortcut to something that exists.
- Not chosen: *the same three as the old drawing* (they were stand-ins).
- Not chosen now: *a list the user edits* (a folder of shortcuts, or a
  settings file). The right end state, and a bigger piece: it needs
  shortcuts as files. The list is three names in one place in the code
  until then.

**How they behave.**

- *Chosen: like the other taskbar buttons*: flat, pressed in while held,
  acting when the button is let go over the icon; one click, not two.
- The program starts as a child of the desktop, exactly as from the Start
  menu: the Editor and the Terminal open another copy each time, the File
  manager comes to the front if it is open.
- Not chosen: *a raised edge when the pointer is over one* (the 98 look).
  The taskbar does not follow the pointer today; pressed-while-held is
  feedback enough.
- No name appears under the pointer: there are no tooltips yet.

**Where the code goes.**

- *Chosen: the window manager draws and handles them, the desktop says
  what they are* (`Wm::set_quick_launch`, up to four). The window buttons
  start after the last icon. With none set the taskbar is as before, so
  the window manager's own tests did not change.

## 4. What was built

- `wm::Wm::QuickLaunch`, `set_quick_launch()`, `quick_launch_rect()`; the
  icons in the taskbar's painting and in its mouse handling.
- `shell::start()` sets the three.

Tested on the PC: in the window manager alone, the layout (after Start,
side by side, window buttons moved right, at most four, entries without an
icon or an action left out), pressed while held, acts on release inside,
not when let go elsewhere, nothing in the gap; on the desktop, a click on
each icon starts the program as the desktop's child, a second click on the
Editor opens a second copy and on the File manager does not. Ninety
reference pictures changed, since nearly every one shows the taskbar; the
new look is in `docs/look/quick-launch/`. 31 tests pass. The firmware
compiles and links in the cloud; it has not run on the board.

## 5. What is left

- To check on the board: the three icons are sharp at that size on the
  real screen, and each opens its program with one click.
- With six or more windows open the window buttons are 72 pixels narrower
  in all than before.
- A list the user can change; tooltips; "show the desktop".
