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
| First picture now, animation after (recommended) | GIFs are shown still in both programs; animation becomes a backlog item on the same decoder | The first sitting |
| **Animated at once** (chosen) | The viewer and the pages play animations | Much more: each picture of an animation is a patch laid on the one before, so the whole screen of the GIF has to be kept, and it wants measuring on the board |
| Never animated | Still pictures only, and nothing in the backlog | The first sitting |

The still decoder was built and pushed while the card waited (PR 92). The
owner answered an hour and a half later: "Animados desde ya en los dos".

How an animation is kept, chosen by the agent:

| | What | Cost |
| --- | --- | --- |
| **One screen, decoded again each time** (chosen) | The decoder keeps the GIF's screen as RGB and lays each picture on it; the program converts the whole screen to board colours for each picture | Processor time for every picture, for as long as it plays. Memory: the file and 3 bytes a pixel, whatever the length |
| Every picture kept once converted | Decode once, then only copy | Memory by the number of pictures: a banner of 468x60 with 21 pictures is 590 KB against 107 KB. A second set of limits and what to do past them |
| The screen kept in board colours, patches converted alone | Less to convert per picture | A patch shrunk or dithered alone does not join its surroundings: seams. And see-through pixels inside a shrunk patch |

For interlaced still files, chosen by the agent:

| | What | Cost |
| --- | --- | --- |
| **The whole picture in the pool** (chosen) | Decode all of it into the 384 KB pool, a byte a pixel, then give the rows | A limit of about 370,000 pixels for interlaced files |
| Four decoders at once | One LZW decoder per pass, each at its place in the file | No limit, but 17 KB of tables each, the file read back and forth, a first run over all the data to find where the passes start |
| "Unsupported", as for interlaced PNG | Nothing | Interlaced GIFs were everywhere on the web of the 1990s |

## What was built

The decoder, `core/src/image/gif.cpp` (556 lines):

- GIF87a and GIF89a. The file's colours or a picture's own; 2 to 256
  colours; a transparent colour; a picture placed on a larger screen;
  interlacing; extensions skipped. LZW with its tables inside the
  decoder's object (18 KB of the pool), codes up to 12 bits, a full table
  that goes on without a clear code. Integers only.
- `FormatDecoder::prepare()`: a bounded piece of work before a row can be
  given. `Decoder::step` calls it and counts it as a turn, so the callers'
  time slices hold. The GIF decoder uses it to decode 4096 pixels at a
  time ahead of the rows: an interlaced picture, or a picture of an
  animation onto its screen.
- Animations. When the file is opened the decoder passes over the first
  picture's data to see whether another follows (`Info::several`); if so
  and the screen fits the working memory as RGB, it can be played
  (`Info::animated`). `Decoder::next_frame()` goes on to the next picture,
  `frame_ms()` says how long the one shown stays, `more_frames()` whether
  another follows. Times, times round and what a picture leaves behind
  are as [the decision](../decisions/gif-pictures.md) says.
- `Decoder::use_pool()` gives a decoder working memory of another size,
  and `image::animation_pool(w, h)` says how much an animation needs.
- The reader (the 1 KB buffer over the file) was one for the whole system,
  "one picture is decoded at a time". Animations in a page stay open side
  by side, so each decoder now has its own, in its pool.

The Picture viewer:

- `.gif` opens in it from the File manager and has the picture icon on the
  desktop.
- An animation plays: the file stays open, and the timer that decoded the
  picture goes on, starting the next picture when the one shown has stayed
  its time. For a screen larger than the usual pool plays, the viewer
  opens the file again with the memory it needs, up to 800x600.
- The status line says "GIF, 8 colours, animated".

Vizcacha:

- Nothing changed for still GIFs: it tells a picture by its bytes.
- A file of several pictures gets an `Animation`: a copy of the file, a
  decoder with working memory sized to it, and the place of its pixels in
  the page. The page's timer, which used to stop when the page was whole,
  goes on while there is one. One picture is decoded at a time (there is
  one converter) and painted when it is whole; only its rectangle of the
  page is painted again.
