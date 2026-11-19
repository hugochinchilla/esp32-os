---
title: "Start-up timing"
date: 2026-11-16
by: owner
status: decided
area: boot
summary: "How long the self-test stays on screen (`monitor_wait_ms`) and what it shows; the splash it also timed was removed 2026-11-19"
---

# Start-up timing

Decided by the owner. The self-test runs for real as before and then stays
on screen until `monitor_wait_ms` after the video signal started (4000 by
default), so a monitor that takes a while to lock still shows it; then the
splash for `splash_ms` (800). Revised the same day: the self-test follows the
approved drawing (docs/look/boot/boot-b-self-test.png) in layout, fonts and
colours with the board's real values (the owner: "use accurate data, the
approved design is for guidance"): the real version, the KB the memory tests
really covered (a new quick test of free internal RAM joins the external
one), the 12 MB flash disk. Its header shows at once; the lines start
appearing `monitor_wait_ms` (now 2500) after the video starts, when the bench
monitor has a picture, one by one at `selftest_pace` (130 %), the memory
count climbing with the real test. The processor, RAM and flash sizes are
still checked and logged though the drawing has no line for them. Any key or mouse button skips straight to the
desktop from either screen (pointer movement does not); the checks still run
to the end and failures are logged. `fastboot = true` shows neither screen
and skips the memory test. All in C:\.config\boot.ini (docs/settings.md). A
monitor cannot tell the board it is ready (VGA is one-way, and DDC is not
wired on the adapter board), so the wait is a time.

Revised again (2026-11-16, the owner): when the lines are done the finished
screen stays with "Press any key to continue" and "Booting in 5" counting to
1 (`selftest_hold_s`, default 5, 0 for none); a key goes on at once. The
splash is hidden by default (`splash_ms` 0: not drawn at all, no blank
frame); its code and its exact picture test stay. The desktop starts with no
windows open (Welcome and About are no longer launched).

Revised 2026-11-19 (the owner): the splash is gone altogether, its screen,
its `splash_ms`, its two fonts and its test. It had been hidden since
2026-11-16 and added nothing; the desktop fading in is the arrival now
(`no-splash.md`, `start-up-fade.md`). What this file says about the splash
above is the record.
