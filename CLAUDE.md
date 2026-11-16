# Notes for agents

Read `docs/DECISIONS.md` before proposing or changing anything. It records what
has been decided, what was rejected and why, and what is still open. Update it
in the same change whenever a decision is made or reversed.

- Work runs in parallel threads since 2026-11-16 (the owner's request). Which
  rule applies depends on the thread:
  - The thread that drives the owner's laptop and the board commits directly to
    `main`, as before.
  - Every other thread works on its own branch, one feature per branch, tested
    on the PC only, and opens a pull request. It never pushes to `main`. Keep
    changes to shared files (`docs/DECISIONS.md`, the program table in
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
  ctest --test-dir build/host`). The cloud container has no ESP-IDF, so say
  when firmware code has not been compiled.
- Do not add Microsoft icons, fonts or bitmaps. All art must be original or
  freely licensed.