- Everything goes back when another page is shown or the window closes.

`image::steady()` picks the dither for a picture that moves.

For measuring on the board, each program writes one line in the system's
log five seconds into an animation: how many pictures it showed and how
much of the time went into decoding them (`dmesg`).

`tools/gen_pictures.py` has a GIF writer (its own LZW) and makes:

- for the tests, 17 still GIFs, 16 with the pixels each must decode to
  (1, 2, 4 and 8 bits; interlaced, one of three rows; 87a; its own colours
  with extensions in front; transparent; placed on a larger screen, plain
  and interlaced; a screen too small; 160x120 pictures that fill the code
  table, with a clear code, without one, and interlaced; the first picture
  of an animation) and one with no picture, which must say "damaged";
- four animations with every picture they must show, the times and the
  times round: patches with see-through pixels, for ever; a picture taken
  away again, one with its own colours, an interlaced patch, three times
  round, on a screen the first picture does not fill; one that plays once
  and whose second picture sticks out of the screen; one of 420x300, too
  large for the usual pool;
- for the board (`bench`), the landscape as a GIF plain and interlaced, a
  spinner of 12 pictures, a 468x60 banner of 21, a 640x480 animation, and
  `gifs.htm`, a page that shows them.

## How it was tested

On the PC only. Not compiled for the board, and not run on it.

- `test_image`: the 16 GIFs decode to exactly the expected pixels. The
  four animations are played picture by picture against the expected
  pixels, with the time of each, to the end of the last time round (a
  little over twice round for "for ever"); the large one first shows its
  first picture and says it cannot be played, then plays with the memory
  `animation_pool()` asks for. Every file is also cut short at 24 places
  and has bytes changed at random 40 times, and what is left of an
  animation is played on, under the address and undefined-behaviour
  checks: every decode ends or gives an error.
- The test GIFs were written by this project's writer and read by this
  project's decoder, which proves little if both are wrong the same way. So
  they were also opened once with Pillow, by hand. The still ones: 13 of
  the 16 gave the same pixels; the other three have screen left uncovered,
  which Pillow paints with the file's background colour and we, like web
  browsers, leave see-through. The animations: the same pictures, with the
  same exception where a patch was taken away again, and one more where a
  picture sticks out of the screen.
- `render_browser_scenes`: FrogFind's logo is now a GIF made in the test
  (`host/tests/test_gif.hpp`), a frog's face on a transparent background,
  interlaced, that blinks: 800 ms open, 150 ms shut. Scenes 61 and 62
  changed (the box with the `alt` text is the frog) and 61a is new (eyes
  shut, 700 ms later). The same file opens from the drive in the Picture
  viewer (74 to 76: open, shut, open again), and at 640x480 (77, 78),
  which needs the larger working memory. The test still ends with every
  byte given back.
- All 58 PC tests pass.

## On the board, the first time

A board turn ran commit `8a704a3` the same morning. It compiled at the
first try (1,737,744 bytes), GIFs showed and moved, memory came back to the
byte after every cycle (free internal 132,759, external 6,006,292), and no
video refill was missed. The figures:

| | Asked | Got |
| --- | --- | --- |
| `landscape640.png`, decoding | | 1,154 ms |
| `landscape640.gif`, decoding | | 710 ms |
| `landscape640i.gif` (interlaced), decoding | | 690 ms |
| `spinner.gif` in the viewer (96x96, every picture whole) | 12.5 a second | 12.4, 17.6 ms of decoding each |
| `banner.gif` in the viewer (468x60, patches) | 10 | 9.9, 29 ms each |
| `landscape640-moving.gif` in the viewer | 2 | 1.8, 220 ms each; maximised 1.4, 290 ms each |
| `gifs.htm` in Vizcacha, four animations | 47.5 | 4.9, 33 ms each |

Three things were reported as wrong.

