# GIF pictures

Backlog items 84 and 85. Branch `claude/project-thread-2defcx`. Follows
[Pictures in Vizcacha](2026-11-18-browser-pictures.md), where GIF was left
out and stayed a box.

## What was asked

The owner, 2026-11-20:

> Empecemos a trabajar en el decodificador de GIF para que tanto el file viewer como el navegador soporten mostrar GIFs y extendamos la galería de fotos que vienen por defecto en C con alguna imagen de la NASA en 800x600 y en 640x480 para que más adelante podamos usarlas como fondo de pantalla. A mí me gustaba mucho en Windows 95 había un fondo de pantalla que era una fotografía de la estación espacial Mir, aparte de algunas así de la vista de la Tierra.

Two pieces of work: a GIF decoder (item 84, this entry) and more NASA
photographs in C:\pictures at the two screen sizes (item 85, not started:
see "What is left").

## What was found

- `core/src/image/` has one `Decoder` and a `FormatDecoder` behind it for
  each of BMP, PNG and JPEG. A format gives one row of RGB at a time, top
  to bottom, takes its memory from one 384 KB pool and reads the file
  through a 1 KB buffer. The kind of file is told by its first two bytes.
- The Picture viewer and Vizcacha both drive that `Decoder` from a timer, a
  few rows per turn, into the same `Converter` (fitting and dithering). So
  a fourth format behind `Decoder` reaches both programs with no change to
  either. Vizcacha does not look at the address or the type the server
  gives to decide what a picture is.
- The File manager already drew `.gif` files with the picture icon
  (`listing.cpp`), but a double click opened nothing: `program_kind` in
  `file_manager.cpp` did not list it. The desktop's own icons did not list
  it either.
- A GIF is not top to bottom by nature in one case: an interlaced file
  stores rows 0, 8, 16..., then 4, 12..., then 2, 6..., then the odd ones.
  The second row of the picture is in the last half of the data.
- FrogFind's logo, the picture on the browser's benchmark page, is a GIF.

## The options

Whether animations move was put to the owner on a card:

| | What | Cost |
| --- | --- | --- |
| **First picture now, animation after** (recommended) | GIFs are shown still in both programs; animation becomes a backlog item on the same decoder | This branch |
| Animated at once | The viewer and the pages play animations | Much more: each picture of an animation is a patch laid on the one before, so the whole screen of the GIF has to be kept, the pictures kept or decoded again and again, and it wants measuring on the board |
| Never animated | Still pictures only, and nothing in the backlog | This branch |

Not answered when this was written. The work below is what all three need.

For interlaced files, chosen by the agent:

| | What | Cost |
| --- | --- | --- |
| **The whole picture in the pool** (chosen) | Decode all of it into the 384 KB pool, a byte a pixel, then give the rows | A limit of about 370,000 pixels for interlaced files |
| Four decoders at once | One LZW decoder per pass, each at its place in the file | No limit, but 17 KB of tables each, the file read back and forth, a first run over all the data to find where the passes start |
| "Unsupported", as for interlaced PNG | Nothing | Interlaced GIFs were everywhere on the web of the 1990s |

## What was built

- `core/src/image/gif.cpp`: GIF87a and GIF89a. The file's colours or the
  picture's own; 2 to 256 colours; a transparent colour; the picture placed
  on a larger screen; interlacing; extensions before the picture skipped
  (comments, the loop count of an animation). LZW with its tables inside
  the decoder's object (17 KB of the pool), codes up to 12 bits, a full
  table that goes on without a clear code. Integers only.
- `FormatDecoder::prepare()`: a bounded piece of work before a row can be
  given. `Decoder::step` calls it and counts it as a turn, so the callers'
  time slices hold. The GIF decoder uses it to decode an interlaced picture
  4096 pixels at a time; without it the first row of a 640x480 interlaced
  GIF would have been one long stop.
- `.gif` opens in the Picture viewer from the File manager and has the
  picture icon on the desktop.
- `tools/gen_pictures.py` has a GIF writer (its own LZW) and makes 17 test
  GIFs, 16 with the pixels each must decode to: 1, 2, 4 and 8 bits; interlaced,
  one of three rows; 87a; its own colours with extensions in front;
  transparent; placed on a larger screen, plain and interlaced; a screen
  too small; 160x120 pictures that fill the code table, with a clear code,
  without one, and interlaced; an animation of two pictures; and a file
  with no picture, which must say "damaged".

## How it was tested

On the PC only. Not compiled for the board, and not run on it.

- `test_image`: the 16 GIFs decode to exactly the expected pixels. Each of
  the 17 is
  also cut short at 24 places and has bytes changed at random 40 times,
  under the address and undefined-behaviour checks: every decode ends or
  gives an error.
- The test GIFs were written by this project's writer and read by this
  project's decoder, which proves little if both are wrong the same way. So
  they were also opened once with Pillow, by hand: 13 of the 16 gave the
  same pixels. The other three are the ones with screen left uncovered,
  which Pillow paints with the file's background colour and we, like web
  browsers, leave see-through.
- `render_browser_scenes` now serves FrogFind's home page a logo: an
  interlaced GIF with a transparent background, made in the test (a frog's
  face, drawn there). Reference pictures `61-browser-frogfind` and
  `62-browser-typing` changed: the box with the `alt` text is the frog.
- All 58 PC tests pass.

## What was learned

- The format needs very little: the decoder is 320 lines, against 513 for
  JPEG. What takes thought is all at the edges: files whose screen is
  smaller than their picture, tables that fill, rows out of order.

## What is left

- The owner's answer about animation.
- The board: nothing here has run on it. Wanted there: the time to decode a
  GIF against a PNG of the same picture, and `tools/dbg.py misses` after.
- Item 85, the NASA photographs. The cloud machine this was written on may
  only reach `frogfind.com`, so nothing could be fetched from
  `images.nasa.gov`; the owner was asked to allow `images-api.nasa.gov` and
  `images-assets.nasa.gov`. The candidates (Mir seen from the Shuttle, views
  of the Earth) will be shown to him before any goes in.
- No sample GIF is in C:\pictures yet; one comes with item 85, so that the
  folder's reference pictures change once.
