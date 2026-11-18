---
title: "Chosen in the image viewer milestone"
date: 2026-11-16
by: agent
status: to-confirm
area: programs
summary: "Picture viewer: own PNG, BMP and JPEG decoders, limits, fitting, the dithering methods, sample pictures"
---

# Chosen in the image viewer milestone

Heading in the single-file log: "Chosen in the image viewer milestone (to confirm)".


- Decoders written for the project (in `core/src/image/`), not taken from
  a library: a streaming inflate for PNG (in the style of zlib's `puff`), PNG
  (every colour type and bit depth, every filter, transparency over the
  background colour; not interlaced), BMP (1, 4, 8, 16, 24 and 32 bits,
  bottom-up and top-down, no RLE), baseline JPEG (Huffman, 4:4:4, 4:2:2,
  4:2:0 and grey, restart markers; integer IDCT; not progressive or
  arithmetic). Why: they read the file in order through the storage port and
  give rows one at a time, take all memory from the Memory port (one 384 KB
  pool), never use the heap per row, use integers only (so the PC and the
  board give the same pixels), and can be fuzzed under sanitizers. The
  libraries looked at need a whole-file buffer or `malloc` (lodepng, stb_image,
  ESP-IDF's ROM TJpgDec with its own I/O, libpng/libjpeg are far larger).
- Limits: 4096 pixels wide (JPEG 2048, for its row buffers), 16384 high.
  Larger pictures say "too large".
- Fit to window by default; it shrinks with a box filter (the average of the
  pixels each screen pixel covers) and never enlarges. Actual size works up
  to 1600x1200 pixels; larger pictures are always fitted. The space around
  the picture is dark grey (73, 73, 73), and transparent PNG pixels are laid
  on that grey.
- Dithering: 11 ways, chosen in View > Dithering (a check marks the one
  in use) or in View > Options (a list with a line about each). The choice
  lasts until restart. What used to be "Error diffusion" is Floyd-Steinberg,
  kept byte for byte as it was (errors in whole values). The others:
  nearest colour; Bayer 2x2, 4x4 (the desktop's pattern, the old
  "Ordered") and 8x8; a 64x64 blue-noise mask made by
  `tools/gen_blue_noise.py` (void and cluster, fixed seed); Floyd-Steinberg
  serpentine, Atkinson, Jarvis-Judice-Ninke, Stucki and Sierra Lite, which
  keep their errors in 16ths, because with whole values the small shares of
  the wide kernels (1/48, 1/42) round to nothing and gradients band.
  Errors are measured against the level the board really shows (blue:
  0, 73, 182, 255) and diffused in the picture's own gamma-encoded values,
  not linear light: the board's resistor ladder spaces its levels evenly in
  those values, the thresholds of the ordered patterns are in them too, and
  linear-light diffusion with so few levels leaves dark areas speckled with
  isolated bright pixels.
- Default: Floyd-Steinberg. Chosen by measurement: on 8 test pictures (sky,
  skin tones, grey ramp, test card, text on a photo, the landscape, the two
  NASA photographs) its error after a blur that stands for viewing
  distance was the lowest (34.5 dB on average; Sierra Lite 34.4, Bayer 4x4
  34.0, blue noise 33.7, none 25.4); on the monitor it is smooth with no
  regular grid, and it costs 1.32 s for a 640x480 JPEG fitted to the
  maximised window against 0.85 s for the ordered patterns. Runner-up:
  Sierra Lite (the same look, 3 % quicker). Bayer 4x4 stays the fastest
  good choice. Full table under "Found on the bench".
- Decoding runs from a timer: up to 8 ms of decoding every 15 ms, showing
  rows as they come and "Loading... N%" in the status bar. The status bar
  then shows name, size, format and "(fitted)" when shrunk.
- A damaged file shows the rows decoded so far and an error box; the
  status bar says the picture is damaged.
- The Picture viewer is one window (98's Imaging opened one per picture).
- Sample pictures in C:\pictures at start-up: three made by
  `tools/gen_pictures.py` (a test card PNG, a landscape JPEG, the mascot as a
  4-bit BMP, 18 KB) and two public-domain NASA photographs (Earthrise as a
  640x480 JPEG, 14.6 KB; the Blue Marble as a 320x320 PNG, 124 KB), see
  `third_party/samples/README.md`. 160 KB in the firmware in all; the RAM
  disk still has 1849 KB of its 2048 KB free.
- The 16x16 picture icon is the 32x32 one with every other pixel, made at
  compile time.
