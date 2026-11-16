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
kept. A missing or damaged file gives the program's defaults. Saving writes
`<program>.ini.tmp` first and then puts it in place of the old file; if power
is lost in between, the next start reads the `.tmp` file.

Files so far:

| File | Section | Keys |
| --- | --- | --- |
| `viewer.ini` | `[view]` | `dither`: none, bayer2, bayer4, bayer8, blue-noise, floyd-steinberg, floyd-serpentine, atkinson, jarvis-judice-ninke, stucki, sierra-lite |
| `files.ini` | `[view]` | `show_hidden`: true or false |
| `screensaver.ini` | `[saver]` | `saver`: none, starfield, chinchillas, curves. `wait_minutes`: 1 to 99 |
