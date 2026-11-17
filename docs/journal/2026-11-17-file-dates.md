# File dates

Backlog item 24. Branch `claude/project-thread-8q7ksu`.

## 1. What was asked

The backlog line is "File dates". The architecture review listed it as
"Files have a name and a size, no modified time". The shell's notes said
"no `date` yet (item 24)". The owner's instruction for this run of work is
"ve haciendo, no pares hasta terminar el backlog, dejo a tu criterio", so
the choices are the agent's and are marked "to confirm" in
`docs/DECISIONS.md`.

## 2. What was found

- A folder entry (`ports::DirEntry`) was a name, a size and "is a folder".
- The board's C: is LittleFS behind ESP-IDF's POSIX layer, and that layer
  already keeps a "last written" time for every file (`st_mtime`, set from
  the system clock when a file is opened for writing). Nothing read it.
- The board has no clock that runs without power. Its system clock counts
  from 1970 at every start and is set from the network (SNTP) once Wi-Fi is
  up; the taskbar clock is set from the same moment. So a file written
  before the network answers is stamped "1970".
- The RAM disk (the PC builds and the tests; the board has none mounted)
  kept no time at all.
- The time zone and the summer-time rule already exist in the portable
  code (`net::local_time`), used for the taskbar clock.

## 3. The options, and what was chosen

**Which dates.**

- *Chosen: one, "last written".* It is the one the flash file system
  already keeps, and the one people look at.
- Not chosen: *created and last read as well.* LittleFS does not keep
  them; "last read" would mean a write to flash for every read.

**What is stored.**

- *Chosen: seconds since 1970, UTC*, turned into local time only when
  shown. A file does not change its date when the zone or summer time does.
- Not chosen: *local time on disk, as FAT does.* Simpler to print, wrong
  twice a year.

**A file written when the machine does not know the time.**

- *Chosen: no date.* A date before 2000 from the flash file system is
  taken as "not known" and shown as a blank. It is the truth, and it does
  not sort or read as a real day.
- Not chosen: *show 1970.* It looks like a fault.
- Not chosen: *stamp it later, when the time arrives.* The file system
  would have to remember which files to go back to.
- Not chosen here: *keep the last known time in flash and start from it.*
  It would give most files a plausible date, but one that can be days
  wrong without saying so. Worth asking the owner; a clock chip on the
  hat board would be the real answer.

**Where the time comes from.**

- *Chosen: one more question for the System port*, `utc_time()`: the
  board answers from its system clock once the network has set it. The RAM
  disk asks it when a file is written; the flash file system uses the same
  clock by itself.

**Where it is shown.**

- `dir` and `ls -l`: a first column, `2026-11-17 14:05`. Year first, so it
  reads the same to everyone and sorts as text.
- The File manager's status bar, for the chosen file: `209 bytes,
  2026-11-17 14:05`. It used to say "4 objects" whatever was chosen.
- `date` (and `time`): `Tue 17 Nov 2026 14:05:09`, or that the machine
  does not know yet.
- Not chosen now: *a Details view in the File manager* with columns to
  sort by. It is the natural home for dates, and a piece of work of its
  own (a new view, column headers, sorting).
- Not chosen: *the day/month/year order of Spain.* The interface is in
  English and the year-first form cannot be misread; easy to change in one
  function (`sh::date_text`) if the owner prefers it.

## 4. What was built

- `ports::DirEntry::modified`; `ports::System::utc_time()`.
- `fs::PosixVolume` (the board's flash disk, and a folder of the PC): the
  file system's date, "not known" if before 2000.
- `fs::MemoryVolume`: a date per file and folder, set when it is made,
  emptied, or closed after a write. A new name keeps it; the safe save
  (temporary file, then its name) carries the new one over. Each entry of
  its table is 4 bytes bigger (1 KB in all).
- `sh::date_text()`; the date column in `dir`; `date`; the zone in the
  session's environment (`sh::Env::zone`, the default zone if not given).
- The File manager's status bar.
- The board's `EspSystem::utc_time()`; the PC programs use the PC's clock.

Tested on the PC: the RAM disk's dates (made, written, saved over, read,
renamed, changed in the middle, no clock); a folder of the PC (the same
code as the flash disk), with a file set back to 1990 by hand; `dir`, `ls
-l`, `date` in winter and summer and in another zone, copy, rename and
`>>`; a new reference picture of the File manager with a file chosen
(`docs/look/dates/`). Thirty reference pictures changed: the status bar
where a file is chosen, and "KB free" by one where the RAM disk's table
grew. 31 tests pass. The firmware compiles and links in the cloud; it has
not run on the board.

## 5. What is left

- To check on the board: after Wi-Fi has connected, save a file from the
  Editor, then `dir` shows today's date and the right hour, and the File
  manager's status bar the same; a file saved before Wi-Fi connects shows
  no date; the sample files (written at first start) show none.
- A Details view in the File manager, sorted by name, size or date.
- A Date/Time panel (the zone is still set in the code).
- Copying a file gives the copy today's date, as on Unix; Windows keeps
  the original's. Not asked; one line to change in the RAM disk, not
  possible through the POSIX layer without `utime`.
