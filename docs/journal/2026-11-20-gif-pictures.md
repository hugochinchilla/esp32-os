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

On the PC first; then twice on the board, see below.

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

## On the board, the second time

Commit `e71b563`, the same day. It built (1,739,088 bytes). No trail on
the banner, in the viewer or in the page; still PNG, JPEG and GIF pictures
right when fitted and at Actual size; nothing painted outside the page
while scrolling under moving pictures; memory back to within 20 bytes
after 5 cycles; no refill missed.

| | Asked | First turn | Second turn |
| --- | --- | --- | --- |
| `gifs.htm` in Vizcacha, four animations | 47.5 a second | 4.9 | 34.1, 8.9 ms of decoding each |
| `spinner.gif` in the viewer | 12.5 | 12.4, 17.6 ms each | 12.0, 11.6 ms each |
| `banner.gif` in the viewer | 10 | 9.9, 29 ms each | 9.6, 8.1 ms each |
| `landscape640-moving.gif` in the viewer | 2 | 1.8, 220 ms each | 1.8, 94 ms each |
| the same, maximised | 2 | 1.4, 290 ms each | 1.7, 78 ms each |

With the page animating the desktop's task had 6.2 per cent of the
processor and Vizcacha's 43.3 (2.0 and 29.6 the first time); the page's
animations hold 428 KB of external memory.

What the figures say and do not say:

- The page went from a tenth of what was asked to seven tenths. The rest
  is the three spinners, whose every picture is the whole 96x96.
- The decoding per picture fell as hoped where pictures are patches: the
  banner to a quarter, the large animation to under a half. The spinner's
  fell by a third, which is the row no longer averaged.
- The pictures a second in the viewer did not move, and two are a little
  lower. They were already at what the file asks; why the spinner counts
  12.0 and not 12.5 was not found. One thing is known to cost: the count
  in the log stops at the first picture finished after five seconds, so
  the time of that one picture is in the figure. For the large animation
  that picture is the first of the next round, the only one that was
  still converted whole: 630 ms on its own.
- That last point was then changed, and has not been on the board: going
  round again, only what the later pictures touched is converted, since
  the first picture comes back exactly as it was shown the first time.

## On the board, the third time

The third turn (`d2deeb1`) was not merged. Banner, spinner and the page
were right and the figures better (the large GIF at 10 pictures in 5362 ms
with 645 ms of decoding, the page at about 39 pictures a second), but
`landscape640-moving.gif` showed two red dots after 6 s and three half a
second later, where the request said one.

The laptop thread's guess was the change of the turn before: that going
round again repaints too little. It was the test file, and the request,
the same mistake as the banner's trail in the first turn. The four dot
pictures were see-through patches that said "leave it", so the file itself
asks for one dot, then two, three and four, and for none when it starts
again; two and then three is what a right decoder shows at 6 s. Pillow
draws the same file the same way (0, 1, 2, 3, 4 dots).

- The generator now makes each of those pictures a patch of the photo
  itself, in the photo's 256 colours, with the ring and the dot drawn on
  it. One dot at any time; Pillow counts 1162 red pixels in each picture
  after the first and nothing different outside the patch. It also puts a
  patch with a table of its own on the board for the first time.
- The PC test that plays by changed parts went on for one picture past the
  first round. It now goes past the second, so that what is named as
  changed when a round starts is checked after a round that itself began
  that way.
- Nothing in `core/` changed.

## On the board, the fourth time

`67d94fb` passed: it builds at 1,739,264 bytes, the viewer shows one red
dot in every screenshot at the default size and maximised, and the hills
are whole. A repaint takes 14.4 ms, no refill was missed and the memory
comes back. The large GIF: 10 pictures in 5380 ms with 697 ms of decoding,
and in 5372 ms with 762 ms maximised. The merge waits for the owner.

## Joined with the programs that left the firmware

Item 82 (PR 91) was merged first: the Picture viewer, Vizcacha and the
File manager are program files now, in `programs/`. `main` was merged into
this branch. What had to be put together by hand: the decoder's
constructor and destructor, now in the system's code, beside the new calls
for animations; the viewer's larger working memory and its playing, now
asking `os::memory()` and `os::counters()`; and the list of what opens a
picture, which moved to `core/src/shell/run.cpp` and has `gif` there. The
new calls are in namespace `image`, which program files may already ask
for by name. PC tests 59 of 59, among them the check of what the program
files ask for. Not compiled for the board and not run there since the
merge: the two programs are loaded from files now, which the four turns
before did not do with this code.

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
- Twice a board turn failed on a test file that asked for what was seen.
  A file made for a board check is drawn with another decoder (Pillow),
  picture by picture, before the request says what to expect.

## What is left

- Nobody has looked at it on the monitor yet: both board turns were
  judged from screenshots and figures. Whether the pointer and the menus
  feel as quick with a page of animations open is the owner's to say.
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

## On the board, the fifth time (2026-11-20, commit 356eb3e)

`main` with item 82 merged in (the Picture viewer and Vizcacha are program
files). Both load with no name refused. A double click on `banner.gif` in the
File manager opens the viewer with one moving block (48 pictures in 5004 ms).
`landscape640-moving.gif` shows one dot at a time in two screenshots half a
second apart (10 pictures in 5362 ms, 698 ms decoding); closing the viewer
gives back the internal memory (132,739 bytes free before and after).
`gifs.htm` in Vizcacha: one block on the banner and one dot on each spinner,
"4 animations, 201 pictures in 5018 ms, 1770 ms of them decoding"; memory
back after closing. No missed refill. Merged into `main`.
