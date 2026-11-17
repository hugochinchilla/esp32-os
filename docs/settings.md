# Program settings

Each program keeps its own settings in a plain text file on C:, in a hidden
folder, the way Linux programs use `~/.config` (decided by the owner; there is
no central registry):

    C:\.config\<program>.ini

Names that start with a dot are hidden in the File manager and the file
dialogs unless View > Show hidden files is ticked (kept in
`C:\.config\files.ini`). The files can be opened in the Editor.

Format (INI):

    ; a comment (# works too)
    [view]
    dither = floyd-steinberg

- `[section]` headers; keys before the first section belong to no section.
- `key = value`, spaces around both ignored; section and key names ignore case.
- Values are text; numbers are written in decimal, true/false for yes/no
  (yes/no, on/off and 1/0 are read too).
- Values are stored by name, not by number (for example the dithering by its
  name), so files stay readable and survive changes to menus.

Programs use `fs::Ini` (`core/include/core/fs/ini.hpp`) through
`shell::load_settings` and `shell::save_settings`. Changing a value rewrites
only its line: comments, blank lines and keys the program does not know are
kept. A missing or damaged file gives the program's defaults. Saving writes a
hidden temporary file (`.~<program>.ini.tmp`) and then gives it the real name
in one step, so a power cut leaves the old settings or the new ones (see
`Storage::begin_save`).

Files so far:

| File | Section | Keys |
| --- | --- | --- |
| `viewer.ini` | `[view]` | `dither`: none, bayer2, bayer4, bayer8, blue-noise, floyd-steinberg, floyd-serpentine, atkinson, jarvis-judice-ninke, stucki, sierra-lite |
| `files.ini` | `[view]` | `show_hidden`: true or false |
| `screensaver.ini` | `[saver]` | `saver`: none, starfield, chinchillas, curves. `wait_minutes`: 1 to 99 |
| `boot.ini` | `[boot]` | `fastboot`: true skips the start-up screens and the memory test (the other checks still run and are logged), default false. `monitor_wait_ms`: when the self-test's lines start appearing, counted from the start of the video, so a monitor that is slow to show a picture shows them, 0 to 30000, default 2500 (0: at once). `selftest_pace`: how slowly the lines and the memory count appear, in percent, 0 to 400, default 130. `selftest_hold_s`: how long the finished self-test stays, counting down under "Press any key to continue", 0 to 60, default 5 (0: no countdown, a short pause). `splash_ms`: how long the splash stays, default 0 (not shown at all). A key or mouse button skips to the desktop from any of them. `crash_hold_s`: how long the error screen after a crash counts down before the start goes on by itself, 0 to 600, default 15 (0: it stays until a key or mouse button) |
| `boot.ini` | `[watchdog]` | `timeout_s`: how long the desktop's loop may give no sign of life before the machine writes a crash report and restarts, 5 to 600, default 15; 0 switches the watchdog off. The report is added to `C:\crash.txt` at the next start |
| `desktop.ini` | `[welcome]` | `created`: true once welcome.txt has been put in C:\Documents\Desktop, so a deleted copy does not come back; set it to false to get it again |
| `desktop.ini` | `[documents]` | `moved`: true once an old C:\docs has been moved into C:\Documents (done by itself at start-up) |
| `desktop.ini` | `[background]` | `colour`: chinchilla-blue (default), teal, sea-green, forest, navy, purple, plum, brick, olive, sand, sky, lilac, rose, silver, slate, black. `pattern`: none (default), dots, diagonal, grid, bricks, weave, diamonds, waves, zigzag, crosses. Set in the Display dialog |
| `culebra.ini` | `[game]` | `best`: the best score. `speed`: slow, normal (default) or fast. Written when Culebra closes, if either changed |
| `cactus.ini` | `[game]` | `size`: small (default), medium or large, the patch the next game starts with. Written when Cactus Patch closes, if it changed |
| `network.ini` | `[wifi]` | `tx_power_dbm`: the radio's top transmit power, 2 to 20, default 2 (higher values put lines on the picture while connecting). `enabled`: false leaves the radio off, default true |

A one-shot fast start for debugging: `tools/dbg.py reboot --fast`, or
`tools/dbg.py boot fast-next` before flashing, makes the next start behave as
`fastboot = true` once (no self-test screen, no countdown, no monitor wait, no
splash, no memory test) and logs "one-shot fast boot". boot.ini is not
changed; the start after that is normal again. The flag is kept in NVS.
