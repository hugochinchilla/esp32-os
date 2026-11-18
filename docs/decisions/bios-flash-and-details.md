---
title: "BIOS stage: flash and details"
date: 2026-11-17
by: owner
status: decided
area: boot
summary: "The BIOS takes 1 MB from C:; two update slots of 1.94 MB; C: is 11 MB"
---

# BIOS stage: flash and details

Decided by the owner: the BIOS takes 1 MB from C:. The BIOS partition is
0x10000 to 0x100000, the two update slots stay 1.94 MB each (from 0x100000
and 0x2f0000), `otadata` at 0x4e0000, C: from 0x500000 with 11 MB; C: is
wiped once, NVS (the Wi-Fi networks) is not. Not chosen: a 384 KB BIOS with
two slots of 1.75 MB (tight for both); a 1 MB BIOS with a single OS slot (no
way back after a bad update). Not applied yet: the table changes when the
BIOS is built.

Taken as recommended, the owner did not object (to confirm): the BIOS's
settings live in NVS, not `boot.ini`; the BIOS shows the crash screen;
an OS image received over serial is a later step.
