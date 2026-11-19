# Handoff: the board-turns thread, 2026-11-19

Written 2026-11-19 when the owner asked for this board thread to be retired
and a fresh one started. Read `CLAUDE.md`, `docs/DECISIONS.md` and
`.claude/skills/board-turn/SKILL.md` first. This note only adds what the
next board thread must know and what is not written down elsewhere. It
continues `docs/handoff/board.md` (2026-11-17), which is still right about the
job, the standing limits and the ground rules; the layout part of that note is
out of date (see below).

## The job in one paragraph

One thread drives the owner's laptop (Remote Control session in
`/home/hchinchilla/esp/esp32-os`) and the one board on `/dev/ttyACM0`. The
coordinator relays "board turn" requests; each names a PR, a commit and a
request file in `/mnt/project-files/board-requests/`. Take a worktree
(`git worktree add --detach .worktrees/NAME PIN`), merge `main` in, build,
flash, run the checks, then merge with `--no-ff` only if the request allows it
and every check passed, append "On the board" to the branch's journal entry,
update the item's row in `docs/BACKLOG.md`, push, leave the board on `main`,
and send **one** report per batch (in Spanish, since the owner writes
Spanish; everything in the repo is English). Tell the owner in the thread before
the board restarts.

## State of the board when this was written

- `main` is 2ea75ed (PR 88 merged; the board runs `main`'s firmware, flashed
  with `tools/flash.py` itself): `esp32-os.bin` 1,726,624 bytes. Laptop clone
  and origin agree.
- **Layout: 3 MB slots** (PR 79): bootloader 0x0, table 0x8000, BIOS 0x10000,
  otadata 0xf0000, `ota_0` 0x100000, `ota_1` 0x400000, C: 0x700000 (9 MB).
  `nvs` (0x9000, the saved Wi-Fi network, the Bluetooth keys) has never moved
  and must never be written or erased. Never `erase_flash`. After an update
  over the network the board may run from `ota_1`; then `app-flash` (which
  writes `ota_0`) has no effect: finish with a full flash. A safety copy of
  `nvs` is in `/tmp/flash-backup-2026-11-19/nvs.bin` (read only, never print it,
  never move it out of `/tmp`).
