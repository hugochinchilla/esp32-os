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
| Testing | Three layers: unit tests of the core on the PC, test-first where practical; framebuffer snapshots compared with stored images; a small number of on-device tests through the serial debug protocol. Signal timing is checked on the bench, not by tests | Fast feedback for most of the code, and checks that can run without hardware |

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

## Still open

| Question | Options | Suggested |
| --- | --- | --- |
| Window manager details chosen while building step 4, closest to the 98 look; to confirm or overrule | See the list under "Chosen in step 4" below | As built |
| Remote keyboard, mouse and screen for a person | VNC server, web page, debug tool only | VNC server |
| How apps are delivered | Built into the firmware, or loadable from disk | Built in first, behind an app interface. Still the owner's question; the shell works on that assumption: programs are entries in a table (`core/include/core/shell/shell.hpp`: name, icon, open function) that the Start menu and desktop icons launch the same way |
| Shell details chosen while building menus, the Start menu and widgets; to confirm or overrule | See "Chosen in the shell milestone" below | As built |
| File system on the flash | LittleFS, FAT | Asked; until it is chosen, C: is a RAM disk (lost at restart) behind the storage port, and nothing writes the flash |
| Details chosen in the files milestone; to confirm or overrule | See "Chosen in the files milestone" below | As built |
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

### System programs the owner would like (2026-11-16)

Not scheduled. Each needs its design choices put to the owner before it is built.

| Program | Notes |
| --- | --- |
| Disk defragmenter | Purely nostalgic: flash storage does not need it. A block map that slowly tidies itself. It must not really rewrite the flash, which would only wear it out |
| Task manager | Opened with Ctrl+Alt+Del. Lists running programs with memory and processor use, and can close one |
| Power-down screen | Shut Down ends on a full screen saying "It is now safe to turn off your computer", as machines without soft power did. Wording and artwork must be our own |
| Audio player | Compact player in the spirit of late-90s players, with no visualiser and no equaliser. Opens WAV and MP3. The look must be original, not a copy of an existing player or its skins. Needs the sound output (GPIO13) working first, and a freely licensed MP3 decoder |

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
  others say there is no program yet.
- The Editor is one window (98 Notepad opens one per file). Files up to
  512 KB; Undo is one step that a second Undo redoes, as in Notepad. The
  title shows a * after the name while there are unsaved changes. No
  Ctrl+S (Notepad 98 had none); Save is in the File menu. No word wrap.
  Menus show no shortcut keys yet.
- The text field and the list box do not use the clipboard yet; the Editor
  does (Ctrl+X, C, V, Shift+Delete).

## To confirm on the bench

- The picture is stable at 640x480 with the framebuffer in PSRAM.
- The colours and the four greys look right in both modes.
- The picture stays stable with Wi-Fi active and during flash writes.

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
6. Text editor, then image viewer. Editor done 2026-11-16.
7. Wi-Fi, NTP, remote display.
8. USB host board, SD card.
9. Network over USB and dial-up over serial.
