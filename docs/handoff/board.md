# Handoff: the board-turns thread

Written 2026-11-17 by the laptop-and-board thread when it was retired (the
owner asked for fewer tokens; a fresh thread takes over the board turns).
Read `CLAUDE.md` and `docs/DECISIONS.md` first; this note only adds what the
board manager needs and is not written down elsewhere.

## The job

One thread drives the owner's laptop (a Remote Control session in
`/home/hchinchilla/esp/esp32-os`) and the one board. For every other thread's
branch it takes a "board turn": worktree in `.worktrees/` at the pinned
commit, merge `main` in, `tools/gate.sh`, build, flash, run the checks the
thread listed, and **merge with `--no-ff` only if everything passes**. Then
append "On the board" to the branch's journal entry (append only; look at
`git diff --stat` before committing), update `docs/BACKLOG.md`, push, leave
the board on `main`, and send the figures to the owning thread either way.
One board job at a time. Tell the owner before the board restarts.

Standing limits on the laptop: no sudo, no package installs, nothing outside
the repository and `/tmp`; not the owner's Firefox (headless Chromium with a
throwaway profile); never `erase_flash`; never write or erase `nvs` (0x9000)
or `phy_init` (0xf000); the Wi-Fi network's name and password go nowhere: not
in commits, docs, logs, reports or messages.

## State when this was written

- `main` is 7d7b21d (PR 42 merged). Laptop clone and origin agree.
- The board runs `main`'s app on the **old layout**: bootloader 0x0, table
  0x8000, `ota_0` 0x10000, `ota_1` 0x200000, `otadata` 0x3f0000, C: from
  0x400000 (12 MB).
- Healthy: Wi-Fi joins, repaint 13.9 ms, free internal RAM 203,983 bytes,
  external 6,406,316, `tools/dbg.py misses` reports none.
- C: matches the file backup (19 files); the SD card is empty.
- All worktrees under `.worktrees/` were removed; local `board/*` branches
  remain.

## The queue

### PR 43, `claude/project-thread-8q7ksu` (backlog items 26 and 27): not merged

Two parts, judged separately.

**Part A, item 26, more programs on their own tasks. Merge pin 435bd25, which
has not been built or run.** Measured on the earlier pins 60113e5 and 32e202e:

- **Fails:** a File manager window on a folder of 50 or more files does not
  open (Run `C:\m50`, `C:\m81`, `C:\many`: nothing happens, no crash, no log
  line). 10 and 30 files open. Not yet compared on `main`, which opened an
  80-file folder earlier the same day.
- Stack never used, in bytes: Editor 4,776 (after a 300 KB save and Save As),
  File manager 4,888 to 5,416, Picture viewer 4,548 to 4,712, Task Manager
  3,636, Defragmenter 6,296 (full run), Cactus Patch 1,528 of 4 KB, About
  2,632, Welcome 2,788, Widget gallery 2,820, Culebra 1,396.
- Works: Run with a file and with a folder; Ctrl+Alt+Del; End Task;
  `fault hang --pid` and `fault slow --pid` on the Editor and the File
  manager (desktop back after 2.0 s, task at priority 0, ended by close or
  End Task with memory back, the slow one returns to priority 5); a desktop
  icon opens in 0.32 s while the File manager hangs; eight program tasks and
  then Cactus Patch as the ninth; About, Welcome and the gallery open and
  close with memory back; Help > About opens About as a process of its own;
  no watchdog lines; no misses that the debug link did not cause.
- Odd, unconfirmed: after 15 Editor rounds three Editor tasks were left
  (25,680 bytes of internal RAM), and went away during the File manager
  rounds; it may be the test script leaving windows open. The debug link's
  window list answers `{"error":"too many windows"}` with about ten windows
  open.
- Not done: File manager rename, Trash and drag; Picture viewer with a PNG
  and a JPEG and the dither options; the cost against `main`; anything on
  435bd25.

