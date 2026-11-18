# Handoff: the backlog thread (2026-11-17)

Written when the thread "Work through the remaining backlog" was retired to
save tokens. Its work continues in threads with one goal each. Read
`CLAUDE.md` and `docs/DECISIONS.md` first; the journal entries named below
have the whole story.

## State

- Branch `claude/project-thread-8q7ksu`, head e1e5db9, **PR 43 (draft)**
  https://github.com/hugochinchilla/esp32-os/pull/43. Not merged. Only the
  board thread merges, after a board check. The Claude GitHub App is not
  installed on the repo, so PR events do not reach any session: check the
  PR by hand.
- PR 43 holds two pieces, in order. Up to 435bd25 is item 26 (can merge
  alone). def4089 and e1e5db9 are item 27's first step.
- Items 37 and the first two steps of 26 are merged and checked on the
  board (PR 36, PR 42; main was 7d7b21d when last seen).
- Board turn asked of the board thread (thread "Build Chinchilla window
  manager") for PR 43, with a list of checks. **No result yet.** Item 26's
  stack figures (8 KB for Editor, File manager, Picture viewer, Task
  Manager, Defragmenter; 4 KB for Culebra, Cactus Patch, About, Welcome,
  Widget gallery) are guesses until `dbg.py tasks` is read on the board.
- The PC gate passes (44 tests) and the firmware compiles and links
  (0x17b250 of 0x1f0000). **Nothing of PR 43 has run on the board.**
- Build after pulling this branch: delete `firmware/os/sdkconfig` (the
  defaults changed), run `tools/setup-cloud-idf.sh` once in a cloud
  container (it now also fetches the ELF loader), then the commands in
  `CLAUDE.md`.

## Board results so far for PR 43 (the board thread, 2026-11-17, turn cut short)

Measured on 60113e5 and 32e202e. 435bd25, def4089 and later were never
built or run there: **item 27 has no board result at all.** Not merged.

- **Fails: a File manager window on a folder of 50 or more files does not
  open** (Run `C:\m50`, `C:\m81`: nothing, no crash, no log line); 10 and
  30 files open. `main` opened an 80-file folder earlier the same day; not
  compared again. Not reproduced on the PC: `test_os` now opens a folder of
  81 files with the File manager on a real thread, also with a 1 ms limit
  so that the open is late, and the window comes. **Theory, not checked:**
  listing is slow on LittleFS (each entry is asked for by index), 50 files
  take more than the two-second limit, the File manager is marked late in
  the middle of opening, and a late program only gets 10 ms of OS calls a
  round at the idle priority, so the window would come after a very long
  time or look as if it never does. To check on the board: time the same
  folder on `main`; on the branch watch `dbg.py tasks` (the File manager at
  priority 0) and wait a minute. If so, the fixes to choose from are a
  listing that does not cost n squared, a longer limit while a window is
  being opened, or more turns for a late program. A design choice: ask Hugo.
- Stack never used, in bytes: Editor 4,776 (300 KB save, Save As); File
  manager 4,888 to 5,416; Picture viewer 4,548 to 4,712; Task Manager
  3,636; Defragmenter 6,296; Cactus Patch 1,528 of 4 KB; About 2,632;
  Welcome 2,788; Widget gallery 2,820; Culebra 1,396. So the 8 KB programs
  use at most about 4.6 KB: 6 KB would do, with the margin Culebra has.
- Works: Run with a file and a folder; Ctrl+Alt+Del; End Task; `fault
  hang` and `slow --pid` on Editor and File manager; a desktop icon opens
  in 0.32 s with the File manager hung; eight tasks and a ninth program;
  the three new programs; no watchdog lines; no misses but the debug
  link's.
- Help > About opens About as a separate process. That is as built
  (`in_program` makes it a child of whoever asks; the board saw it as its
  own process, which is right. Its parent was not read).
- Odd, unconfirmed: after 15 Editor rounds three Editor tasks were left
  (25,680 bytes of internal RAM), gone later; possibly the script. Worth a
  check: `Runner` gives a task back only after its thread has left the
  loop. The debug link's window list answers "too many windows" at about
  ten windows.
- Not done: File manager rename, Trash, drag; the viewer with PNG and
  JPEG; cost against `main`; everything from 435bd25 on.

## Hugo's decisions (he writes Spanish; reply in Spanish)

- Item 37: drawing by calls, now. Merged.
- Item 26: the server waits for the program when it paints; cost of a 4 KB
  stack accepted; **the programs get tasks, the dialogs and settings tools
  do not** ("Los programas, no los diálogos"). Not decided by him, the
  agent's choices to confirm: the two-second limit, keys and clicks dropped
  while a program is late, the Terminal's window staying on the UI task,
  the stack figures.
- Item 27: **native code loaded from C:** (WebAssembly, an interpreted
  language and nothing were the others), and programs reach the OS calls
  **by name** (a table of calls by number was the other option).
- Standing instruction: keep going through the backlog, my judgement on
  order and parallelism, at most four things active at once. He wants to
  be asked about design choices (cards with options and a recommendation).
  Unanswered earlier: add an "exclusive screen mode for games" (his Doom
  idea) to the backlog; kill on `sh` closing the Terminal; serial and
  telnet as sessions.

## Item 26, programs on their own tasks

Story: `docs/journal/2026-11-17-program-tasks.md`; decisions:
`docs/DECISIONS.md` "Programs on tasks of their own".

- Done: `core/prog` runner, the two-second limit and "not responding",
  `in_program` / `in_process` as the one way into a program, the answer to
  a question going back to who asked, `App::stack`, every program but the
  Terminal on a task (PC only past Culebra).
- Next: the board check of PR 43 (stack high-water marks, memory, repaint
  cost, `dbg.py fault hang` and `slow` on Editor and File manager, eight
  tasks then a ninth program, `dbg.py misses`); trim the stack figures
  from it; fix whatever it finds. If a figure alone is wrong the board
  thread will send the high-water marks.
- Then: the queue of events (also the last part of item 37): programs get
  events through a queue instead of the hand-over, so a late program loses
  none (today events missed while late are lost, including a job's result).
  This is a design choice for Hugo: put options to him first.
- Known limits: a Low-priority job busy more than 5 s trips the IDF task
  watchdog; programs that call each other's code directly
  (`app::program<T>` in the tests' accessors) are only safe on the PC.

## Item 27, loaded programs

Story: `docs/journal/2026-11-17-loaded-programs.md`.

- Done: `ports::Loader`, `shell::run_file` (`core/src/shell/loaded.cpp`),
  Espressif `elf_loader` 2.1.0 on the board
  (`firmware/os/main/elf_programs.hpp`), the PC's `dlopen` for tests
  (`host/dl_loader.hpp`, `host/tests/test_loaded.cpp`),
  `tools/gen_exports.py` (the table of names, from the core library at
  build time), `tools/build_programs.sh`, `programs/hello`.
- Next: **the board proof**: does code loaded into PSRAM run with
  `XIP_FROM_PSRAM` and memory protection on? Checks are in the message to
  the board thread. If it panics, the loader's log lines say where; the
  likely trouble is the cache and memory-protection set-up, not our code.
- Then, in this order of value: memory of a loaded program counted as its
  process's; a task of its own for a loaded program (the runner takes a pid,
  so this is a rule or a mark in the file: ask Hugo); an icon and a Programs
  menu entry for programs found in a folder; moving built-in programs out of
  the firmware into files (the point of the whole item); Doom, for which he
  wants an exclusive screen mode (direct framebuffer 640x480, RRRGGGBB).
- Known limits: no protection (a bad pointer restarts the board); 1 MB a
  file, eight loaded at a time; the loader's own tables of libc and ESP-IDF
  are off, so a program can only use what `gen_exports.py` lists.

## Remaining backlog (not done in `docs/BACKLOG.md`)

- 26 and 27 above; 37 left: events through a queue (with 26).
- 47: the PC test `dbg_tool` fails now and then under parallel load.
- 21: the window manager split into server and shell (the process part is
  done; see the row for what is left).
- 33: Chinchilla in a browser is built and tested; not published yet.
- 46: the BIOS stage, on `claude/project-thread-y7w3uh` (another thread).
- Look at 37 again at the end of the backlog (Hugo's decision).

## Habits that cost time when forgotten

- After `git merge`, check for conflict markers **before** `git add -A`
  (`tools/gate.sh` now rejects markers). Merge `main` before each board pin.
- Commit pattern: `tools/gate.sh 2>&1 | tail -2` must say "gate: all
  passed" before committing and pushing.
- A message to the board thread is at most 2000 characters; the rule given
  to it: measurably worse or anything hangs, do not merge, send figures.
- Every piece of work: a journal entry, the DECISIONS section, the BACKLOG
  row, `docs/os-calls.md` if a call changed. Say that firmware has not run
  on the board until it has.
