# NASA photographs that fill the screen

2026-11-20. Backlog item 85. Decision: `docs/decisions/nasa-backgrounds.md`.

## What was asked

With the GIF decoder (item 84) the owner asked to "extend the gallery of
photographs that come by default in C: with some image from NASA at 800x600
and at 640x480, so that later we can use them as a desktop background", and
said which he remembered: a background of the 1990s that was a photograph
of the Mir space station, and views of the Earth.

## What was found

- The cloud machine could reach nothing at NASA when the work began; the
  owner allowed `images-api.nasa.gov` and `images-assets.nasa.gov` at
  midday.
- The NASA Image and Video Library answers a search with records that have
  an id, a title, a date and a description, and each picture is there at
  several sizes. 915 records match "mir space station shuttle".
- The two NASA photographs the drive already had (item 21) are small: the
  Earthrise of Apollo 8 at 640x480 and the Blue Marble at 320x320.

## The options

Thirty records were looked at as thumbnails; fourteen were cropped to 4:3
at 800x600 and shown to the owner on one sheet, numbered: seven of Mir
seen from the Shuttle, seven of the Earth. The agent recommended six (1,
4, 5, 8, 10, 14). He picked seven: 1, 4, 6, 8, 10, 11, 12.

Where the two sizes go was asked on a card: a folder for each size
(recommended, so that a chooser of backgrounds could show the folder of the
screen in use), all in `C:\pictures` with the size in the name, or a folder
`space`. He chose the size in the name.

## What was built

- `third_party/samples/`: the six originals that were not there yet, at
  the library's "medium" size (1280 pixels wide), and their sources in the
  README. The Blue Marble's original was there already.
- `tools/gen_photos.py`: `BACKGROUNDS`, a name, an original and a 4:3 crop
  each, made at every size of `SCREENS`. The crops keep Mir whole in the
  one where it sat at the edge, cut a caption strip off the bottom of the
  station's photograph, and put the Blue Marble in the middle with black
  at its sides.
- Fourteen JPEGs at quality 85, 31 to 107 KB each, 891 KB together.
- `tools/flash.py` copies them to the board with the other samples.
  `fs::add_backgrounds` puts them on a new drive of the PC program and of
  the web page. The tests do not get them: their drives are of 1 or 2 MB,
  and the first try, which gave them to every new drive, failed nine tests
  on a full drive.
- `mascot blinks.gif`, the animation promised with item 84: the mascot,
  three seconds looking and 150 ms with its eyes shut, the second picture
  a patch of 64x8. 1,017 bytes. It is among the small samples, so every
  drive has it.
- `core/src/fs/sample_pictures.cpp`, the generated file the PC programs
  are built from, is 5 MB of text now (it was 757 KB).

## How it was tested

- Each crop looked at after `gen_photos.py` made it, all seven on a sheet,
  and the GIF's pictures and delays read back with Pillow (3000 and 150 ms).
- PC tests 59 of 59. 49 reference pictures changed and were looked at: the
  pictures folder and the Open dialog show the GIF, and the free space in
  the status bars is 1 KB less.
- The flash tool's test counts the files it would send: the programs, 6
  small pictures, 14 photographs.
- Not on the board: nothing that runs there changed. The photographs have
  not been seen in the board's 256 colours; a board turn opens them in the
  Picture viewer.

## What is left

- Using one as the desktop's background, and choosing it: a later item.
- How they look on the monitor, dithered to the board's colours, is the
  owner's to judge; a photograph he does not like there can be swapped.