- **"The banner leaves a trail in Vizcacha": the test file's fault, not the
  browser's.** `banner.gif` was written here with patches that drew the
  block at its new place and did not cover the old one, which was 21
  pixels to the left and 15 pixels outside the patch. Pillow shows the same
  trail of stripes from the same file. The writer now makes each patch
  reach back over where the block was.
- **The page at 5 pictures a second, not 47.5.** Not the decoding as such
  but when it was allowed: the page's timer turned every 30 ms and gave
  8 ms to one animation at a time, so a picture of 33 ms took four turns,
  and four animations waited on each other. And 33 ms was far too much for
  a patch of 52x40: the whole screen of the GIF was converted for every
  picture.
- **The 640x480 animation under 2 a second**, for the same last reason:
  307,200 pixels converted to move 25,600.

What was changed:

- **Only what a picture changes is converted and painted.** After
  `next_frame()` the decoder says which part of the screen may differ
  (`Decoder::changed`: the new patch, and the old one if it was taken
  away) and gives only the rows asked for (`only_rows`).
  `Converter::begin_part` makes again only the result's pixels that part
  reaches, shrunk or not. That works because a pattern dither gives a
  pixel the same colour whatever is around it; it is one more reason for
  animations not to use error diffusion. The banner's 28,080 pixels a
  picture become 2,440, the large animation's 307,200 become 25,600. The
  spinner, whose every picture is the whole 96x96, gains nothing here.
- **A picture shown at its own size is not averaged.** The converter
  summed and divided every pixel even when one source pixel made one
  result pixel. That path now copies the row. This is for every picture,
  moving or not, PNG and JPEG too.
- **While a page has animations its timer turns every 10 ms** with 6 ms of
  decoding a turn, instead of every 30 ms with 8.
- Smaller things: the decoder decodes 1024 pixels ahead per turn, not
  4096 (the longest slice had been 12.8 ms against the 8 asked); the
  screen is cleared a row at a time, not a pixel at a time.

Tested on the PC: a part converted alone gives the same pixels as the
whole picture converted, at the same size and shrunk, for the five pattern
dithers, and the six others refuse; the four test animations played by
their changed parts give the expected pictures, and nothing changes
outside the part the decoder names; every reference picture is as it was,
the frog's included. How much faster the board is, only the board can say:
a second request is with the coordinator.

## What was learned

- The format needs very little. What takes thought is all at the edges:
  files whose screen is smaller than their picture, tables that fill, rows
  out of order, and above all what "the background" means, where the file
  format says one thing and every browser does another.
- Error diffusion and animation do not go together: the errors of the part
  that changes run through the parts that do not. Animations are dithered
  with a pattern instead, and it shows in the reference pictures: the
  frog's flat green has another grain than it had as a still picture.
- Adding scenes in the middle of `render_scenes` moves its clock, and 14
  later pictures change (Culebra's berries, the minute on the taskbar).
  The viewer's animation scenes went to the end of the browser's program
  instead.

## What is left

- The board: nothing here has run on it. Wanted there: the time to decode
  a GIF against a PNG of the same picture, how many pictures a second the
  spinner, the banner and the 640x480 animation really get, what the
  desktop feels like meanwhile, and `tools/dbg.py misses` and `frames`
  after. A request is with the coordinator.
- "Restore to previous" is treated as "leave it". If a file that matters
  uses it, the patch has to be saved before it is drawn.
- A page's animation is decoded even when another window covers it.
- An animation whose every picture is the whole screen (the spinner)
  still costs a whole conversion each time.
- Item 85, the NASA photographs. The cloud machine this was written on may
  only reach `frogfind.com`, so nothing could be fetched from
  `images.nasa.gov`; the owner was asked to allow `images-api.nasa.gov` and
  `images-assets.nasa.gov`. The candidates (Mir seen from the Shuttle, views
  of the Earth) will be shown to him before any goes in.
- No sample GIF is in C:\pictures yet; one comes with item 85, so that the
  folder's reference pictures change once.
