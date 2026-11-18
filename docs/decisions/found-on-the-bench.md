---
title: "Found on the bench"
date: 2026-11-18
by: agent
status: reference
area: hardware
summary: "What the real board showed: pixel clock, bounce buffers, PSRAM and flash effects on the picture, Wi-Fi interference, timings"
---

# Found on the bench

First picture: 2026-11-16, through a VGA-to-HDMI adapter.

- Pixel clock: 25.000 MHz gives a clean picture. 25.175 MHz shows shifting
  pixels. 25.000 MHz is the default.
  24 MHz also gives a clean picture, but runs the screen at 57 Hz instead of
  59.5 Hz and saves only 4 % of the video work, so it is kept as a fallback.
- With the framebuffer sent straight from PSRAM, redrawing the screen left the
  picture displaced sideways until the output was restarted. The test program
  now uses bounce buffers and restarts the output in every vertical blank,
  which cured it.
- The blue mirror pin works: the four greys show no tint. Measured on a white
  screen at the VGA plug, adapter connected: red 0.485 V, green 0.485 V, blue
  0.475 V average, which is about 0.66 V peak (95 % of VGA full white).
- The cable's pin 10 wire is not a usable ground on its own with the adapter
  in use. The cable's shield braid must be soldered to the board's ground.
- During the ground rework the UART/COM socket stopped powering the board for
  about 20 minutes and then recovered by itself, which points to a
  self-resetting fuse tripped by a momentary short. One cable is enough again.
- `firmware/os` compiled with ESP-IDF v5.5 without changes to the code. One
  setting was missing: ESP-IDF ignores the console speed unless the console
  is set to "custom", so the board talked at 115,200 baud until
  `CONFIG_ESP_CONSOLE_UART_CUSTOM=y` was added.
- The debug link works at 921,600 baud: 20 screenshots in a row, all
  identical, no receive errors. A screenshot of the demo screen takes 0.3 s.
- Opening the port with `tools/dbg.py` does not restart the board: uptime
  and the pointer position carry over between commands.
- The screen updates at 59.6 frames a second (counted by `frames_shown`).
- The board's UART socket appears on the PC as `/dev/ttyACM0`, now the
  default port of `tools/dbg.py`.
- A webcam (Logitech StreamCam, `/dev/video4`) watches the monitor, so what
  the monitor really shows can be checked from the PC with `ffmpeg`.
- Files milestone (2026-11-16): 22 steps, including My Board, the docs
  folder, a file opened in the Editor, Save As, the changes question, Rename
  and Delete, are pixel-identical on the board and in the PC render (clock
  masked), with no stale pixels after any step. On the board: a file edited
  and saved, then reopened, kept the change; a 150 KB file sent with `put`
  appeared after Refresh and came back with `get` byte for byte (3.0 s each
  way); a folder was created, renamed and deleted through the File manager;
  a click on the Editor while Save As was open left Save As in front; the
  clock set to 11:59:50 showed 12:00:02 PM twelve seconds later. Times:
  typing in a 335 KB file 13.5 ms per character (the visible text is
  repainted), 38 ms for the first character after jumping from the end to
  the start (the gap moves the whole file once), PageDown 0.03 ms plus
  painting; full repaint 32 ms with the Editor open; 59.5 frames a second,
  no receive errors. Free RAM at start-up: 318.7 KB internal (327.6 KB in the
  shell milestone: the File manager windows, the file dialogs and the larger
  debug-link buffers take about 9 KB) and 5.91 MB external (the 2 MB RAM disk
  and the 64 KB clipboard); opening the Editor takes another 0.6 MB external
  (its 512 KB text and 64 KB undo). Icons and pointer shapes moved from RAM
  to flash on the way (15 KB).
