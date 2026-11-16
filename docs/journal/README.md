# Journal

How Chinchilla came to be the way it is, told as it happened. The owner is
recording the project, so this is the written memory behind it: what was asked,
what was found, which options were on the table, which was chosen and why, and
why the others were not.

It complements two other files:

- [`docs/DECISIONS.md`](../DECISIONS.md) is the reference: the current state of
  every decision, short, kept up to date.
- [`docs/BACKLOG.md`](../BACKLOG.md) is the list of work, with its status.

The journal is the story. Entries are not rewritten later; a correction is a new
entry that says what changed.

## Writing an entry

One file per piece of work or conversation, named `YYYY-MM-DD-topic.md`, added
in the same change as the work it describes. An entry has, as far as they
apply:

1. **What was asked**, in the owner's own words where there is a message
   (in the language he wrote it; the rest of the entry is in English).
2. **What was found**: how things worked before, with file names and figures.
3. **The options considered**, each with what it would cost.
4. **What was chosen, by whom, and why**, and why each other option was not.
5. **What was built**, how it was tested, and what was not tested (PC only,
   compiled only, run on the board).
6. **What was learned**: surprises, mistakes, things worth telling.
7. **What is left**, with pointers to the backlog.

Plain words, real numbers, no summaries of summaries.

## Entries

| Date | Entry | About |
| --- | --- | --- |
| 2026-11-16 | [Architecture review](2026-11-16-architecture-review.md) | How the OS works today, the owner's ideas for processes, signals, a shell, telnet, a TTY and init, twelve design choices and what was decided |
| 2026-11-16 | [Safe saves and the hourglass](2026-11-16-safe-saves-and-hourglass.md) | Saves write a temporary file and swap it in; the hourglass pointer, chosen from four drawings; why a frozen desktop can still show it |
| 2026-11-16 | [Task statistics](2026-11-16-task-statistics.md) | Backlog item 1: the kernel counts processor time per task, the System port lists the tasks, `tools/dbg.py tasks` shows them. Seven small choices and what interrupts do to the figures |
| 2026-11-16 | [The flash-write shake](2026-11-16-flash-write-shake.md) | The picture shook while LittleFS wrote; the cache, PSRAM and the fix |
| 2026-11-16 | [The flash drive and settings](2026-11-16-littlefs-and-settings.md) | C: on LittleFS, and one plain INI file per program in C:\.config |
| 2026-11-16 | [Wi-Fi scanlines](2026-11-16-wifi-scanlines.md) | Lines while the radio connects, measured in the video interrupt; 2 dBm by default |
| 2026-11-16 | [Start-up screens](2026-11-16-boot-screens.md) | Waiting for the monitor, real self-test values, the countdown, the splash hidden |
| 2026-11-16 | [Files on the desktop](2026-11-16-desktop-files.md) | C:\Documents\Desktop as icons, welcome.txt, C:\docs moved into C:\Documents |
| 2026-11-16 | [The Trash](2026-11-16-trash.md) | Deletes to C:\.trash, Restore, Empty Trash, drag and drop |
| 2026-11-16 | [Painting each pixel once](2026-11-16-painting-once.md) | Overdraw counted, a 12-line scratch band, before and after |
| 2026-11-16 | [Slow-pointer submenus](2026-11-16-start-menu-submenus.md) | Why a slow pointer closed Start > Programs, and the fix |
| 2026-11-16 | [Internal RAM](2026-11-16-internal-ram.md) | Where the 512 KB of internal SRAM goes, what Wi-Fi costs, wired alternatives and cheap wins (report, nothing applied) |
| 2026-11-16 | [One-shot fast start](2026-11-16-one-shot-fast-boot.md) | `dbg.py reboot --fast` and `boot fast-next`; why NVS and not RTC memory |
