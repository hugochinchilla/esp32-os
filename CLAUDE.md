# Notes for agents

Read `docs/DECISIONS.md` before proposing or changing anything. It records what
has been decided, what was rejected and why, and what is still open. Update it
in the same change whenever a decision is made or reversed.

- The owner wants to be asked about design choices, not have them made for him.
  Present options with a recommendation.
- Hardware facts (pin map, resistor values) live in `docs/hardware/README.md`.
  The firmware pin definitions must match it.
- Firmware targets ESP-IDF v5.5 on ESP32-S3 with octal PSRAM. A GitHub Actions
  workflow that builds it is in `ci/github-build-workflow.yml`; it is not active
  until the owner copies it to `.github/workflows/`, because agent sessions
  cannot push workflow files.
- Do not add Microsoft icons, fonts or bitmaps. All art must be original or
  freely licensed.