- Dithering comparison (2026-11-16). Quality: PSNR in dB against the
  original after a Gaussian blur in linear light (sigma 1 pixel), on the PC
  (`host/dither_eval.cpp`, pictures from `tools/gen_pictures.py dither`).
  Time: decoding plus converting a 640x480 JPEG on the board, fitted to the
  maximised window / at actual size. Webcam: a 600x380 test picture at
  actual size, 3 s at 60 fps per dither; "noise" is the per-pixel standard
  deviation over time in the sky and skin areas (0.8 for flat colour, the
  camera's own noise); no frame-to-frame flash was seen with any of them.

  | Dither | Sky | Skin | Grey ramp | Test card | Text on photo | Landscape | Earthrise | Blue Marble | Mean | Fitted | Actual | Webcam noise | On the monitor |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | None | 24.9 | 23.1 | 23.8 | 25.3 | 21.3 | 21.1 | 31.8 | 32.3 | 25.4 | 0.96 s | 1.21 s | 0.8 | Hard bands everywhere |
  | Bayer 2x2 | 35.4 | 30.8 | 30.9 | 29.6 | 30.3 | 30.2 | 41.2 | 35.9 | 33.0 | 0.85 s | 1.03 s | 1.1 | Visible steps in gradients |
  | Bayer 4x4 | 37.7 | 32.0 | 31.4 | 34.2 | 30.3 | 30.1 | 40.8 | 35.7 | 34.0 | 0.85 s | 1.04 s | 1.3 | Smooth; faint regular lines in the sky |
  | Bayer 8x8 | 37.2 | 31.8 | 31.3 | 31.8 | 30.4 | 30.3 | 40.6 | 35.6 | 33.6 | 0.85 s | 1.02 s | 1.4 | Smooth; faint cross-hatch |
  | Blue noise | 37.4 | 31.9 | 31.3 | 32.2 | 30.4 | 30.3 | 40.6 | 35.6 | 33.7 | 0.85 s | 1.03 s | 2.1 | Smooth but grainy, and the most shimmer |
  | Floyd-Steinberg | 37.2 | 32.1 | 31.8 | 33.4 | 30.9 | 30.7 | 41.4 | 38.2 | 34.5 | 1.32 s | 1.75 s | 1.4 | Smooth, no grid |
  | FS serpentine | 37.8 | 32.1 | 31.6 | 32.6 | 30.7 | 30.5 | 41.6 | 38.0 | 34.4 | 1.35 s | 1.80 s | 1.4 | Smooth, no grid |
  | Atkinson | 33.5 | 31.2 | 31.5 | 36.8 | 31.1 | 30.9 | 39.5 | 37.4 | 34.0 | 1.46 s | 1.98 s | 1.4 | Colour patches in skin and sky (it drops 1/4 of the error); crisp on the test card |
  | Jarvis-Judice-Ninke | 36.5 | 31.9 | 31.4 | 32.6 | 30.3 | 30.2 | 40.8 | 36.9 | 33.8 | 1.79 s | 2.48 s | 1.5 | Smooth, visible worm texture in skin |
  | Stucki | 37.0 | 32.0 | 31.5 | 32.7 | 30.5 | 30.4 | 41.1 | 37.2 | 34.1 | 1.79 s | 2.48 s | 1.4 | As JJN |
  | Sierra Lite | 38.0 | 32.1 | 31.5 | 32.7 | 30.7 | 30.6 | 41.7 | 38.1 | 34.4 | 1.28 s | 1.71 s | 1.5 | Smooth, no grid |

  The NASA photographs come out best with Sierra Lite and the two
  Floyd-Steinberg variants by the numbers; on the monitor the small Earth
  discs look the same with any of the error diffusions. Blue noise's
  higher webcam noise is partly the monitor sampling its isolated dots and
  partly the camera's compression of fine detail; it is the one pattern
  where shimmer can be seen. The longest timer slice while decoding was
  21 to 31 ms with every dither.
- Self-test in the approved layout (2026-11-16): on the webcam the header
  shows 3.2 s after a restart, the memory count climbs 512K, 1408K, 2304K,
  3328K, 4096K to about 5760K over 1.7 s, then the internal test (about 144K),
  the remaining lines, the splash at 6.8 s, the desktop at about 7.6 s. An
  all-black screen until the lines start made the monitor take 5.4 s instead
  of 2.4 s to show a picture, so the header is drawn at once. The PC test
  compares the layout with the approved drawing pixel for pixel, given the
  drawing's own numbers.
- Start-up timing on the board (2026-11-16). The bench monitor shows a
  picture about 3.1 s after a restart (2.4 s after the video starts). With
  the defaults the self-test is on screen from then until 4.7 s (about 1.5 s
  readable on the webcam), the splash in full until 5.5 s, the desktop at
  5.5 s after the restart (4.95 s after the video started). A key 1.6 s
  after the restart: the desktop is the first thing the monitor shows.
  fastboot: the desktop 0.16 s after the video starts, the checks logged.
  With the direct join Wi-Fi associates 0.1 s after the radio starts and has
  its address about 1.1 s later (before: 5 s and 6 s, after a scan of every
  channel).
- Scanlines while Wi-Fi connects (2026-11-16). The owner saw disturbed
  lines (not whole-picture shifts) from the first picture after a restart
  until a few seconds after the Wi-Fi bars appeared, then once or twice more,
  then none. By eye, back to back: Wi-Fi off at start-up, a steady picture;
  Wi-Fi on, the lines; Wi-Fi on at 2 dBm instead of 20, no lines. The webcam
  does not show them (its 30 different frames a second and blur hide single
  lines). Measured in the video interrupt itself (`tools/dbg.py memory`,
  every refill of a bounce buffer against the DMA's 320 us cadence, and a
  100 ms timeline from start-up to 20 s): with Wi-Fi on, no refill missed its
  deadline and the worst started 12 us late (1 to 2 us with Wi-Fi off), while
  the owner saw the lines. So the picture data is right and the lines come
  from the radio's transmit bursts disturbing the analogue side (supply or
  radiation into the resistor DAC, the cable or the VGA-to-HDMI adapter).
  Fixed in software by the 2 dBm default and the direct join (Decided). Cost
  at 2 dBm with the owner's router (received at -64 dBm): Wi-Fi 270 KB/s
  instead of 370 to 408, a plain 300 KB VNC full screen 1.07 s instead of
  0.8 s, ping 0 % loss over a minute, 13 ms average. For later, in hardware:
  the module's antenna end away from the VGA board and cable, a clip-on
  ferrite on the VGA cable, bulk and ceramic decoupling (100 uF and 100 nF)
  at the resistor DAC, and a stronger 5 V supply; with those a higher power
  may be clean. Separately, the video interrupt now starts on core 1 (Wi-Fi
  and lwIP are on core 0): neutral on the counters at start-up; before, two
  isolated missed refills (339 and 658 us late) were seen during heavy
  debug-link traffic.
