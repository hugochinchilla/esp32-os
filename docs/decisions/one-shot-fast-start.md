---
title: "One-shot fast start for debug restarts"
date: 2026-11-16
by: owner
status: decided
area: tools
summary: "`dbg.py reboot --fast` skips the slow start-up once; the flag is a word in NVS"
---

# One-shot fast start for debug restarts

Asked for by the owner: restarts made by the agents skip the deliberately slow
start-up. `tools/dbg.py reboot --fast` (Reboot with flag bit 0) or
`tools/dbg.py boot fast-next` (Boot screen 2, for before flashing) arms one
fast start: the next boot behaves as `fastboot = true`, logs "one-shot fast
boot", and clears the flag; boot.ini is untouched. The flag is a word in NVS:
RTC no-init memory was tried first and survives a software restart, but the
reset after flashing cleared it on this board. The agents use it for their
own restarts and post-flash restarts, except when the start-up screens are
what is being tested or shown. Measured: desktop 0.27 s after the video
starts, against 11.3 s for a normal start.
