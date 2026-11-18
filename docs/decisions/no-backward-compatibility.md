---
title: "No backward compatibility"
date: 2026-11-17
by: owner
status: decided
area: project
summary: "No releases, no users: a changed format is read in its new form only; keep the Wi-Fi networks where free"
---

# No backward compatibility

Heading in the single-file log: "No backward compatibility (the owner's rule, 2026-11-17)".


"We are not making releases for users; backward compatibility is not
needed; the data on the board can be wiped whole every time, C: too." So a
format that changes is read in its new form only, and the board's copy is
rewritten. The saved Wi-Fi networks are to be kept where that is free;
otherwise the owner gives the credentials again. First applied to
`startup.txt` (the verbs `start` and `run` are gone from init's parser).
Still in the code from before the rule, to be taken out when next touched
or if the owner says so: moving an old `C:\docs` into `C:\Documents` at
start-up (`desktop.ini`, `[documents]`).