- Screen mode: 640x480. Theme Classic, plain Steel blue desktop (no pattern).
- Wi-Fi joined (the network's name and password go nowhere). `C:\.config\
  network.ini` has `tx_power_dbm = 8`: that is not what was left at the end of
  my turns (I left 20); somebody set it (probably the owner while testing the
  Bluetooth pointer). Ask the owner before changing it.
- Bluetooth: the owner's Logitech **MX Master 3** (mouse) and **MX Keys Mini**
  (keyboard) are paired (keys in `nvs`, list in `C:\.config\bluetooth.ini`;
  CRLF line ends there). They are "remembered, not connected" after every
  restart until a key or a click wakes them (then both are ready in 2 to
  5 s). They are the owner's: do not forget them.
- Files on C: besides the system's: `C:\Programs\DOOM1.WAD` (4,196,020 bytes,
  MD5 f0cefca49926d00903cf57551d901abe; PR 89's test copy, kept for Doom) and
  `C:\Programs\doom.app` (354,184 bytes, from PR 89's firmware, not on `main`).
  `C:\system\installed.txt` is the flash tool's list (9 lines). `C:\.config\
  chess.ini` exists (written by the Chess tests; harmless, delete it if a request
  says so). `C:\crash.txt` holds the records of the crash tests (the hangs,
  `fault wait`, `fault hang`, one unexplained panic in `open_in_editor`).
- No worktrees are left in `.worktrees/`.

## Open requests when this thread ended

- **PR 89, Doom** (item 81, branch `claude/project-thread-oebalh`, 7763b7e; a
  log fix 03d523e was added after): tested on the board, all checks passed,
  **not merged**: the owner has to play it and confirm choices. Real speed is
  26 to 35 pictures a second (my first report read the log wrong).
- **PR 82 / 84 / 86** (shut down, start-up fade, desktop shortcuts) were
  merged after the owner confirmed. Nothing else is waiting on the board that I
  know of; ask the coordinator.
- Findings that went to other threads, not fixed here: a panic once in
  `shell::open_in_editor` (`core/src/shell/editor.cpp:364`, the `g_editors`
  list, a stale pointer) seen once, not reproduced; the Bluetooth pointer is
  "much better, not perfect" with Wi-Fi power saving on while a device is
  connected (item 51 is merged; `wifi_rests = false` in `bluetooth.ini` turns
  the saving off for comparison).

## How a board turn goes here (the tools I used)

Everything below is in the repository or `/tmp`; the helper scripts of this
session were in the session's scratchpad and are gone with it. The ones that
could be worth writing again (the project files have copies of some:
`/mnt/project-files/capture84.py`, `ctl84.sh`, `burst77c.py`, `lag77b.py`):

- `tools/build_programs.sh` then in `firmware/os`: `rm -f sdkconfig && idf.py
  build` (source `~/esp/esp-idf/export.sh`). `tools/build_programs.sh` must
  print "0 name(s) missing". `tools/flash.py` (PR 88) now does the whole
  flash: build, the four esptool writes, then it copies the programs and
  pictures to C: over the debug link (`--no-build`, `--files-only`, `--list`,
  `--wad FILE`). It waits for C: to answer and 3 s of uptime, so it needs no
  window to be open.
- `idf.py -p /dev/ttyACM0 app-flash` is the quick way when the board runs
  `ota_0`. A full flash writes only 0x0, 0x8000, 0xf0000, 0x100000 (read
  `build/flash_args` first).
- Python access to the board: `import dbg` from `tools/` and
  `dbg.Board("/dev/ttyACM0", timeout=8)`; the CLI is `tools/dbg.py`
  (`windows`, `memory`, `repaint`, `tasks`, `misses`, `dmesg`, `frames`, `stats`,
  `screenshot`, `key down|up|tap`, `mouse`, `put/get/ls/rm/mkdir`, `mode`, `bt`,
  `fault wait|hang|abort|pointer|slow`, `boot selftest|crash|fast-next`,
  `reboot [--fast]`).
- **Only one process at a time may hold the serial port.** A second `dbg.py`
  while a script holds a `Board` makes both fail ("the serial port went away"),
  and `dbg.py put` of a big file blocks every other command until it ends.
  Use the Board object for everything inside a script.
- `dbg.py frames --reset` restarts the board (the global `--reset` flag eats
  it); use `Board.frames(reset=True)` in a script instead.
- Screenshots: `dbg.py screenshot FILE.png`. From a script, `Board.screenshot`
  returns raw one-byte pixels (640x480 or 800x600); `tools/rfb_client.py HOST
  shot|time|click` is the remote-screen client (VNC, port 5900, no password).
- The webcam is `/dev/video4` (ffmpeg: `-f v4l2 -input_format mjpeg
  -video_size 1280x720`). It points at the monitor. The monitor loses the
  signal for a few seconds at each restart and at each mode change, so the
  webcam cannot show a start-up fade.
- The laptop's shell wrapper refuses: `bash -c`, heredocs to interpreters,
  `sleep` over a minute (use `until` loops with a timeout), long pipes, `eval`
  and `$()` at command position, and any script not already in its allowlist
  when called through a pipeline; write scripts with the Write tool and run
  them by path. `grep -n` on a file with no match exits 1 and fails a chained
  command.
- Opening the port with plain pyserial resets the board; use `dbg.Board`.
- The owner's Start menu coordinates at 640x480 (Start button (30,466),
  Programs (70,357), Settings (70,381), Run (70,405), Shut Down (70,436); the
  Programs list is bottom-aligned, so a new program shifts the others up; the
  Settings list has Bluetooth between Network and Screen Saver). At 800x600
  everything is 120 pixels lower, and the dialogs are re-centred. A click on the
  taskbar button of a window that is already in the front minimises it.
- Typing: `dbg.py type` cannot send `:`; use the Run dialog with paths only by
  other means (the Picture viewer's Open dialog works).
- While the owner has his Bluetooth mouse awake, its movements move the
  pointer under your own `dbg.py mouse` clicks: ask him to keep still for the
  clicks, or send keys instead.
- The webcam, the VNC client, `dbg.py` over serial and the owner's mouse can
  all be busy at once; say so in the report when a figure was taken with the
  Wi-Fi congested (the room's Wi-Fi was slow on 2026-11-19: a full remote
  screen update took 5 to 13 s where it took 2 s earlier in the week).

## Things that are easy to get wrong

- A request that says "do not merge" means it: Doom (PR 89) and any BIOS PR are
  reported, not merged, unless the coordinator later says so in a message that
  cites the owner.
- Merge conflicts in `docs/BACKLOG.md` and `docs/journal/README.md` are
  routine: keep both sides (a `git rerere` resolution is recorded and often
  applies itself). `docs/DECISIONS.md` is an index now: regenerate it with
  `tools/decisions_index.py`, never edit it by hand.
- Journal notes are appended ("On the board (date, commit)"), with no model
  name and no Wi-Fi detail. After a merge, check `git status -sb` shows
  `main...origin/main` level.
- `tools/gate.sh` has one PC test that is flaky under load (unidentified);
  run it again before believing a single failure.
- End every batch with the board on `main`, 640x480, plain desktop, Wi-Fi
  joined and `dbg.py misses` at 0, and say what is left on C:.
