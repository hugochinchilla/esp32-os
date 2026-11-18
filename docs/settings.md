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
| `desktop.ini` | `[background]` | `colour`: steel-blue (default), chinchilla-blue (the only dithered one; the default until 2026-11-17), teal, sea-green, forest, navy, purple, plum, brick, olive, sand, sky, rose, silver, slate, black; or `#RRGGBB` for any other (Display > Other... writes one of the board's 256). `pattern`: none (default), dots, diagonal, grid, bricks, weave, diamonds, waves, zigzag, crosses. Set in the Display dialog |
| `desktop.ini` | `[look]` | `theme`: classic (default), sky, glacier, midnight: the look of windows, buttons, menus and the taskbar (`core/gfx/theme.hpp`). Set in the Display dialog |
| `desktop.ini` | `[icons]` | `snap`: true makes an icon let go on the desktop take the nearest free cell of the grid. Default false. Set from the desktop's menu (right click) |
| `icons.ini` | `[system]`, `[files]` | Where each desktop icon is: `My Board = 214,124`, the corner of its place in pixels. `[system]` by the icon's label, `[files]` by the file's name in C:\Documents\Desktop. Written whole by the desktop when an icon is moved or the icons are realigned; without it the icons are in columns from the top left |
| `culebra.ini` | `[game]` | `best`: the best score. `speed`: slow, normal (default) or fast. Written when Culebra closes, if either changed |
| `cactus.ini` | `[game]` | `size`: small (default), medium or large, the patch the next game starts with. Written when Cactus Patch closes, if it changed |
| `chess.ini` | `[game]` | `level`: 1 to 5, how strong the computer plays (default 3). `side`: white, black or random (default), the side the user takes. Written when a game is started from the "New game" dialog |
| `chess.ini` | `[look]` | `board`: green (default) or brown. `pieces`: contour (default), symbols or classic. `dots`: false hides the marks on the squares a picked piece may go to, default true. Written when chosen in the Options menu |
| `network.ini` | `[wifi]` | `tx_power_dbm`: the radio's top transmit power, 2 to 20, default 20 (on a board without the supply capacitor across 3.3 V, high values put lines on the picture while connecting; 2 removes them). `enabled`: false leaves the radio off, default true |
| `network.ini` | `[update]` | `enabled`: true lets `tools/ota.py` send a new version of the system over the network (port 3232, no password). Default false. Read at start-up |
| `startup.txt` | (lines, not sections) | What init starts, from the top down, a line each as `WHAT:ACTION` (the form of a Unix inittab). WHAT is a part of the system (`desktop`, `taskbar`, `inetd` for the task the network services run on, `telnetd` for the command shell on port 23 with no password, `vncd` for the remote screen on port 5900 and its web page on port 80, `console` for the command shell on the serial port) or a program or a file as Start > Run... takes it. ACTION is `once` (started), `respawn` (started, and started again whenever it ends: the taskbar can be) or `off` (not started). `#` for a comment. The usual file: `desktop:once`, `taskbar:respawn`, `inetd:once`, `telnetd:once`, `vncd:once`, `console:off`. The desktop and its taskbar start even with no line; a service with no line is not started; with `taskbar:once` an ended taskbar stays away until `run taskbar`. Start > Settings > Services changes the `telnetd` and `vncd` lines (`once` or `off`) and nothing else. Written with the usual lines if it is not there. Read at start-up; `service NAME stop` and `service NAME start` in a terminal switch a service until the next start without touching the file |

Since the BIOS (2026-11-17, `firmware/bios`): the self-test and its
countdown are shown by the BIOS, which cannot read `boot.ini` and uses the
built-in defaults until its settings move to NVS (backlog item 46, step 3).
The `[boot]` keys for the self-test then only reach the OS's own screen,
which shows when there is no BIOS in the flash. `splash_ms`, `crash_hold_s`
and `[watchdog]` are the OS's and work as before.

A one-shot fast start for debugging: `tools/dbg.py reboot --fast`, or
`tools/dbg.py boot fast-next` before flashing, makes the next start behave as
`fastboot = true` once (no self-test screen, no countdown, no monitor wait, no
splash, no memory test) and logs "one-shot fast boot". boot.ini is not
changed; the start after that is normal again. The flag is kept in NVS.
