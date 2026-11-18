---
title: "No window limit of the system's"
date: 2026-11-17
by: owner
status: decided
area: system
summary: "The system sets no limit on open windows; running out of memory is allowed to show"
---

# No window limit of the system's

Heading in the single-file log: "No window limit of the system's (the owner's decision, 2026-11-17)".


Asked whether the system should own one limit on open windows or each
program keep its own cap, the owner chose neither kind of common limit: "creo
que no tiene que haber un límite de ventanas común. Prefiero ver el sistema
fallar por falta de memoria dado el caso. Me parece un caso divertido y de
época."

- **There is no limit on the windows open at once.** The window server's
  two lists and the list of numbered windows (`os::ui`) begin with room for
  sixteen and grow out of the Memory port, twice the room each time; the
  block goes back when sixteen or fewer are left. Before, the seventeenth
  window did not open.
- **Running out of memory is how the machine says it has had enough**, and
  that is accepted. A window that finds no memory does not open and
  nothing of it is left ("There is not enough memory to open ...", when
  there is memory for the message).
- **The caps programs set for themselves are gone too** (the owner,
  asked: "Quítalos"): any number of Editors, at 576 KB each, where there
  were four and a message for the fifth; a File manager window for every
  folder opened, where there were three and the first was reused for a
  fourth folder. The one that finds no memory says so.
- Left as they were: the 32 rows of the process table and the 8 program
  tasks (a ninth program runs on the UI task). They are not limits on
  windows.
- With very many windows the taskbar's buttons shrink to four pixels and
  the last ones are off the bar. To confirm; nothing is drawn for it.
