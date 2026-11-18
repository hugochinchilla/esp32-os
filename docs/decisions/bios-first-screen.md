---
title: "The BIOS's first screen"
date: 2026-11-17
by: owner
status: decided
area: boot
summary: "The BIOS shows a first screen until 7 s after the reset, because the monitor takes that long"
---

# The BIOS's first screen

Heading in the single-file log: "The BIOS's first screen (the owner, 2026-11-17)".


`docs/journal/2026-11-17-bios-first-screen.md`.

- Before the self-test the BIOS shows a first screen (asked for as 2 or
  3 s; held until 7 s after the reset, the owner's choice on a card, because the bench
  monitor was measured to show a steady picture only 5.5 to 6 s after a
  reset, for the BIOS and for `main` alike, not the 2.4 s recorded on
  2026-11-16), as the PCs
  of the nineties showed their BIOS maker's logo and an energy badge. It
  covers the time the monitor takes to wake; nothing that matters is on it.
- The badge is our own: a leaf with a lightning bolt in a round ring, the
  name under it, and "Runs on a USB port". No square frame, no "no fan, no
  noise" (the owner's changes to the first drawing). The real Energy Star
  mark is not used.
- The name "Energy Nibbler" is to confirm.
- The BIOS is called "Rodent BIOS" on its screens, on serial and in the
  docs from here on (the owner, 2026-11-17: so that not everything is
  called Chinchilla). Earlier journal entries keep the words of their day.
  The OS's own self-test screen, which still says "Chinchilla BIOS" and is
  shown only without a BIOS or after an error, goes away in step 3.
- The second drawing of the first screen is approved by the owner.
- The memory count in the BIOS runs at the speed of the test, without the
  pause between steps that the OS's self-test has.
- After an error the OS restarts without the BIOS, and a reset by a
  watchdog or a brownout starts the OS directly. Left as it is for now.
- The BIOS will not show the OS's error screen (the owner, 2026-11-17,
  reversing the "to confirm" point of the first discussion): the BIOS is
  to have no link with the OS's errors. It is a self-test and, later, a
  Setup with the partition to start, the clock and other settings. The OS
  is to say on the desktop that the last session failed, with a button
  that opens the report in the text editor. Not built; what is still open
  is in `docs/handoff/bios-step-3.md`.
