---
title: "The settings tools ask through the OS calls"
date: 2026-11-17
by: agent
status: to-confirm
area: system
summary: "Display, Screen Saver, Network and Shut Down are programs that ask through new OS calls (backlog 20)"
---

# The settings tools ask through the OS calls

Heading in the single-file log: "The settings tools ask through the OS calls (2026-11-17, the agent's, to confirm)".


Backlog item 20, second part. Display, Screen Saver, Network and Shut Down
were each half a window and half something the system keeps going. The
window is a program and asks through new calls (`os::set_backdrop`,
`os::set_screen_saver`, `os::preview_screen_saver`, `os::last_saver_run`,
`os::shut_down`, `os::network`); the other half is the system's (the
screensaver's runner in `core/os`, `network_service.cpp`, `power.cpp`).

- One call for each thing a tool sets, not one general "set a desktop
  setting by name" call: names would be an interface nothing checks at
  build time, and the road to a registry, which the settings decision
  turned down.
- The network is handed over whole (`os::network()`), as the drives are to
  the picture decoder, not as a call for each thing the panel does. Those
  calls belong to item 37 (numbered calls with handles).
- Display and Screen Saver keep nothing in memory while closed: they read
  their settings file when they open.

Journal: `docs/journal/2026-11-17-os-calls-settings-tools.md`.
