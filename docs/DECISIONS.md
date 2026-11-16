# Decisions

A running record of what this project will and will not do, so that anyone
picking it up (person or agent) does not have to ask again. Newest decisions
are added to the tables; nothing is deleted, only marked as replaced.

Last updated: 2026-11-16.

## Decided

| Topic | Decision | Why |
| --- | --- | --- |
| Goal | A 90s-style (Windows 95/98 look) desktop OS for the ESP32-S3 | The point of the project |
| Board | ESP32-S3-N16R8 (16 MB flash, 8 MB octal PSRAM), two USB-C sockets, kept plugged into a computer for flashing and testing | The board on hand |
| Resolution | 640x480 at 60 Hz | Basic VGA mode; fits the PSRAM bandwidth with room to spare |
| Colour depth | 256 colours (red 3 bits, green 3, blue 2), with 64 colours (2 bits each) as a fallback | Enough for the look; one byte per pixel sent straight to the pins, no palette step |
| Adapter board | One 5x7 cm perfboard carrier holds the VGA resistors, the audio output filter and the beeper |  It all fits beside the dev board |
| VGA wiring | Resistor DAC on a 5x7 cm perfboard carrier: 510, 1k, 2k ohm per colour, 100 ohm on each sync line, 1/4 W | Simple to build; every signal is on the left header next to its resistor |
| Blue's third pin | GPIO8 repeats blue's top bit | Keeps white and four greys neutral with only two blue bits in the byte |
| Video pins | Red 4, 5, 6. Green 7, 15, 16. Blue 17, 18, 8. HSYNC 9. VSYNC 10 | See `docs/hardware/README.md` |
| Debug link | UART0 through the USB-C socket marked UART | Leaves the chip's own USB port free for a keyboard and mouse |
| Keyboard and mouse | USB host on GPIO19 and GPIO20, later. Until then, input is injected over serial or the network | No USB breakout boards on hand yet |
| Two USB devices | Through a hub (external, or a hub module on the board), or one receiver shared by keyboard and mouse | The chip has a single USB port |
| Clock | NTP when online. A battery-backed clock chip (I2C on GPIO1, GPIO2) only if it can be built from parts already at home | The chip has no battery input for its own clock; no new purchases |
| No purchased modules | Nothing is bought for this project. Extras are built from parts on hand or not at all | Owner's choice |
| Networking order | 1. Serial only, for debugging and screen dumps. 2. Wi-Fi, once a basic OS boots. 3. Later: network over the second USB-C socket, and dial-up over serial | Wi-Fi leaves the serial port free for debugging while the system is in use |
| Language and base | C++ on ESP-IDF, with FreeRTOS underneath. Our own kernel layer (app model, app scheduling, messages, file system layer) sits on top of it | Every Espressif driver works directly, which carries the least risk for video, USB and Wi-Fi |
| First code | A VGA test program in ESP-IDF (C) showing 64-colour and 256-colour test cards | Proves the wiring before anything else is built on it |
| Code structure | Ports and adapters. The core (drawing, window manager, widgets, shell, apps) depends only on interfaces such as display, input, clock, storage, network and debug link. Adapters implement them for the ESP32, for a PC, and in memory for tests. Interfaces are coarse (framebuffer access, changed rectangles), never one call per pixel; the scanout interrupt sits below them | One seam gives host tests, a desktop that runs on a PC, and a small hardware-bound layer. Applies whichever language is chosen |
| PC build | Yes. The core builds and runs on a PC | Follows from the code structure; lets the desktop be developed and tested without the board |
| Debug protocol | Framed packets (COBS, CRC-16) on UART0 at 921,600 baud, with 115,200 as fallback. Log text travels inside the protocol on the same port. Described in `docs/debug-protocol.md` | A screenshot takes seconds, not half a minute; logs cannot corrupt a transfer |
| Mouse pointer | Mixed into each line as it goes to the monitor, never painted into the picture in memory. Screenshots mix it in with the same code, and can leave it out | Moving the mouse costs nothing and touches no picture memory; no saving and restoring what is under the pointer |
| Screen buffers | One picture in memory, no double buffering | Saves 300 KB of PSRAM and the copying between buffers; drawing is fast enough that tearing has not been an issue |
| Debug tool | `tools/dbg.py`, in Python | Already installed with ESP-IDF, quick to change, easy for an agent to drive |
| Repository layout | `core/` portable C++, `firmware/os/` ESP-IDF project with the ESP32 adapters, `host/` PC tests and a simulated board | Follows from the code structure |
| Window repainting | Each program redraws its window when asked, as 90s systems did. Windows keep no stored picture of themselves | Least memory. Some visible repainting when windows move is accepted |
| Font | Spleen 6x12 (BSD licence), a fixed-width bitmap font, for the whole interface. Bold is made by doubling each pixel sideways. The font file and its licence are in `third_party/fonts/spleen/` | Owner's choice from eight freely licensed candidates |
| Look | Windows 98 style: fading title bars, toolbar and address bar in folder windows, quick-launch icons by the Start button. Target picture: `docs/look/target-98.png`, drawn by `tools/mockup/desktop.py` | Owner's choice between 95 and 98 previews |
| Name and mascot | The OS is called Chinchilla. Its logo and mascot is a chinchilla face, shown on the Start button. The 16x16 sketch in `tools/mockup/desktop.py` is the starting point | Owner's choice |
| Colours that the board cannot show exactly | Made from a fixed 4x4 dot pattern of the two nearest colours (ordered dither), per colour channel. Used for the desktop colour, title bars and their fade. Lines one pixel wide always use exact colours | Blue has only four levels, so navy, teal and mid-grey do not exist as plain colours |
| Start-up | A quick real self-test screen (each line printed when its check has finished, with the value found; a failed check shows in red and the start goes on), then a splash with the mascot, the name and a ribbon of the 256 colours for about 500 ms, then the desktop. Chosen by the owner, 2026-11-16 | Shows what the machine has, in the spirit of a 90s PC, without slowing the start much |
| Memory test at start-up | Real and on by default, decided by the owner: writes and reads back an address-dependent pattern over external RAM borrowed from the heap (never memory in use) for about 250 ms, counting KB on screen; a wrong word is shown with its address and logged, and the start goes on. On the board it covers 4992 KB in 254 ms (about 20 MB/s written and read back), of roughly 6 MB free at that point; all of it would take about 0.31 s | Real figures, a quick flash |
| Testing | Three layers: unit tests of the core on the PC, test-first where practical; framebuffer snapshots compared with stored images; a small number of on-device tests through the serial debug protocol. Signal timing is checked on the bench, not by tests | Fast feedback for most of the code, and checks that can run without hardware |

