# Notes for agents

Read `docs/DECISIONS.md` before proposing or changing anything. It is an
index: what always applies, then one line per decision. Read it whole, then
open only the files in `docs/decisions/` that touch your work; do not read
them all. Whenever a decision is made or reversed, record it in the same
change as that file's "Recording a decision" says: a file of its own in
`docs/decisions/` (or an edit to the one it changes), then
`tools/decisions_index.py` to rewrite the list. Do not add decisions to
`docs/DECISIONS.md` itself.

- Language (the owner's rule, 2026-11-17): code, comments, commit messages and
  all documentation in the repo are written in English, whatever language the
  owner uses in conversation. Reply to the owner in the language he writes in.
- Work runs in parallel threads since 2026-11-16 (the owner's request). Which
  rule applies depends on the thread:
  - The thread that drives the owner's laptop and the board commits directly to
    `main`, as before.
  - Every other thread works on its own branch, one feature per branch, tested
    on the PC only, and opens a pull request. It never pushes to `main` and
    never merges its own pull request: the laptop-and-board thread merges each
    branch into `main` once it has passed on the board (the owner's decision). Keep
    changes to shared files (the program table in
    `core/shell/`, CMake lists) small and additive so branches merge cleanly.
  - There is one board. A branch that needs a real-board check asks the
    coordinator for a turn; the laptop thread checks the branch out in a git
    worktree, flashes it, runs the checks and reflashes `main`. One board job
    at a time.
- The owner wants to be asked about design choices, not have them made for him.
  Present options with a recommendation.
- Hardware facts (pin map, resistor values) live in `docs/hardware/README.md`.
  The firmware pin definitions must match it.
- Firmware targets ESP-IDF v5.5 on ESP32-S3 with octal PSRAM. A GitHub Actions
  workflow that builds it is in `ci/github-build-workflow.yml`; it is not active
  until the owner copies it to `.github/workflows/`, because agent sessions
  cannot push workflow files.
- Portable code goes in `core/` and may not include ESP-IDF headers; hardware
  code goes in adapters under `firmware/os/main/`. Run the PC tests before
  committing (`cmake -S host -B build/host && cmake --build build/host &&
  ctest --test-dir build/host`). The cloud container has no ESP-IDF until
  `tools/setup-cloud-idf.sh` installs it (compile and link only), so say
  when firmware code has not been compiled, and always that it has not run.
- Do not add Microsoft icons, fonts or bitmaps. All art must be original or
  freely licensed.
- Keep the written record (the owner's request, 2026-11-16: he is filming the
  project and uses it to tell the story). Every piece of work adds an entry to
  `docs/journal/` in the same change, as `docs/journal/README.md` describes:
  what was asked, the options considered, what was chosen and why, why the
  others were not, what was built and how it was tested. Update the item's
  status in `docs/BACKLOG.md` too.
- A new dialog or settings tool is a program of its own, not a part of the
  desktop (the owner's rule, 2026-11-17;
  `docs/decisions/dialogs-are-programs.md`): its own process row, its own file, no reach
  into the desktop's private variables, and no memory kept while it is closed.
- Windows are made when they open and destroyed when they close (backlog
  items 19 and 34, `docs/journal/2026-11-17-program-lifecycle.md`). Do not add
  a `static` window object: use `make_window`, `one` or `open_one` from
  `core/src/shell/internal.hpp`, give back in the destructor what the window
  asked the Memory port for, and keep what must outlive a close in the
  program's settings file. Do not keep a pointer or reference to a window
  across an event unless something clears it when the window is destroyed.
- The programs are files, not part of the firmware (the owner's rule,
  2026-11-19; `docs/decisions/programs-out-of-the-firmware.md`): a new
  program is a folder in `programs/` with `APP_INFO` and `APP_MAIN`, its
  pictures in its own `art.hpp`, and no row in `core/src/shell/apps.cpp`.
  It is the same source on the board, in the Linux program and in the web
  page; do not add a form of a program for one machine. Only what the
  desktop cannot do without stays in the firmware (the Task Manager, the
  settings tools, About, Welcome). The PC test `program_exports` fails when
  a program asks for a name the firmware does not export
  (`tools/gen_exports.py`).
- Programs ask the system through the OS calls (`core/os/os.hpp`, listed in
  `docs/os-calls.md`; backlog item 20). A new program uses only those and
  goes on the `MOVED` list of `tools/check_os_calls.py`; if a call is
  missing, add it there rather than reaching for `manager()` or `storage()`.
- The desktop has themes (backlog item 47, `core/gfx/theme.hpp`). Code that
  draws a part of the user interface asks the theme for its colours
  (`gfx::face()`, `ink()`, `paper()`, `dim()`, `gfx::theme().select`) and
  its edges (`raised`, `sunken`, `button`, `dent`); it does not name
  `gfx::kGrey`, `kBlack` or `kWhite`, which are for drawings that keep
  their colours. Look at a new window in every theme.
- No backward compatibility (the owner's rule, 2026-11-17): there are no
  releases and no users, and everything on the board may be wiped at any
  time, C: included. When a file format or a setting changes, read only the
  new form and have the board's copy rewritten; do not keep the old form
  understood. The saved Wi-Fi networks are the one thing to leave alone
  where that costs nothing; if they must go, ask the owner for the
  credentials.
- After board checks run `tools/dbg.py misses`: a missed video refill not
  caused by the debug link is a failure to chase, not noise. Do not hold a
  lock or mask interrupts for more than about 100 us, and do not call
  `uxTaskGetSystemState` outside the debug link's Tasks request
  (`docs/journal/2026-11-17-refills.md`).
