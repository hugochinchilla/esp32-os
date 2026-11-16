# 2026-11-17: New dialogs are programs of their own, not parts of the desktop

A rule set by the owner while looking at where the internal RAM goes. This
entry records what he said, what the code really does today, what the rule
means at each stage of the plan, and the backlog item it adds. Documentation
only; nothing was built.

## 1. What was asked

The Display dialog had just been merged, and its cost had been measured: 2.8 KB
of internal RAM, held from start-up whether the dialog is open or not
([desktop background](2026-11-17-desktop-background.md)). The owner:

> Vale, y este buffer de los ajustes del diálogo display es porque este ajuste
> no se está haciendo con una aplicación distinta, sino que está dentro del UI,
> ¿no? Como en ese diseño monolítico que estamos intentando dejar de tener, en
> el cual las aplicaciones son threads separados y que cuando se cierran tienen
> el efecto de liberar la memoria. Por lo tanto, los nuevos diálogos que creamos
> deberían seguir esta filosofía de ser programas ejecutables separados fuera
> del proceso del escritorio.

In short: is that memory held because Display is part of the desktop instead
of a separate application? Then new dialogs should be separate programs
outside the desktop process, whose memory is freed when they close.

## 2. What was found

His reading of the cause is right. His picture of where the project stands is
one step ahead of the code: programs are not separate threads yet, and closing
one frees nothing yet. Checked on `main` at `4b3957f`:

- **Tasks.** Chinchilla's own code still creates three FreeRTOS tasks and no
  more (`firmware/os/main/main.cpp`): `ui`, and the short-lived `video` and
  `scanlog`. Every program and dialog runs on `ui`, called by the window
  manager.
- **Process rows.** Since the process table (backlog item 2), an open program
  has a row with a PID, and the windows and timers it makes are recorded as
  its own. Display has one too: `display_dialog()` opens under
  `AsProgram("Display")`. So it already looks like a separate program in Task
  Manager.
- **Memory.** A row is bookkeeping, not memory. The dialog itself is
  `static DisplayDialog d;` in `core/src/shell/display.cpp`: one fixed object
  compiled into the firmware, with all its widgets inside it. The linker
  reserves its bytes for the whole life of the machine. Closing the window
  hides it and removes the row; the bytes stay. Every program is built this
  way (Editor, File manager, Task Manager, Network, Screen Saver, the file
  dialogs, the message box).
- **How much.** Our own fixed objects come to about 113 KB of internal RAM
  ([internal RAM](2026-11-16-internal-ram.md)), where roughly 100 KB is free
  with Wi-Fi on. Display's share is the 2.8 KB above.

So "inside the UI" is true in two separate senses, and only one of them costs
memory:

| Sense | Today | What it costs |
| --- | --- | --- |
| Runs on the UI task | Yes, every program | No memory. It costs robustness: a stuck program freezes the desktop |
| Exists for ever as a fixed object | Yes, every program | The memory. This is what the owner noticed |

## 3. What "a separate program" can mean here

Three steps, each already on the backlog, each giving more of what the owner
described:

| Step | What it gives | Backlog | State |
| --- | --- | --- | --- |
| A program with a life: created when started, destroyed when closed | **The memory comes back on close.** Two of the same program become possible | 19 | todo |
| A task of its own | A stuck program no longer freezes the desktop; real figures per program from FreeRTOS | 26 | todo, after the OS-call layer |
| A file of its own on C:, loaded when started | Programs can be added without a new firmware; code memory comes back too | 27 | deferred by the owner earlier |

The freed memory he asked about comes from the first step alone. It does not
need threads or loadable files. The other two build on it.

Moving the fixed objects to external RAM (the open question from the internal
RAM entry) attacks the same 113 KB from the other side: the objects stay for
ever but stop using the scarce kind of memory. The two do not conflict.
External RAM is 8 MB, so once objects live there the saving from freeing a
closed dialog is small in bytes; the lasting value of this rule is the design
(a clean start and end, no hidden shared state), which the later steps need.

## 4. The rule

Set by the owner, 2026-11-17:

**A new dialog or settings tool is a program of its own, not a part of the
desktop.** In practice, from now on:

1. It has its own row in the process table while it is open (as Display does).
2. It keeps no memory while it is closed. Its window, widgets and working
   buffers are created when it opens and given back when it closes.
3. It reaches the desktop only through calls any program could make (settings
   files, the window manager's public interface), not through the desktop's
   private variables.
4. Its code is a file of its own under `core/src/shell/`, so it can later
   become a task (item 26) or a loadable file (item 27) without being untangled
   first.

Point 2 needs a way to create and destroy a program, which is backlog item 19
and does not exist yet. Until it does, a new dialog follows points 1, 3 and 4,
is written so that nothing in it assumes it lives for ever (no state that must
survive a close, apart from its settings file), and its journal entry says that
point 2 is still owed.

## 5. Options considered

| Option | Taken? | Why |
| --- | --- | --- |
| The rule as above, met in stages | **Yes** | It is what the owner asked, and each stage is work already planned |
| Wait for loadable programs (item 27) before changing how dialogs are written | No | That item is far off; every dialog written meanwhile would add to what must be moved later |
| Give each dialog its own FreeRTOS task now | No | The window manager and widgets assume one task (decided 2026-11-16: the hybrid model); a task also costs 4 to 8 KB of internal RAM for its stack, more than the dialog it would free |
| Only move the fixed objects to external RAM and leave the design | No | It recovers the RAM but keeps the monolith the owner wants to leave behind. Still worth doing for the RAM, separately |

## 6. What it adds to the backlog

Item 34: Display and the other built-in dialogs and settings tools (Screen
Saver, Network, Shut Down, the file dialogs, the message box, Task Manager)
move out of the desktop's permanent memory, onto the lifecycle from item 19.
It sits in Phase 4 after item 19 and before items 26 and 27. Display goes
first: it is the newest and smallest, and makes a good first user of item 19.

Recommended to the owner, not yet decided: bring a first slice of item 19
forward (create and destroy for one kind of program, the dialog), so that the
next new dialog can follow the rule in full instead of owing point 2.

## 7. What was learned

- A PID in Task Manager and memory that comes back on close are different
  things. The first arrived with the process table; the second has not yet.
- Of the three things "a separate program" could mean, the cheapest one is the
  one that frees the memory.
