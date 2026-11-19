# The files of C: are copied when the board is flashed

2026-11-19. Backlog item 77. Decision:
[`docs/decisions/files-copied-when-flashing.md`](../decisions/files-copied-when-flashing.md).

## What was asked

The owner, in a list of ideas for later (the others are backlog items 78 to
80):

> replantear el proceso de instalación del firmware inicial, creo que el
> script de flasheado de la placa puede encargarse de copiar las apps al
> disco C: sin necesitar embebeerlas en el firmware primero
>
> y justo este ultimo punto podría resolver el problema de doom, que
> necesita que yo copie el DOOM1.WAD a C:
>
> De momento puedes empezar solo por el problema de sacar los ficheros del
> firmware y copiarlos a C:/ con el script de flsasheo para que doom no
> requiera que yo copie nada a mano

## What was found

- The firmware carried two sets of files as data. The four programs kept in
  files (`programs/`: Hello, Culebra, Cactus Patch, Chess) were built by the
  firmware build, turned into a C array by `tools/gen_bundle.py`
  (`bundle.c`, 92,873 bytes in the report of item 71) and written to
  `C:\Programs` two seconds after the desktop came up
  (`shell::install_programs`, with a stamp in `C:\Programs\installed.txt`).
  The five sample pictures (`core/src/fs/sample_pictures.cpp`, 159,815
  bytes of pictures) were written by `fs::add_samples` when C: was new.
- That the programs ride in the firmware was the owner's choice on
  2026-11-17 ("Dentro del firmware, se instalan"). Copying with `dbg.py`
  was rejected then because a wiped C: loses them. The journal of item 71
  had already named the way out: "the sample photos and the loadable
  programs (252 KB) could be put on C: when the board is flashed".
- There was no flashing script: `idf.py flash` in `firmware/os`, with the
  rule (in the board-turn skill) to read `build/flash_args` first and to
  write only 0x0, 0x8000, 0xf0000 and 0x100000. `tools/flash-bios.sh`
  writes the BIOS.
- The debug link already copies files (`dbg.py put`): 1 KB a request, the
  file opened, written and closed for each. Nobody has timed 4 MB over it.
- The PC programs, the browser version and 14 tests call `fs::add_samples`
  and count on the pictures. Nothing flashes those.
- Doom (backlog 81, another thread, in progress) looks for
  `C:\Programs\DOOM1.WAD`: the shareware data, version 1.9, 4,196,020
  bytes, which may be passed on unchanged but is not freely licensed, so it
  cannot be in the repository.

## The options

How the files travel, put to the owner on a card:

1. **Over the debug link, after the firmware is written, only what
   changed** (recommended). Nothing on C: is lost: settings, documents, the
   Trash. Nothing to install on the laptop. The cost: the system must be
   up to take them, and the WAD is slow the first time (not measured;
   minutes).
2. **A LittleFS image written at 0x700000 with esptool.** Fast and it needs
   no running system, but every flash wipes C:, and the laptop needs a
   tool that builds the image with the same settings as the board's
   LittleFS.
3. **Both**: the image behind `--fresh`, the debug link otherwise. Two
   ways to keep working.

Built on the first while the card waits. The board is flashed many times
a day, and each flash would otherwise take away `network.ini`, the theme,
the places of the icons and whatever the owner was writing.

Smaller choices, the agent's, to confirm:

- The text files of a new C: stay in the firmware (`fs::add_first_files`):
  they are under 1 KB together, and a C: with nothing on it at all is a
  poorer thing to find after a format than one with a readme. `C:\Programs`,
  which `install_programs` used to make, is made by the tool: a first try
  had `add_first_files` make it, and the new folder moved every icon in the
  File manager's scenes of the PC tests (the Trash tests drag `readme.txt`
  from where it used to be).
- `fs::add_samples` (first files and pictures) moved to a file of its own,
  `core/src/fs/samples.cpp`. The firmware does not call it, so the linker
  leaves `sample_pictures.cpp` out of the firmware with no `#if`.
- The list of what was copied is `C:\system\installed.txt`: a mark (16 hex
  digits of the SHA-256), the size and the path of each file. A file is
  sent if the list does not have it with that mark or the folder does not
  have it with that size, so a program the user deleted comes back, as it
  did before. A file in the list that is no longer in the set is deleted
  (a program taken out of `programs/`). What the tool never copied is not
  touched.
- The WAD is in the set when `programs/doom` exists or `--wad FILE` is
  given. Its mark and size are known beforehand, so a board that has it
  costs no download. Otherwise: `--wad`, else the copy in
  `build/downloads/`, else the network (ibiblio's copy in the SliTaz
  sources, then one on GitHub), checked by size and SHA-256 every time. A
  run that leaves the WAD out (`--no-wad`, or a branch without Doom) does
  not delete one that is on the board.
- The tool flashes with esptool from the build's `flasher_args.json` and
  refuses a build that would write anywhere but the four addresses: the
  board-turn rule, kept by the tool. No `erase_flash`, no `nvs`.

