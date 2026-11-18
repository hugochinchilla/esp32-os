---
title: "Chosen for the taskbar as a process"
date: 2026-11-17
by: agent
status: to-confirm
area: desktop
summary: "`desktop` is the window server and `taskbar` its child process (backlog 21, first part)"
---

# Chosen for the taskbar as a process

Heading in the single-file log: "Chosen for the taskbar as a process (2026-11-17, to confirm)".


Backlog item 21, first part. The owner was asked (a card in the thread,
the first option recommended); built on it meanwhile.
[Journal](journal/2026-11-17-taskbar-process.md).

- `desktop` (protected) is the window server: windows, pointer,
  background, screensaver, network, Ctrl+Alt+Del. `taskbar` is its child,
  a system process that is not protected and has the respawn flag: the
  taskbar with the Start menu and the quick-launch icons, and the desktop
  icons. Named `taskbar`, not `shell`.
- Ended (TERM or KILL alike), it takes those away and the table starts it
  again. Windows stay where they are and the work area does not change.
  Programs it started become init's children.
- The window manager paints once between the ending and the restart. No
  pause is added; whether to add one is the owner's to say.
- The brake of item 18 applies. After it, `run taskbar` from a console
  starts it again.
- The desktop starts it; there is no line for it in the start-up file.
- The drawing code is still in the window manager. Cutting that file in
  two is the second part.
