---
name: board-turn
description: How to run a board turn for the ESP32-S3 OS on the owner's laptop. Use when asked to test a branch or PR on the real board, flash it, run the standard checks and report.
---

# Board turn

A board turn tests one or more branches on the one real board, connected to the
laptop at `/dev/ttyACM0`. Source of truth for state and history:
`docs/handoff/board.md`, `CLAUDE.md`, `docs/DECISIONS.md` (an index: open only
the files in `docs/decisions/` that the branches under test touch). Everything written in
the repo is in English; reply to the owner in the language he writes.

## Ground rules

- One board job at a time. Run the whole batch in one session, then send ONE
  report covering every item. Do not start board work unprompted.
- Tell the owner before the board restarts.
- Never `erase_flash`. Never write or erase `nvs` (0x9000) or `phy_init`
  (0xf000). Check `build/flash_args` before any full flash.
- The Wi-Fi name and password go nowhere: not in commits, docs, logs, reports
  or messages. `nvs.bin` safety copies are never printed or moved.
- Board data, C: included, may be wiped. Keep the Wi-Fi keys if it costs
  nothing; if they are lost, ask the owner.
- No sudo, no package installs, nothing outside the repo and `/tmp`; not the
  owner's Firefox (headless Chromium, throwaway profile).
- Merge with `git merge --no-ff` only if the request allows it AND every check
  passes. BIOS PRs are reported, not merged, unless the request says so.
- The shell wrapper on the laptop refuses `bash -c`, heredocs and long pipes:
  put commands in script files in the scratchpad.
- Opening the port with plain pyserial resets the board; use `dbg.Board` or
  its `console` callback. The ROM's lines are 115,200 baud; the bootloader,
  BIOS and OS use 921,600.

## 1. Prepare the branch

1. `git fetch origin`; add a worktree: `git worktree add .worktrees/NAME PIN`
   (checkout the pinned commit or later on the branch).
2. In the worktree: `git merge origin/main` (the branch must contain main's
   latest commit). Resolve nothing silently: report conflicts.
3. `tools/gate.sh`. `dbg_tool` has failed once under parallel load and passed
   alone: run the gate again before believing a single failure.

## 2. Build

```
cd WORKTREE/firmware/os && rm -f sdkconfig && . ~/esp/esp-idf/export.sh && idf.py build
```

Loadable programs (item 27): `tools/build_programs.sh BUILDDIR` (all names
exported; it must print "0 name(s) missing"), output in `build/programs/`.

## 3. Flash

Which layout the board is on decides the command. Read the partition table
offsets in `build/flash_args` first.