**Part B, item 27 first step, def4089 on the same branch** (native programs
loaded from C: with Espressif's `elf_loader` 2.1.0). Not started. Delete
`firmware/os/sdkconfig` before building. The checks are in the backlog
thread's message: the "programs: N names exported" line, `tools/build_programs.sh`
printing "0 name(s) missing", `hello.app` opening from the File manager and
counting, ten runs without PSRAM drift, two copies, End Task, a text file as
`bad.app` giving a message box, load time and misses. If A passes and B does
not, merge 435bd25 alone.

### BIOS step 2, PR 40, `claude/project-thread-y7w3uh`: pin fe550f4, not run

History of the tries, each rolled back cleanly:

1. 1bf5acd: the BIOS looped on a stack overflow in task `main`.
2. de26d90: the self-test ran; the handover to the OS failed.
3. 88a8d47 (diagnostic): the word survives `esp_restart()`; the bootloader
   wiped it because the CRC of twelve zero bytes is 0xffffffff, which ESP-IDF
   calls invalid.
4. 6ff2dde: the handover works, as do the serial byte skip, `reboot --fast`,
   the Start-menu restart, an update on trial and confirmed, Wi-Fi, telnet,
   the viewer and VNC. **Not merged:** after `fault abort` the error screen
   says only "a panic that left no record". Also, on the owner's monitor the
   BIOS becomes visible only at "Booting in 5", and a restart takes about
   15.5 s with blanks of about 6.5 s and 3.7 s.

fe550f4 makes a crash skip the BIOS (word "CHSK"; the bootloader logs "the
OS stopped on an error: starting the OS without the BIOS"). To check:
`fault abort`, `fault hang`, `fault pointer`; the two bootloader lines around
a Start-menu restart after an error screen, word for word; when the BIOS
becomes visible after an EN reset and after a restart and whether "Video: N
refills missed" appears; an update aborted during its ten-second trial must
fall back to the old slot; then the earlier passes again. Not yet done on any
pin: a trial build that hangs, the physical button, a real power cut.

The owner approved step 2 knowing that it wipes C: ("Go", in the BIOS
thread). A decision card is open in the retired thread on whether the board
may stay on the new layout between consecutive BIOS tries; unanswered, so
each try rolls back.

When step 2 merges: the board stays on the new layout, restore the saved
files that still make sense, check that the tools and docs match the new
addresses, and delete the `nvs` safety copy.

### Done recently

PR 40 step 1 (c811a63), PR 41, item 26's Culebra task (6efd5ff, 2d7ad53) and
PR 42 (`dbg.py fault slow`, 7d7b21d) are merged.

## On the laptop

- Safety copies, mode 600: `/tmp/flash-backup-2026-11-17/` holds
  `storage-old.bin` (the old C: partition, 0x400000, 12 MB),
  `partition-table-old.bin` and `nvs.bin`. **`nvs.bin` holds the Wi-Fi
  secret: never print it or move it, and delete it once the BIOS merges.**
- File backup of C:, 19 files: `/tmp/c-backup-2026-11-17/`.
- Logs, clips, screenshots and helper scripts:
  `/tmp/claude-1000/-home-hchinchilla-esp-esp32-os/04b0d22e-f1ac-581d-abf2-237e6cb97e5b/scratchpad/`.
  `/tmp` does not survive a reboot of the laptop; nothing there is in the
  repository.
  - `t26lib.py` board helpers; `p26b-flash.sh DIR` app-only flash;
    `rollback-main.sh` cable flash of `main` and the raw C: image;
    `cverify.py` compares C: with the backup and checks Wi-Fi and health.
  - `sercap.py` serial capture (`--poke S` sends a byte after S seconds);
    `logclean.py` shows only the readable lines.
  - `bios_measure.py`, `bios_frames5.py` webcam timing of restarts;
    `bios2-build.sh`, `bios2-flash-os.sh`, `bios2-flash-bios.sh`.
  - `leakA.py`, `cul10.py`, `hang26.py`, `miss26.py`, `wdt26.py`,
    `save26.py`, `slow42.py`: the item 26 checks.

## Commands

Run each from a script file: the laptop's shell wrapper refuses `bash -c`,
heredocs and long pipes. The port is `/dev/ttyACM0`, 921,600 baud; the webcam
is `/dev/video4`.

- Gate: `cd WORKTREE && tools/gate.sh`
- Build: `cd firmware/os && rm -f sdkconfig && . ~/esp/esp-idf/export.sh &&
  idf.py build`. Read `build/flash_args` before any full flash.
- App only, old layout: `python3 tools/dbg.py boot fast-next`, then in
  `firmware/os`: `idf.py -p /dev/ttyACM0 app-flash`.
- Cable flash of `main` (must write only 0x0, 0x8000, 0x10000, 0x3f0000): in
  the main clone's `firmware/os`: `idf.py build && idf.py -p /dev/ttyACM0
  flash`.
- Restore C: after a layout rollback: `esptool.py -p /dev/ttyACM0 -b 921600
  write_flash 0x400000 /tmp/flash-backup-2026-11-17/storage-old.bin`, then
  `python3 SCRATCH/cverify.py /tmp/c-backup-2026-11-17`.
- BIOS layout: `idf.py -p /dev/ttyACM0 flash` in the worktree's `firmware/os`
  (must write only 0x0, 0x8000, 0x100000, 0x4e0000), then
  `sh tools/flash-bios.sh /dev/ttyACM0` (0x10000 only). Never `idf.py flash`
  inside `firmware/bios`.
- Misses: `python3 tools/dbg.py misses` after every turn.
- Update over the network: `[update] enabled = true` in
  `C:\.config\network.ini` and a restart, then `python3 tools/ota.py
  <the board's address> firmware/os/build/esp32-os.bin`.

## Learned the hard way

- `dbg_tool` failed once in the gate under parallel load and passed alone;
  run the gate again before believing it.
- The Editor has no Ctrl+S: Alt+F, then S. Its save question needs a click
  on No; Alt+N does nothing.
- `main`'s idle processor time moves between boots (the UI task with nothing
  open: 64.6, then 147 ms per 10 s). Compare costs within one boot; repaint
  times are steady.
- Sizes built on the laptop differ from the cloud's (another ESP-IDF 5.5
  patch release).
- The Culebra task's own run counter is unreliable: use wall time less
  IDLE0 and IDLE1.
- The webcam runs at about 31.5 frames a second; resample to a fixed rate
  before timing anything.
- A fresh C: puts `welcome.txt` on the desktop, which adds about 0.5 ms to a
  repaint.
- Opening the port with plain pyserial resets the board; use `dbg.Board` or
  its `console` callback.
- The ROM's reset lines are at 115,200 baud; the bootloader, the BIOS and the
  OS log at 921,600.
- A task spinning above idle priority trips ESP-IDF's task watchdog every
  5 s, whose report from an interrupt costs a video refill.

## Open with the owner

- Whether the board may stay on the new layout between BIOS tries (card
  unanswered).
- Older, unanswered: whether `rm` uses the Trash; the visible pause when the
  taskbar restarts; checks with a real mouse and a USB keyboard; a true power
  cycle; two entries holding the Wi-Fi details in
  `~/.local/share/lean-ctx/context_radar.jsonl` on the laptop, disclosed and
  not yet removed; one system-owned window limit against per-program caps.