### File system on the flash (2026-11-16)

Decided by the owner: LittleFS (chosen over FAT), as C:, so files stay across
restarts. The joltwallet/littlefs component (pinned to 1.23.1) behind the same
storage port; a new or unreadable drive is formatted and gets the sample files.
Names ignore case on it too, as on the RAM disk (the drive code looks a name
up without regard to case when it does not exist as written). Code and
constants run from PSRAM (CONFIG_SPIRAM_XIP_FROM_PSRAM), because writing the
flash otherwise turns the cache off and the picture shakes (see Found on the
bench).

### Flash layout (2026-11-16)

Decided by the owner. The app area is 0x10000-0x400000 (3.94 MB, one app now;
it can later be split into two over-the-air update slots of about 1.9 MB
without moving C:). C: is LittleFS on the 12 MB above it. A larger C: with no
room kept for updates was the alternative.

### Program settings (2026-11-16)

Decided by the owner. Each program keeps its settings in its own plain text
file in a hidden folder, Linux style: `C:\.config\<program>.ini`. The format
is INI (`[section]`, `key = value`, `;` and `#` comments), chosen over TOML and
YAML. Names that start with a dot are hidden in the File manager unless hidden
files are shown. There is no central registry.

### Parallel work (2026-11-16)

The owner asked for work to be split across several threads, each in its own
worktree, with board time taken in turns. The laptop-and-board thread keeps
committing to `main`. Other threads use one branch per feature, test on the PC
and open pull requests; this replaces "no branches, no pull requests" for those
threads only. The owner decided that the laptop-and-board thread ("board
manager") merges each branch into `main` after it has passed on the board; the
other threads do not merge their own work.

## Rejected

| Option | Why not |
| --- | --- |
| 32,768 or 65,536 colours (15 or 16 bit) | Overkill for this project. Twice the memory traffic, sits above Espressif's tested PSRAM limit at 640x480, and needs 15 to 16 colour pins |
| RGB565, RGB555 and RGB444 wiring | Follows from the above |
| An "upgradeable" 15-bit board with empty resistor slots | Replaced by the 9-resistor board |
| USB ports made from ordinary GPIO pins in software | Only works with slow devices and its timing would compete with the video signal |
| Powering a keyboard from the dev board's own USB-C socket | That socket cannot supply 5 V to a device |
| Ethernet module (W5500, ENC28J60) | Would have to be bought |
| Writing our own kernel from scratch, without FreeRTOS | Would mean porting or rewriting Wi-Fi support, USB host, display and network code, and the chip cannot isolate apps from each other anyway |
| LVGL for the desktop | No overlapping window manager, and its widgets are built for touch panels |
| The "Lenna" test photo as a sample picture | A copyrighted magazine photograph; the person in it has asked for it to be retired, and journals no longer accept it. It does not go in the repository or the firmware. The owner can still copy any file of his own onto his board with `tools/dbg.py put` |
| "Tom's Diner" as a sample sound | A commercial recording, not ours to distribute. Same as above: not in the repository or the firmware; `tools/dbg.py put` works for his own copies |
| Kodak Lossless True Color Image Suite as sample pictures | Its usual source says only that it is "my understanding" Kodak released it for unrestricted use; no licence statement from Kodak could be found, so its status was not confirmed (2026-11-16). Can be added if a clear statement turns up |
| A central settings registry, as in Windows | The owner does not want one (2026-11-16). Settings are separate plain text files, one per program |

## Still open

| Question | Options | Suggested |
| --- | --- | --- |
| Window manager details chosen while building step 4, closest to the 98 look; to confirm or overrule | See the list under "Chosen in step 4" below | As built |
| Remote keyboard, mouse and screen for a person | VNC server, web page, debug tool only | VNC server |
| How apps are delivered | Built into the firmware, or loadable from disk | Built in first, behind an app interface. Still the owner's question; the shell works on that assumption: programs are entries in a table (`core/include/core/shell/shell.hpp`: name, icon, open function) that the Start menu and desktop icons launch the same way |
| Shell details chosen while building menus, the Start menu and widgets; to confirm or overrule | See "Chosen in the shell milestone" below | As built |
| A RAM disk as well as C: | Drop it, or keep a small one as a separate temporary drive | Dropped with LittleFS, since nothing needs it; the RAM disk code stays for the PC tests and the simulator |
| Details chosen in the files milestone; to confirm or overrule | See "Chosen in the files milestone" below | As built |
| Details chosen in the image viewer milestone; to confirm or overrule | See "Chosen in the image viewer milestone" below | As built |
| Desktop wallpaper from a picture (Settings > Display) | Yes (a picture shrunk to 640x480 in external RAM, 300 KB, repainted from it), later, never | Later: there is no Settings program yet and the desktop would need a picture store; skipped in this milestone |
| Start-up screens and slow monitors | Wait for the monitor before the self-test (e.g. keep the self-test on screen until 3 s after the video starts), or not | Asked. On the bench monitor (through the VGA-to-HDMI adapter) the picture appears about 3.6 s after the video starts, and the self-test and splash are over 1.25 s after it, so after a restart neither is seen; they show correctly when asked for with `tools/dbg.py boot` |
| Interlaced PNG and progressive JPEG | Support them (a whole-picture buffer for progressive JPEG: 6 bytes a pixel), keep them refused | Keep refused for now: they say "not supported" in an error box |
| SD card in the first version | Yes, later, never | Later (pins are reserved) |
| How two USB sockets are provided | External hub, hub module on the board, shared receiver | One socket now, space for a hub module |
| A PC build of the desktop for testing | Yes, no | Decided: yes. See Decided |
| How the PC build shows the screen | SDL window, image files only | Open |
| Clock chip | DS3231 module, another I2C chip | DS3231 |

## Ideas kept for later

Not scheduled. To be revisited once a basic OS boots.

| Idea | How it would work | Cost |
| --- | --- | --- |
| Dial-up over serial | PPP from the OS to a PPP server (`pppd`) on the connected computer, which shares its internet connection. About 90 KB/s at 921,600 baud; could be throttled to 56k and given a Dial-Up Networking window that "dials" | No hardware, no pins. Needs a one-time setup on the computer; awkward on Windows |
| Dial-up and debugging on the same serial port | The debug protocol is already framed packets, so network traffic travels as one more packet type. The PC tool splits the two and hands the network side to `pppd` through a virtual serial port | No hardware. More work in the PC tool |
| Software modem over serial | A real modem implemented in software at both ends. Instead of a phone line, the two sides exchange digital audio samples over the serial link (8,000 samples a second, as the digital phone network did), and the board plays them through the audio output, so the dial-up sounds are genuine. Realistic to implement: dial tone, touch-tone dialling, ringing, answer tone, and data at 300 to 2,400 bit/s (V.21, V.22bis). The well-known 33.6k and 56k negotiation (V.34, V.90) is far too complex to implement fully; it would be imitated, with the data then carried as plain serial | No hardware. Needs the sound output first, and a "phone exchange" program on the computer. Real modem speeds are very slow |
| Network over USB | The second USB-C socket presents the board to the computer as a network card. Much faster than serial | Uses the port reserved for the keyboard and mouse, so not both at once |
| Second serial port | A UART on two spare pins for dial-up, separate from the debug link | Needs a USB-serial adapter |
| Sound | Audio out on GPIO13 through a 1 kΩ resistor and a 100 nF capacitor to a 3.5 mm jack for amplified speakers, plus a piezo beeper on GPIO14. Both have their place on the adapter board. Wanted for a boot chime and, above all, dial-up modem sounds. The chip generates the signal in hardware, separate from the video output. No firmware for it yet | Parts already at home. Sounds must be original or synthesized: no Microsoft chime, no copied recordings |
| Port to Rust | Rewrite the OS in Rust as a later adventure, either on ESP-IDF or without it | A full rewrite. Check the state of Rust's display and USB host support for this chip first |
| Wired Ethernet | SPI module with an RJ45 socket, 5 pins | Rejected for now: a module would have to be bought |
| Battery-backed clock | I2C clock chip with a coin cell on GPIO1 and GPIO2 | Only if parts are already at home |
| Bluetooth keyboard and mouse | The chip's Bluetooth LE radio acts as a HID host, so wireless keyboards and mice that speak Bluetooth LE feed the same input path as USB and the debug link. Needs a pairing window. Out of place for the era, wanted anyway (2026-11-16) | No hardware. Older Bluetooth Classic keyboards and mice will not work: the ESP32-S3 has Bluetooth LE only. Shares the radio with Wi-Fi and takes flash and RAM for the Bluetooth stack |
| Bluetooth audio | Not possible on this chip: the ESP32-S3 has no Bluetooth Classic (so no A2DP) and no LE Audio | Ruled out |

### System programs the owner would like (2026-11-16)

Not scheduled. Each needs its design choices put to the owner before it is built.

| Program | Notes |
| --- | --- |
| Disk defragmenter | Purely nostalgic: flash storage does not need it. A block map that slowly tidies itself. It must not really rewrite the flash, which would only wear it out |
| Task manager | Opened with Ctrl+Alt+Del. Lists running programs with memory and processor use, and can close one |
| Power-down screen | Shut Down ends on a full screen saying "It is now safe to turn off your computer", as machines without soft power did. Wording and artwork must be our own |
| Audio player | Compact player in the spirit of late-90s players, with no visualiser and no equaliser. Opens WAV and MP3. The look must be original, not a copy of an existing player or its skins. Needs the sound output (GPIO13) working first, and a freely licensed MP3 decoder. Its sample files must be public domain, openly licensed, or made by us, with the source and licence recorded as in `third_party/samples/README.md` |
| Mine-sweeping game | A grid puzzle of the mine-clearing kind: numbers show how many neighbours are mined. Our own name, artwork and icons; not a copy of any existing game's look |
| Snake game | The classic grow-as-you-eat arcade game, with our own name and artwork. A good first program driven by the timer and the keyboard |
| Screensaver | Starts after a period with no input and ends on any key or pointer movement. Three savers in the spirit of 90s ones, all our own code and art: a starfield (flying through space), flying Chinchilla mascots, and animated curves and colours. Open: idle time, where it is configured, whether it needs a password, and how it shares the screen with the window manager |

## Found on the bench

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

### Chosen in step 4 (to confirm)

Picked by the agent that built the window manager, as close to Windows 98 as
practical. Each is easy to change.

- The taskbar clock shows `--:--` until there is a clock (NTP, later).
- Clicking the desktop takes the focus away from every window, as in 98.
- Dragging shows a dotted outline 3 pixels wide (every other pixel
  inverted); the window moves when the button is let go.
- A maximised window fills the screen above the taskbar with its border still
  showing (98 pushes the border off the screen), and cannot be dragged.
- The taskbar stays on top; windows can go under it. A window cannot be
  dragged so far that its title bar leaves the screen.
- Taskbar and title text that does not fit is cut off, without "...".
- Taskbar buttons act when the mouse button is let go, not when pressed.
- The quick-launch icons are drawn but do nothing yet.
- ~~The About window's OK button has no extra dark "default button"
  frame~~: replaced in the shell milestone, default buttons now have it.

### Chosen in the shell milestone (to confirm)

- Start menu: Programs (the built-in programs), Settings (Control Panel and
  Taskbar, both greyed for now), Run... (greyed), a line, Shut Down....
  Rows are 24 pixels with 16x16 icons (98 uses 32x32 icons). The banner
  fades from near-black at the top to blue at the bottom, with
  "Chinchilla" in bold white reading upwards. The menu sits on the left edge
  of the screen, just above the taskbar.
- The Start menu and menu-bar menus open when the mouse button goes down; an
  item is chosen when it comes up, so press-drag-release works too.
- Submenus open as soon as the pointer is over their entry (98 waits a
  moment).
- A click outside an open menu closes it and goes no further.
- Alt+Tab brings the window at the bottom of the stack to the front at each
  press, with no switcher window (98 shows a row of icons while Alt is held).
- Double click: two presses within 0.5 s and 4 pixels.
- Desktop icons sit in a fixed column (as in the target picture) and cannot be
  dragged yet. A selected icon has a navy label and every other pixel of the
  picture tinted. Network and Recycle Bin open a "not available yet" message
  box. (My Board and Documents open the File manager since the files
  milestone.)
- ~~Message boxes and dialogs are not modal~~: replaced, they are modal now.
- ~~Fixed-size dialogs keep all three title buttons~~: replaced, dialogs show
  only the close button.
- ~~The caret does not blink, scroll arrows do not repeat~~: replaced, both
  work with the timers.
- Key presses type through a US keyboard layout. Accented letters and ñ are
  typed with the debug link's Text command (`tools/dbg.py text "año"`) until
  real keyboards and layouts arrive.
- Window corners grab both ways within 16 pixels of the corner. Minimum size:
  128 pixels wide, title bar (and menu bar) plus 24 pixels high.
- Welcome now opens at x = 96 so it does not cover the desktop icons; About
  at (300, 190).
- Icons reused where there is no special one yet: Run uses the text icon,
  Shut Down the mascot; Settings has a new control-panel icon.

### Chosen in the files milestone (to confirm)

- The taskbar clock is 12-hour ("12:46 PM"), as in the target picture. It
  counts from 12:00 AM at start-up until set (`tools/dbg.py time set`).
- Timings: caret blinks every 0.53 s; held keys repeat after 0.5 s, then 30
  times a second; held scroll arrows after 0.4 s, then 20 a second; a
  submenu opens after resting 0.3 s on its entry.
- Modal: a message box is modal to the window that was in use when it
  opened; Shut Down holds the whole desktop. Dialogs that belong to a window
  have no taskbar button; the owner's button shows pressed instead.
- Text files are Latin-1, as 98 wrote them; file and folder names are kept
  in UTF-8. Files read with CRLF line ends are written back with CRLF; new
  files get CRLF too.
- Names: up to 63 characters, compared without regard to case, none of
  \ / : * ? " < > |, no space or dot at the end.
- The RAM disk is 2 MB of external RAM, labelled "RAM disk", with at most
  256 files and folders. Its readme.txt says that it is lost at restart.
- The File manager opens folders in the same window (Back and Up), at most
  three File manager windows. My Board lists the drives. Deleting a folder
  deletes everything in it, after a question that says so. New Folder and
  Rename ask for the name in a small dialog (98 edits the name in place).
  Icons per extension: text (.txt .log .ini .md), picture (.png .jpg .bmp
  .gif), other files a blank page. Only text files open (in the Editor);
  others say there is no program yet. (Since the image viewer milestone:
  .bmp .png .jpg .jpeg open in the Picture viewer.)
- The Editor is one window (98 Notepad opens one per file). Files up to
  512 KB; Undo is one step that a second Undo redoes, as in Notepad. The
  title shows a * after the name while there are unsaved changes. No
  Ctrl+S (Notepad 98 had none); Save is in the File menu. No word wrap.
  Menus show no shortcut keys yet.
- The text field and the list box do not use the clipboard yet; the Editor
  does (Ctrl+X, C, V, Shift+Delete).

### Chosen in the image viewer milestone (to confirm)

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

### Chosen in the Wi-Fi milestone (asked 2026-11-16, to confirm)

Put to the owner as options; built on the recommended one until he answers.

- Where Wi-Fi passwords are kept: the board's settings area in the flash
  (NVS), not a file on C:, so the File manager, `tools/dbg.py get` and a
  remote viewer cannot read them. This is an exception to "settings are INI
  files in `C:\.config`" and needs the owner's yes; the alternative is
  `C:\.config\network.ini` with the passwords in it. The names of the
  remembered networks and the time zone are meant to move to
  `C:\.config\network.ini` once the INI helper is on `main`; today they
  sit with the passwords behind one small port (`net::KeyStore`).
- Remembered networks: up to 8, most recently used first. At start-up and
  after a lost link the board joins the strongest remembered network in
  range, tries the next one if that fails, and looks again after 10, 20, 40
  and then every 80 seconds. Disconnect in the panel stays disconnected.
  A network is remembered only after it has connected.
- The Network panel opens from the desktop's Network icon, from Start >
  Settings > Network and from the signal icon in the tray (one click). The
  tray icon shows 0 to 4 bars, or a red cross when not connected.
- Time zone: Spanish mainland time with EU summer time (a guess from the
  Spanish sample text), set in the code until there is a Date/Time panel.
  The clock is set when the network time first arrives and every hour.
  Time server: `pool.ntp.org`.
- Security accepted: open networks, and WPA, WPA2 and WPA3 with a password.
  WEP is refused; networks that ask for a user name (enterprise) are listed
  but cannot be joined. Hidden networks: through "Other...".
- Wi-Fi power saving is off (a remote screen would lag by 100 to 300 ms).
- The debug link can scan, connect, disconnect and forget
  (`tools/dbg.py wifi`); it never returns a password.
- Firmware size with Wi-Fi: 1.10 MB (556 KB before), over ESP-IDF's
  default 1 MB app partition; it fits the 3.94 MB app area of the LittleFS
  partition table, which also keeps `nvs` (24 KB) and `phy_init`.
- ESP-IDF v5.5 does install in the cloud container (the firmware of this
  milestone was compiled and linked there, not run): `tools/setup-cloud-idf.sh`.

## To confirm on the bench

- The picture is stable at 640x480 with the framebuffer in PSRAM.
- The colours and the four greys look right in both modes.
- The picture stays stable with Wi-Fi active and during flash writes.
- Wi-Fi (never run on the board yet): the radio starts and how long it
  takes; a scan lists the networks around; a wrong password is reported as
  such and a right one connects; the board joins by itself after a restart;
  the clock is set from the network; free internal RAM with Wi-Fi up; the
  picture stays stable and the frame rate holds during a scan and while
  connected.

## Planned order of work

1. Bench test of the VGA wiring (`firmware/vga-test`). Done 2026-11-16.
2. Debug protocol and PC tool, so screenshots and injected input work early.
   Done 2026-11-16, working on the board.
3. Video driver with a cursor merged in at scanout. Done 2026-11-16.
4. Window manager and widget toolkit. Done 2026-11-16: windows, frames,
   resizing, menus and menu bars, taskbar, and the widgets (label, buttons,
   check box, radio button, text field, scroll bars, list box, group frame,
   text area).
5. Desktop shell, file system and file manager. Shell done 2026-11-16 (Start
   menu, desktop icons, program table, message box, Shut Down dialog, Widget
   gallery); storage port, RAM disk and File manager done 2026-11-16. The file
   system on the flash waits for the owner's choice (LittleFS or FAT).
6. Text editor, then image viewer. Editor done 2026-11-16. Picture viewer
   done 2026-11-16 (BMP, PNG, baseline JPEG); wallpaper not done.
7. Wi-Fi, NTP, remote display.
8. USB host board, SD card.
9. Network over USB and dial-up over serial.

### Chosen for the Task Manager, Shut Down and the Defragmenter (to confirm)

Built on a branch from the owner's backlog (2026-11-16), tested on the PC
only; not yet run on the board. Each is easy to change. Pictures:
`host/tests/pictures/34-task-manager.png` to `38-power-down.png`.

- Task Manager: Ctrl+Alt+Del opens it from anywhere (the key never reaches
  the program in use), and it is in Programs. It lists every window that
  has a taskbar button, except itself. Switch To (or Enter, or a double
  click) brings one forward. End Task closes the program at once, with its
  open dialogs and without its "save changes?" question: unsaved text is
  lost, as with a program that hangs. The other option is to ask the program
  to close as its close button does. Ctrl+Alt+Del does nothing while the
  Shut Down dialog is open.
- Its figures are all measured, once a second. Programs here are windows
  of one task, so there is no figure per program. "Desktop" is the share
  of one core the UI task spent on input, timers and painting
  (`Wm::busy_us`); "Video out" is the time spent filling video lines
  (`scanout_us` of the System port); the graph shows their sum over the
  last minute. Memory is used against total for internal and external RAM.
  The other option for the processor is FreeRTOS's own run-time statistics
  (every task, both cores), which needs two settings in `sdkconfig` and
  code in the firmware adapter; it can replace this without changing the
  window. On the PC the video share is 0 and free memory is a fixed,
  plausible figure.
- Shut Down: the dialog now has Shut Down (the default), Restart and
  Cancel. Shut Down asks every program to close as its close button would.
  An unsaved file in the Editor calls it off and leaves the Editor's
  question on screen; after answering, Shut Down has to be chosen again
  (98 carries on by itself). Then the screen is handed over for good: the
  mascot asleep under a night sky, "It is safe to power down the system
  now." (the owner's wording) and a line saying that Ctrl+Alt+Del starts
  Chinchilla again, since the board has no power switch to press. The
  pointer goes and all other input is ignored. Nothing needs flushing:
  the storage port has no open-ended writes (a file is on the flash once
  it is closed), and every program has closed by then.
- Defragmenter: a show only. Its disk is made up (864 blocks, about 60 %
  full, some 30 files in pieces, laid out from a fixed sequence of seeds);
  no drive is read or written, and the window says so. Each file has its
  own colour, so scattered colours gather into bands (98 coloured blocks by
  state). A move shows the blocks being read (yellow), then written (red),
  30 ms a step, up to 6 blocks at a time: a full run takes about a minute.
  Start, Pause and Resume are one button; Scramble makes a new mess;
  closing the window pauses. The other option is a map that follows the
  real drive's fill level and file count.
- Icons reused: the Task Manager has the control-panel icon, the
  Defragmenter the drive icon.
