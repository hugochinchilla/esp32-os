# The flash drive and program settings

## What was asked

Drive C: was a RAM disk, lost at every restart. It moved to the flash. Then,
about where programs keep their settings, the owner wrote:

> for each program settings I would like something linux style, a hidden dir
> (~/.config) where each app stores config as a plain text file (.ini, .toml,
> .yaml)

> not a registry like windows please

## What was found

- The flash is 16 MB. The app partition was made 3.94 MB (0x10000 to
  0x400000) and the rest, 12 MB from 0x400000, became the `storage`
  partition.
- Settings were few and lived nowhere in particular (the viewer's dither, Show
  hidden files).

## Options considered

- File system: LittleFS (wear levelling, power-cut safe, a maintained ESP-IDF
  component) against FAT with wear levelling. LittleFS was chosen
  (joltwallet/littlefs 1.23.1).
- Settings format: INI, TOML or YAML. INI was chosen: the simplest to parse in
  a few hundred lines and to edit by hand in the Editor. A central registry
  was rejected by the owner.

## What was chosen and why

Each program keeps its settings in `C:\.config\<program>.ini` (`[section]`,
`key = value`, `;` and `#` comments). Names that start with a dot are hidden
in the File manager unless View > Show hidden files is on. `fs::Ini` keeps
comments and keys it does not know, and saves through a `.tmp` file so a cut
in the middle leaves a readable copy.

## What was built and tested

The LittleFS adapter in `firmware/os/main/`, `core/src/fs/ini.cpp`, settings
for boot, network, viewer, files and screensaver (`docs/settings.md`). On the
board: mounting takes 6 to 11 ms; 12 MB, 11 MB free with the samples; a 60 KB
file goes onto the flash in 3.0 s through the debug link (the link is the
limit); the Editor saves a 293 KB file in 3.3 s (about 90 KB/s, the screen
waits meanwhile). A file put on the drive, a file saved from the Editor, the
viewer's dithering and Show hidden files all survive a restart.

## What was learned

Writing the flash shook the picture; see
[the flash-write shake](2026-11-16-flash-write-shake.md).

## What is left

Saves still make the screen wait: backlog items 6 (atomic saves) and 6b (the
hourglass).