## What was built

- `tools/flash.py`: build (`idf.py build`), flash, wait for the desktop,
  copy. `--files-only`, `--no-files`, `--no-build`, `--list`, `--wad`,
  `--no-wad`.
- `dbg.Board.put` takes a `progress` function.
- The firmware build still builds `programs/*` into
  `build/programs/*.app` and checks them against the exports (the target
  `programs`), but makes no `bundle.c`. `tools/gen_bundle.py`,
  `shell::set_bundled_programs`, `shell::install_programs` and the timer
  that ran it are gone.
- `fs::add_first_files` and `fs::add_samples` as above;
  `gen_pictures.SAMPLE_NAMES`.
- `tools/test_flash_sim.py` (ctest `flash_tool`), against the simulated
  board: a board never seen, a second run that sends nothing and reads no
  file, a changed file, a file deleted on the board, a file cut short, a
  file that left the set, `keep`, the user's own file left alone; the
  check of the addresses (nvs, C: and the layout before the BIOS are
  refused); the set from a build folder; a wrong WAD refused.

## How it was tested

- PC: `tools/gate.sh`, 57 tests of 57 passed, `flash_tool` among them.
- The firmware was compiled and linked in the cloud container (ESP-IDF
  v5.5), not run. `esp32-os.bin`: 1,949,264 bytes on `main` (b303cb7),
  1,695,472 on this branch: 253,792 bytes less, from 62.0 % to 53.9 % of
  the 3 MB slot. `sample_pictures` is not in the link map. The four
  programs are built to `build/programs/` (93,068 bytes of `.app` in all)
  and checked: "368 names; 4 program(s) checked, 0 name(s) missing".
- `tools/flash.py --list` and `flash_plan` on that build: the four
  addresses, the four programs and the five pictures (159,755 bytes).
- The WAD was fetched in the container from the GitHub address (the
  ibiblio one is not reachable from there) and matched the size, the MD5
  the Doom thread gave and the SHA-256 in the tool.
- Not tested: esptool through the tool, the copy over the real serial
  link, how long 4 MB takes, the board with no programs before the first
  copy. Those are the board turn's.

## What was learned

- One folder more on a new drive is not free in the tests: the scenes
  know where the icons are.
- "Do not carry it" needed no switch in the code: a function in a file of
  its own that the firmware never calls is enough for the linker.

## What is left

- The board turn: flash with the tool, time the copies, check that a
  second run sends nothing and that the programs open.
- An update over the network does not bring the programs (the decision
  file says what that means). The software store, backlog 78, is the place
  for it.
- Doom (backlog 81) merges onto this: `programs/doom` puts the WAD in the
  set with no further change.

## The owner's answer (2026-11-19)

On the card he chose the first option, "Copiar por el enlace de
depuración": what was built stands. The smaller choices listed above are
still the agent's, to confirm.

## What the board found (batch 26, b48bd76), and the fix

The tool waited for the desktop with `dbg.wait_for_desktop`, which waits
for a window to be open. The simulator starts with two windows; the board
starts with none, so the tool gave up after 90 s ("the desktop did not
start") and copied nothing unless someone opened a window first. The PC
test could not see it for that reason. Now `flash.wait_for_drive` waits
for C: to answer over the debug link and for the system to have been up 3
s, and never asks for the windows; the test has a board that would fail
if it did.

The rest passed with a window open: only the four addresses written
(1,726,624 bytes in 25.4 s), 9 files (252,771 bytes) copied in 17.7 s, a
second run 0 files in 1.5 s, a deleted `hello.app` copied again, programs
and pictures open, a start with no programs on C: is fine. 4,196,020
bytes with `dbg.py put`: 339 s (12 KB/s), read back identical in 184 s, no
refill missed. So the WAD costs five to six minutes, once. Each 1 KB
request opens, writes and closes the file; keeping it open between
requests, or larger requests, is where that time would come down. Not
done here.

## On the board, the second turn (2026-11-19, 0c41aa7)

`tools/flash.py --no-build` straight after the build, with the desktop empty
and nobody touching the board: esptool wrote the four addresses (the OS,
1,726,624 bytes, in 25.4 s), the board restarted, and the first file was copied
7.1 s after the hard reset; four files were new that time (the programs of the
build; the pictures were already there) and the run ended at 47 s in all. After
`dbg.py rm` of `hello.app`, `--files-only` copied one file in 3.4 s and a second
run copied none; with `dbg.py reboot` and `--files-only` started at once, the
tool waited for the board (7.1 s) and reported 0 copied with no error.
`dbg.py misses` 0; Hello opened from Start > Programs. The owner chose this
design and the PR was merged.

Also seen in the first turn (b48bd76) and left as it was: `dbg.py put` of a
4.2 MB file took 339 s (12 KB/s) and reading it back 184 s, byte-identical.