- Internal RAM with Wi-Fi (2026-11-16, `tools/dbg.py memory`). Before:
  internal heap 276 KB, 110 KB free, lowest 78 KB since start, largest block
  37 KB; a VNC viewer costs about 0.1 KB more. Task stacks: the UI task uses
  at most 6.3 KB of its 16 KB (after the viewer, dialogs, an Editor save, the
  Task Manager); every other task is ESP-IDF's own and small. Measured one at
  a time (Wi-Fi throughput as 300 KB full screens over VNC):
  8 static Wi-Fi receive buffers instead of 16: +13.4 KB, throughput the
  same; the Wi-Fi code out of internal RAM (ESP_WIFI_IRAM_OPT and
  RX_IRAM_OPT off): +18 KB, throughput the same; mallocs over 4 KB in PSRAM
  instead of over 16 KB: no change. Applied: the first two together give
  142 KB free (lowest 109 KB, largest block 70 KB), 370 to 408 KB/s, 59.6
  frames a second and a steady picture. Not applied (choices): the UI
  task's stack at 12 KB (+4 KB, smaller margin for deep paths not yet
  exercised); the UI task's stack in PSRAM (+16 KB, needs checking that flash
  writes from that task stay safe).
- Remote screen on the board (2026-11-16, PR 7's VNC and browser parts).
  VNC on port 5900: colours exact (0 pixels differ from the board's own
  screenshot), mouse and keys work, a full screen takes 0.37 s with ZRLE
  (4.5 KB) and 1.9 s as plain pixels (300 KB, about 160 KB/s over Wi-Fi).
  The browser page on port 80 connects within a second in Chromium, takes
  mouse and keys, takes over from a VNC client (which is let go) and hands
  back to the next one. 59.5 frames a second and a steady picture with
  either viewer. Free internal RAM 141 KB with a viewer connected; firmware
  1.21 MB. Both listen on every address of the board, start whenever the
  radio starts, cannot be switched off, and ask for no password (see Still
  open). There is no VNC viewer on the laptop: `tools/rfb_client.py` checks
  the server.
- Wi-Fi on the board (2026-11-16, PR 7's Wi-Fi part). The radio starts in
  70 ms after the start-up screens; a scan takes 2.9 s; after a restart the
  board joins the remembered network on its own about 5 s after the radio
  starts, and the clock is set from the network (local time). A wrong
  password says "the password is not right" and keeps the remembered one.
  59.5 frames a second and a steady picture (no shaken frames on the webcam)
  while scanning and while connected. Free internal RAM falls from about
  300 KB to 151 KB with Wi-Fi on; 6.8 MB external. Firmware 1.12 MB. Note:
  ESP-IDF's own Wi-Fi log line names the network it joins (on the debug
  link, not stored); the password is never logged.
- C: on LittleFS (2026-11-16). Mounting takes 6 to 11 ms; 12 MB, 11 MB
  free with the samples. Through the debug link a 60 KB file goes onto the
  flash in 3.0 s (the link is the limit); the Editor saves a 293 KB file in
  3.3 s (about 90 KB/s, the UI waits meanwhile). A file put on the drive, a
  file saved from the Editor, the viewer's dithering (C:\.config\viewer.ini)
  and Show hidden files (C:\.config\files.ini) all survive a restart.
- The picture shook while the flash was written (2026-11-16, first seen by the
  owner with the LittleFS build): every other frame had bands shifted
  sideways (frame-to-frame change up to 38.8 against 0.4 when still) for the
  whole write, and two debug-link frames were lost, because writing the flash
  turns the cache off and the bounce-buffer refill cannot read the
  framebuffer in PSRAM. Fixed with CONFIG_SPIRAM_XIP_FROM_PSRAM and
  CONFIG_LCD_RGB_ISR_IRAM_SAFE (Espressif's advice for RGB panels with a PSRAM
  framebuffer): 0 shaken frames while writing, 59.5 frames a second, no lost
  frames, and debug-link writes 2.6 times faster. It costs external RAM for
  the app's code and constants (about 610 KB now); a full repaint still takes
  27.5 ms.
- Start-up screens (2026-11-16): self-test 624 ms (the checks 274 ms, of
  which the memory test 254 ms, then the 350 ms hold), splash 511 ms, from
  the start of the UI task to the desktop 1247 ms; the first full paint of
  the desktop 29 ms. Firmware 557 KB. The splash is the approved drawing
  (docs/look/boot/boot-e-colour-ribbon.png) pixel for pixel, on the PC and
  on the board; it uses Spleen 32x64 and 12x24 for the name and subtitle,
  only the 11 letters needed (5 KB). Both screens are pixel-identical to
  the PC pictures (the self-test apart from its values, which differ from
  the simulator's). The webcam shows both when asked for with `tools/dbg.py
  boot`, but not after a restart: the monitor needs about 3.6 s to show a
  picture once the video starts (see "Still open").
- Image viewer milestone (2026-11-16): all 33 PC render steps, including
  the 11 new ones (pictures folder, the viewer with each dither, fitted and
  actual size with scroll bars, the Open dialog, the mascot BMP, the test
  card PNG, a damaged JPEG with its error box), are pixel-identical on the
  board, with no stale pixels. Decoding 640x480 pictures on the board
  (time spent decoding; with the pauses between timer slices in brackets):
  JPEG 4:2:0 0.80 s (1.32 s), with error diffusion 1.13 s, no dither 0.90 s;
  JPEG 4:4:4 1.09 s (1.43 s); PNG photo 0.94 s (1.51 s); PNG test card and
  colour bars 0.73 s (1.21 s); 24-bit BMP 0.43 s (0.80 s). The longest timer
  slice was 12 to 26 ms, not the 8 ms aimed at, because a JPEG decodes a
  whole 16-row strip at once; a ping took 19 to 29 ms while decoding
  (9 to 10 ms idle). Still 59.5 frames a second, no receive errors. The
  viewer takes about 2.3 MB of external RAM the first time it opens (the
  1600x1200 display buffer, the 384 KB decoder pool and the converter rows)
  and keeps it; internal RAM did not change (311 KB free). On the webcam,
  ordered dither and error diffusion both look smooth at viewing distance
  and are hard to tell apart; no dither shows clear bands in the sky. Copying
  a 900 KB BMP over the debug link takes 19 s.
- Shell milestone (2026-11-16): 14 steps (Start menu, launching each
  program, the gallery's menus, typing "año" into the text field, resizing,
  desktop icon double click, Shut Down dialog) are pixel-identical on the
  board and in the PC render, with no stale pixels after any step. Restart
  from Shut Down restarts the board, and it comes back to the same start-up
  screen. Times on the board: full repaint 24 to 29 ms (more windows,
  more to draw); opening the Start menu or a menu 1.1 ms; an outline step
  1 ms; dropping or resizing a window 7 to 18 ms; maximising 28 ms. Still
  59.5 frames a second, no receive errors. Free RAM: 327 KB internal,
  8.07 MB external. A screenshot takes 0.33 s.
- Found while testing Restart: `tools/dbg.py` resends a command when no reply
  comes, so a key press that restarted the board was sent again to the new
  start-up. The board now replies to key, mouse and text commands before
  acting on them.
- Window manager (2026-11-16): the board's screen is pixel-identical to the
  PC render of the same steps (desktop, focus change, drag and drop), and a
  full repaint after any step changes nothing (no stale pixels). Times on the
  board: full-screen repaint 22 ms; bringing a window to the front 16 ms;
  each mouse move while dragging 0.4 to 1.1 ms (only the outline changes);
  dropping a 300x160 window 6.6 ms; closing it 16 ms. Still 59.5 frames a
  second, no receive errors. A screenshot took 2.2 s instead of 0.3 s,
  because the dithered desktop colour does not compress with run lengths;
  screenshot encoding 2 (copies from 1, 2, 4 pixels or 1, 2, 4 rows back)
  brought it back to 0.4 s.
- Pointer mixed in at scanout (2026-11-16): the OS fills the video bounce
  buffers itself (`no_fb` and `on_bounce_empty` in ESP-IDF's RGB panel
  driver), copying 10 lines at a time from the framebuffer in PSRAM and
  drawing the pointer over them. Results: still 59.5 frames a second; the
  filling takes 5.2 % of one core, almost all of it the copy from PSRAM;
  600 small mouse moves showed no tearing, no doubled or missing pointer
  and no picture shift on the webcam; the pointer clips cleanly at every
  edge and corner; moving it leaves the framebuffer byte-identical.