- **3 MB layout** (`main` since backlog item 71, 2026-11-19: bootloader 0x0,
  table 0x8000, BIOS 0x10000, otadata 0xf0000, ota_0 0x100000, ota_1 0x400000,
  C: 0x700000). The commands are those of the BIOS layout below, except that a
  full cable flash must write only 0x0, 0x8000, 0xf0000, 0x100000. `ota_0`
  starts at the same place in both layouts, so `app-flash` of a branch cut
  before item 71 still works on a board with this layout; a full `flash` of
  such a branch does not (it would put the old table back): merge `main` in
  first, as step 1 says. After an update over the network the board may run
  from `ota_1` (the BIOS's "Boot device" line says which): `app-flash` writes
  `ota_0` only, so it would not be started. Finish a turn that did OTA checks
  with a full `idf.py flash` (it rewrites `otadata`, so `ota_0` is started again).
- **BIOS layout** (the board's layout from PR 40, 2026-11-17, until item 71:
  bootloader 0x0, table 0x8000, BIOS 0x10000, ota_0 0x100000, ota_1 0x2f0000,
  otadata 0x4e0000, C: 0x500000): app only, `python3 tools/dbg.py boot fast-next` then
  in `firmware/os`: `idf.py -p /dev/ttyACM0 app-flash`. A full cable flash
  (`idf.py -p /dev/ttyACM0 flash` there) must write only 0x0, 0x8000, 0x100000,
  0x4e0000; the BIOS itself is written with `sh tools/flash-bios.sh
  /dev/ttyACM0` (0x10000 only; never `idf.py flash` inside `firmware/bios`).
  A fast start armed with `boot fast-next` is honoured by the BIOS too.
- **Old layout** (before PR 40: ota_0 0x10000, ota_1 0x200000, otadata 0x3f0000,
  C: 0x400000, no BIOS): only for testing a branch cut before PR 40. Do not
  put a post-merge build on it; it needs a layout change either way.
- Restore the files of C: after a layout change with `dbg.py mkdir` and
  `dbg.py put` from the file backup (`/tmp/c-backup-*/`, if it still exists).
  The Wi-Fi networks live in `nvs` (0x9000), which flashing never touches.
- Update over the network (OTA): `[update] enabled = true` in
  `C:\.config\network.ini`, restart, then
  `python3 tools/ota.py <board address> firmware/os/build/esp32-os.bin`.
- Leave the board on `main` when the turn ends (reflash main) unless the
  request says otherwise.

## 4. dbg.py

`python3 tools/dbg.py [--port P] [--logs] [--reset] COMMAND`. Port defaults to
`/dev/ttyACM0` or `$DBG_PORT`. Commands used in turns:

| Command | Use |
| --- | --- |
| `ping`, `info`, `stats` | the board answers; uptime, free memory |
| `memory` | heaps (internal, external/PSRAM) and video timing |
| `tasks` | kernel tasks, priorities, unused stack in bytes (stack high-water marks) |
| `windows` | windows with their pids; answers "too many windows" at about ten |
| `misses` | missed video refills not caused by the debug link; exit 1 if any |
| `frames [--reset]` | pieces of the desktop's work longer than one video frame (16.8 ms), with where the time went; exit 1 if any. `tools/bench/desktop_frames.py` is the scripted benchmark |
| `dmesg`, `monitor` | the board's log |
| `ls`, `get`, `put`, `rm`, `mkdir` | files on C: (`put build/programs/hello.app 'C:\hello.app'`) |
| `fault hang\|pointer\|abort\|slow [--pid N]` | fail on purpose; report in `C:\crash.txt` |
| `boot selftest\|fast-next\|crash` | start-up screens; `fast-next` before flashing |
| `screenshot`, `key`, `type`, `text`, `mouse` | drive and photograph the UI |
| `repaint` | time a full repaint (steady, about 13.9 ms on main) |
| `reboot [--fast]` | restart |

Webcam for restart timing: `/dev/video4` (about 31.5 fps: resample before
timing). Serial capture with a throwaway script, never plain pyserial.

## 5. Standard checks (every turn)

Take "before" figures on the same boot, then compare within that boot (idle
processor time moves between boots).

1. **Boot log**: expected lines present (for item 27: "programs: N names
   exported", with no "(the table was refused)"); no panic, no watchdog lines.
2. **Memory**: `dbg.py memory` before and after each scenario. Opening then
   closing a window or program must give memory back; run 5 to 10 open/close
   cycles and report drift in internal and external bytes. Healthy main
   baseline: free internal about 203,983, external about 6,406,316.
3. **Repaint**: `dbg.py repaint`, compare with the baseline.
4. **Misses**: `dbg.py misses` after every scenario and at the end. A miss not
   caused by the debug link is a failure to chase, not noise. No lock or
   interrupt mask over about 100 us; no `uxTaskGetSystemState` outside the
   Tasks request.
5. **Stacks**: `dbg.py tasks`; report unused stack per program task.
6. **Fault handling**: `fault hang --pid` and `fault slow --pid` on a program:
   desktop back in about 2 s, task priority 0, ended by close or End Task with
   memory back (slow returns to priority 5). Whole-board `fault abort`,
   `fault hang`, `fault pointer`: error screen and `C:\crash.txt`.
7. **Restart**: Start-menu restart and `reboot --fast`; Wi-Fi rejoins; time
   to desktop. Note when the BIOS first becomes visible after a restart vs an
   EN-pin reset (webcam).
8. **OTA trial/rollback**: update on trial (10 s) confirmed; an update aborted
   during the trial (`fault abort`) must fall back to the old slot.
9. **After a BIOS pin**: handover to the OS, serial byte skip, the slow-memory
   words before and after a Start-menu restart, "Video: N refills missed"
   line, bootloader lines around a restart after an error screen, word for
   word.
10. The request's own checks (screenshots, End Task of one of two copies,
    large folders in the File manager, ...).

UI notes: the Editor has no Ctrl+S (Alt+F, then S); its save question needs a
click on No, Alt+N does nothing. A fresh C: puts `welcome.txt` on the desktop
(about +0.5 ms per repaint). A task spinning above idle priority trips the task
watchdog every 5 s, which costs a video refill.

## 6. After the checks

- Pass and merge allowed: `git merge --no-ff` the branch into `main` in the
  main clone.
- Append "On the board" to the branch's journal entry in `docs/journal/`
  (append only: read the file first, and `git diff --stat` before committing),
  and update the item in `docs/BACKLOG.md`. English only.
- Commit directly to `main` and push. Remove the worktree. Leave the board on
  `main`.

## 7. Report format

One message per batch, to the coordinator, per item:

- Item name, PR, branch, pin tested (full SHA short form), main commit merged.
- Verdict: PASS / FAIL / BLOCKED, and whether it was merged.
- Each check: pass/fail with the figures (bytes before/after, ms, counts).
- Serial or log lines asked for, verbatim; backtrace on a panic. Never any
  Wi-Fi details.
- Screenshots as files, with paths.
- Anything odd or unconfirmed, labelled as such; what was not run.
- State the board was left in (layout, firmware, C: contents) and any open
  question for the owner.
