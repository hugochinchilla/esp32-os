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
| Snake game's name | Culebra (Spanish for snake). Chosen by the owner, 2026-11-16, over Sierpe and Hay Trail | Owner's choice |
| System sounds | Chosen by the owner, 2026-11-16, over two rounds of synthesized options: boot is a glass pad with a quiet bell; session close is a mellow, low FM-piano phrase ("sunset"); alert and error dialogs are a wood-knock pair; the bell is a short kalimba pluck. The files are `docs/sounds/boot.pcm`, `close.pcm`, `alert.pcm`, `error.pcm` and `bell.pcm`: raw signed 16-bit little-endian samples, mono, 22,050 Hz, no header, about 390 KB together. That format was the owner's choice (2026-11-16) over a pulse stream made on the PC: the firmware copies the samples unchanged to the I2S output, whose hardware converter makes the pulses on GPIO13, so nothing is decoded while playing. To listen on a PC: `ffplay -f s16le -ar 22050 -ch_layout mono boot.pcm`. Generated by `tools/sounds/make_sounds.py`; nothing sampled or copied. No firmware plays them yet | Owner's choice |
| Piezo beeper's job | The beeper on GPIO14 is used only for the modem. System sounds go to the audio jack only (the owner, 2026-11-16) | Owner's choice |
| Colours that the board cannot show exactly | Made from a fixed 4x4 dot pattern of the two nearest colours (ordered dither), per colour channel. Used for the desktop colour, title bars and their fade. Lines one pixel wide always use exact colours | Blue has only four levels, so navy, teal and mid-grey do not exist as plain colours |
| Start-up | A quick real self-test screen (each line printed when its check has finished, with the value found; a failed check shows in red and the start goes on), then a splash with the mascot, the name and a ribbon of the 256 colours for about 500 ms, then the desktop. Chosen by the owner, 2026-11-16 | Shows what the machine has, in the spirit of a 90s PC, without slowing the start much |
| Memory test at start-up | Real and on by default, decided by the owner: writes and reads back an address-dependent pattern over external RAM borrowed from the heap (never memory in use) for about 250 ms, counting KB on screen; a wrong word is shown with its address and logged, and the start goes on. On the board it covers 4992 KB in 254 ms (about 20 MB/s written and read back), of roughly 6 MB free at that point; all of it would take about 0.31 s | Real figures, a quick flash |
| Screensaver | Starts after 5 minutes without a key or mouse movement by default, showing Flying Chinchillas, with no password (the owner's choices, 2026-11-16), and ends on a key press, a click or a real mouse movement, which only wakes the screen. Three savers, all our own code and art: Starfield, Flying Chinchillas, Curves and Colours. Set in a small dialog under Start > Settings > Screen Saver now; it becomes a tab of a Display window once there is a Control Panel (the owner's choice, 2026-11-16) | Wanted by the owner; the dialog works with the menus that exist today |
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

### Start-up timing (2026-11-16)

Decided by the owner. The self-test runs for real as before and then stays
on screen until `monitor_wait_ms` after the video signal started (4000 by
default), so a monitor that takes a while to lock still shows it; then the
splash for `splash_ms` (800). Revised the same day: the self-test follows the
approved drawing (docs/look/boot/boot-b-self-test.png) in layout, fonts and
colours with the board's real values (the owner: "use accurate data, the
approved design is for guidance"): the real version, the KB the memory tests
really covered (a new quick test of free internal RAM joins the external
one), the 12 MB flash disk. Its header shows at once; the lines start
appearing `monitor_wait_ms` (now 2500) after the video starts, when the bench
monitor has a picture, one by one at `selftest_pace` (130 %), the memory
count climbing with the real test. The processor, RAM and flash sizes are
still checked and logged though the drawing has no line for them. Any key or mouse button skips straight to the
desktop from either screen (pointer movement does not); the checks still run
to the end and failures are logged. `fastboot = true` shows neither screen
and skips the memory test. All in C:\.config\boot.ini (docs/settings.md). A
monitor cannot tell the board it is ready (VGA is one-way, and DDC is not
wired on the adapter board), so the wait is a time.

Revised again (2026-11-16, the owner): when the lines are done the finished
screen stays with "Press any key to continue" and "Booting in 5" counting to
1 (`selftest_hold_s`, default 5, 0 for none); a key goes on at once. The
splash is hidden by default (`splash_ms` 0: not drawn at all, no blank
frame); its code and its exact picture test stay. The desktop starts with no
windows open (Welcome and About are no longer launched).

### Files on the desktop (2026-11-16)

Decided by the owner: the desktop shows the files in C:\Documents\Desktop as
icons after the system icons (My Board, Documents, Network, Trash, which
stay), a picture by file type and the name below; a double-click opens the
file with its program, or the folder in the File manager. The icons follow
changes to the folder. A welcome.txt is put there on a new drive and once on
an existing one; C:\.config\desktop.ini remembers it, so a deleted copy never
comes back.

### One documents folder (2026-11-16)

Decided by the owner: the Documents icon opens C:\Documents, which holds the
sample documents (C:\docs before) and the Desktop folder. A drive formatted
before keeps its files: at start-up its C:\docs moves into C:\Documents
once, a clashing name numbered ("notes (2).txt"), nothing overwritten, and
C:\docs removed when empty; `[documents] moved` in C:\.config\desktop.ini
records it. If something cannot be moved it stays in C:\docs and the move is
tried again at the next start.

### Trash (2026-11-16)

Asked for by the owner: deleting a file or folder moves it to the Trash; the
Trash icon on the desktop opens the Trash window, also when it is empty;
files and folders can be dragged onto the Trash icon or window from the File
manager and from the desktop; the Trash window's File menu has Empty Trash.

### Wi-Fi transmit power (2026-11-16)

Decided by the owner: the radio's transmit power defaults to the minimum,
2 dBm, adjustable as `tx_power_dbm` in C:\.config\network.ini (2 to 20),
because at full power its transmit bursts put lines on the picture while it
connects (see Found on the bench). Joining goes straight to the remembered
network's channel and access point (kept in NVS with the networks) instead
of scanning every channel first, so the radio transmits less; a full scan
follows only if it is not found there.

### Remote screen access (2026-11-16)

Decided by the owner: as built. The VNC server (port 5900) and the browser
page (port 80) start whenever Wi-Fi is on, ask for no password, and give
full mouse and keyboard control of the board to anything on the local
network that can reach it (nothing from the internet unless the router
forwards those ports). An off-by-default switch in C:\.config\remote.ini
and a password were offered and can be revisited.

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

### OS architecture (2026-11-16)

Decided by the owner after the architecture review; the full story, with the
options and why each was or was not taken, is in
`docs/journal/2026-11-16-architecture-review.md`. The work is listed in
`docs/BACKLOG.md`.

- **The model to aim for is AmigaOS's**: real tasks with priorities, one
  shared memory space, no protection between programs. The lack of memory
  isolation is accepted on purpose.
- **A process** is a row in a process table kept by the portable core: PID
  (counting up from 1), parent, name, state, flags, and what it owns (windows,
  timers, jobs, large memory). Window code stays on the UI task; a process may
  also own real FreeRTOS tasks (background jobs, shell sessions, services).
  Apps can move to their own task later, one at a time.
- **Figures in Task Manager**: FreeRTOS run-time statistics for the task view,
  and the window manager's own timing charged to the PID it is calling for
  programs that share the UI task.
  - The run-time statistics are switched on (backlog item 1,
    `docs/journal/2026-11-16-task-statistics.md`): the kernel reads the
    microsecond clock (`esp_timer`, the same for both cores) at every task
    switch and keeps a 64-bit total per task. The System port hands them out
    as a list of tasks with raw counters (`ports::System::tasks`); whoever
    shows them takes two readings and divides. `tools/dbg.py tasks` is the
    first reader. Time spent in interrupts is counted as the interrupted
    task's, so the video's share still comes from its own stopwatch
    (`scanout_us`).
- **Signals**: TERM (asks, like the close button), KILL (the OS removes what
  the process owns; it takes effect at a safe point, never while the task
  holds an OS lock) and INT (Ctrl+C). End Task asks politely first and offers
  force if the program has not closed after a few seconds.
- **Flags**: system (hidden in Task Manager's default view), protected (the
  user cannot signal it), respawn (started again if it ends). Protected: the
  serial console, the debug service, init and the window server. The taskbar
  is not protected; it restarts.
- **Priorities**: three classes, Low, Normal and High, in a narrow band below
  the video, Wi-Fi and the network stack. The FreeRTOS scheduler is not
  changed.
- **Saves**: written by a job on a worker task that belongs to the program;
  the program cannot end until its jobs have. The window is locked while it
  saves and the pointer shows an hourglass (asked for by the owner; own art).
  A save writes a temporary file and renames it over the original.
- **Shell**: in the portable core, commands built in, Linux names with DOS
  aliases (`dir`, `type`, `del`); paths keep drive letters and accept both
  `\` and `/`. One session is one task. It runs on a TTY object whose other
  end is the serial port, a telnet connection or a terminal window.
- **Serial port**: the console and the debug protocol are both live on it at
  all times. Bytes between two zero bytes that pass the frame check go to the
  debug service, everything else to the console, so `tools/dbg.py` keeps
  working whatever state the shell is in.
- **Start-up**: init is PID 1 and starts the rest from a text file. First the
  services move to after the first paint; splitting the wallpaper (window
  server) from the taskbar and icons (desktop shell) comes in a later phase.
  No artificial delays.
- **After a crash**: the machine restarts and shows an error screen naming
  the program.
- **No users and no passwords.** Security is not a goal: the OS is for
  recreation, not real use. This covers telnet; whether telnet starts by
  default or is switched on in a settings file is asked before it is built.
- **A written record**: every piece of work adds an entry to `docs/journal/`
  (what was asked, options, choice and reasons, what was built and tested).

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
| One FreeRTOS task for every app, now | A rewrite of the window manager and every widget, and 4 to 8 KB of internal RAM per app. Kept as a later step, app by app (2026-11-16) |
| Staying cooperative only (no tasks besides the UI task) | The shell and telnet would have to be hand-sliced state machines and nothing could run in the background (2026-11-16) |
| The serial console switching into the debug protocol by a command | The agents' tool would be locked out whenever the shell is stuck. Both share the wire instead (2026-11-16) |
| Changing the FreeRTOS scheduler for priorities | Not needed: fixed priorities are built in (2026-11-16) |
| Typing while a save runs (the job saves a copy) | Up to 512 KB more memory and more states to get wrong, for saves of a few seconds. The window is locked instead (2026-11-16) |
| Users, logins and passwords (telnet and VNC included) | The owner does not want them: security is not a goal of a recreational OS (2026-11-16) |
| Artificial delays to make start-up look layered | The screen shows what the machine is really doing (2026-11-16) |

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
| A virus, for fun | The owner would like to write one for Chinchilla later, to see how they work: with no memory protection every program can reach every other, as on the home computers of the time (2026-11-16) | Needs programs that can be loaded from disk to be interesting |

### System programs the owner would like (2026-11-16)

Not scheduled. Each needs its design choices put to the owner before it is built.

| Program | Notes |
| --- | --- |
| Disk defragmenter | Purely nostalgic: flash storage does not need it. A block map that slowly tidies itself. It must not really rewrite the flash, which would only wear it out |
| Task manager | Opened with Ctrl+Alt+Del. Lists running programs with memory and processor use, and can close one |
| Power-down screen | Shut Down ends on a full screen saying "It is now safe to turn off your computer", as machines without soft power did. Wording and artwork must be our own |
| Audio player | Compact player in the spirit of late-90s players, with no visualiser and no equaliser. Opens WAV and MP3. The look must be original, not a copy of an existing player or its skins. Needs the sound output (GPIO13) working first, and a freely licensed MP3 decoder. Its sample files must be public domain, openly licensed, or made by us, with the source and licence recorded as in `third_party/samples/README.md` |
| Mine-sweeping game | A grid puzzle of the mine-clearing kind: numbers show how many neighbours are mined. Our own name, artwork and icons; not a copy of any existing game's look |
| Snake game | The classic grow-as-you-eat arcade game, with our own name and artwork. A good first program driven by the timer and the keyboard. Built 2026-11-16 as Culebra; see "Chosen in the snake game" |
| ~~Screensaver~~ | Built, see Decided and "Chosen in the screensaver milestone". No password (the owner, 2026-11-16) |

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

### Chosen in the snake game (to confirm)

Culebra, the snake game, was built on a branch from the PC tests and the
simulator only; it has not run on the board. The owner chose the name (see
"Decided"); the rest is still his to confirm:

- Look: a green striped snake with a face on pale sand, red berries, a
  24 x 18 board of 12-pixel cells in a sunken frame. All drawn in
  `core/src/shell/culebra.cpp`; the 16x16 icon is in `core/src/gfx/art.cpp`.
- Rules: walls end the game (no wrapping round), as does the snake's own
  body; the head may follow its tail. Two turns can be waiting, so quick
  corners work. Filling the board wins.
- Keys: arrows steer and start; Space or P pauses and carries on, and starts
  again after the end; F2 is a new game.
- Speed: Slow, Normal, Fast (a step every 200, 130, 80 ms) from the Game
  menu, worth 5, 10, 20 points a berry. The speed does not rise as the snake
  grows.
- The game pauses by itself when another window or a menu is in use, and
  when it is minimised. Closed in the middle of a game, it is still there,
  paused, when opened again.
- The best score lasts until restart; it is not saved to disk.
- It sits in Programs in alphabetical order with the other programs; there
  is no Games folder yet.
- The rules are in `core/games/snake` with no drawing or clock, tested in
  `host/tests/test_snake.cpp`; the timer runs only while the snake moves and
  each step repaints only the cells that changed.

### Chosen in the screensaver milestone (to confirm)

- The saver takes the whole screen: the window manager paints nothing while
  it shows (timers go on; everything is painted once when it ends) and the
  pointer is hidden. It does not start while a mouse button is held.
- What wakes it: any key press, any mouse button, or the pointer moving 4
  pixels or more from where it was (a nudge of the desk does not). The key or
  click that wakes the screen does nothing else. Letting a key go does not
  wake it. A full repaint asked for over the debug link ends it too.
- 30 frames a second, in fixed steps, from the window manager's tick. The
  savers change only the pixels that move, never the whole 300 KB screen,
  and use whole numbers only, so the PC and the board draw the same frames
  from the same seed (the seed is the clock when the saver starts). There is
  no link to the monitor's refresh yet, so a moving face may tear now and
  then; to look at on the bench.
- Starfield: 260 stars coming out of a point that drifts slowly, as if
  steering; dim, grey, then white as they near, two pixels wide when close;
  one in eight pale blue, yellow or orange.
- Flying Chinchillas: 14 mascot faces flying out of the distance, drawn at 1
  to 5 times the 16x16 picture, in four fur colours (white, grey, beige and
  pale "sapphire" blue). Each place a face was or is gets its final pixels
  in one pass, so nothing flashes and overlapping faces stay whole.
- Curves and Colours: two closed curves, each round four points that bounce
  off the edges of the screen, each with a trail of 12 copies running through
  the colour wheel, the two on opposite sides of it.
- A remote screen (VNC, the browser viewer) shows the saver too: each saver
  reports the parts of the screen it changes, never the whole screen, and a
  key or click from the remote side wakes it like any other.
- Default saver: Flying Chinchillas. No password. Both confirmed by the
  owner, 2026-11-16.
- The dialog: a list ((None) and the three), the wait in minutes (1 to 99),
  Preview (Alt+P), OK and Cancel. After a run it shows real figures from it:
  frames a second and the slowest frame.
- Settings are kept in `C:\.config\screensaver.ini` (section `[saver]`:
  `saver`, `wait_minutes`; see `docs/settings.md`), written by OK and read
  at start-up.
- Not measured on the board yet: frame times, tearing, how it looks on the
  monitor. The firmware was not compiled for this change (no ESP-IDF in the
  cloud); no firmware file changed, the shell installs the saver itself.
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

### Chosen in the remote screen milestone (2026-11-16)

- Who may connect: anyone on the same Wi-Fi network, no password (the
  owner's choice, 2026-11-16; the options were no password, a VNC password,
  or off until switched on). The viewer sees and controls everything.
- The server speaks RFB 3.3, 3.7 and 3.8 on port 5900, in the core
  (`core/net/rfb.hpp`), and runs on the UI task a little at a time (at most
  4 ms of encoding per round) without ever waiting for the network.
- One viewer at a time. A new viewer takes over from the one connected.
- Only what changed is sent: the window manager reports each rectangle it
  paints, kept as a grid of 16x16 cells and sent as rectangles of up to
  64x64.
- The pointer is not part of the picture: its shape goes to the viewer
  (cursor pseudo-encoding), so moving the mouse costs no screen updates. A
  viewer without that feature sees no pointer from the board, only its own.
- Pixels: the viewer's own format (8, 16 or 32 bits, or a colour map), by a
  table of the 256 screen colours.
- Compression: ZRLE (one colour, a palette of up to 16, or plain, then
  zlib), with our own small zlib compressor (fixed codes, 32 KB window)
  to stay free of a library and its memory. Viewers without ZRLE get plain
  pixels. Memory: about 190 KB of external RAM plus the compressor's, taken
  at the first connection.
- Keys: a key that exists on the US layout with the same Shift state goes
  in as that key; anything else goes in as text. Keys still held when the
  viewer leaves are released.
- In a browser (asked 2026-11-16, to confirm; built on the recommended
  option): the board serves one small page of its own at
  `http://<address>/` (port 80, about 9 KB, in the firmware), a viewer
  written for this server only. The other options put to the owner: the
  full noVNC kept on C: and served by the board, noVNC kept on the owner's
  computer, or no browser viewer. The page talks VNC through a WebSocket to
  the same port (`core/net/web.hpp`), which noVNC could use too. It asks for
  8-bit pixels with the colour map and ZRLE, unpacked with the browser's
  own zlib (`DecompressionStream`); a browser without it gets plain pixels.
  The web server answers nothing else: one page, one WebSocket, up to four
  requests at a time. Like the VNC port, no password.

### Chosen in the Cactus Patch game (2026-11-16); to confirm or overrule

The mine-sweeping game from the owner's list, built on its own branch and
tested on the PC only (not yet compiled for or run on the board).

- Name and theme: Cactus Patch. Cacti hide under the tiles, the player
  plants orange pennants as markers, and the mascot's face on the button
  shows how the game is going. The owner asked for the original game's name
  and look; that is not done (no copies of another product's name or art,
  as the rest of this file says), but it plays the same way. A plain "Mines"
  with mines and flags of our own drawing is the alternative if he prefers.
- Rules as in the genre: left click opens, right click marks, the first tile
  opened is never a cactus (nor are its neighbours when the field has room),
  an empty tile opens its area, and a click on a number with all its markers
  placed opens the rest around it. F2 or the face button starts again.
- Three sizes in the Game menu: Small 9x9 with 10 cacti, Medium 16x16 with
  40, Large 30x16 with 99. Tiles are 16x16; the window fits the field and
  cannot be resized.
- The status row shows cacti left (cacti less markers) and seconds up to
  999, in black digits on white boxes. The clock starts with the first tile
  opened. Closing the window ends the game.
- No best times yet: nothing keeps settings between restarts.
- The right mouse button now reaches windows: a press in a client area
  calls `Window::right_click()`. No program used it before.
- The rules are in `core/games/cactus.hpp`, the window and its pictures in
  `core/src/shell/cactus_patch.cpp`. Its tests and reference pictures are
  `host/tests/test_cactus.cpp` (pictures 50 to 54).

### noVNC from a CDN (asked 2026-11-16, to confirm)

- The owner asked whether noVNC could be used from a trusted CDN at a pinned
  version. It can, and is added as a second page, `http://<address>/novnc`,
  beside the built-in viewer at `/` so the two can be compared; which one
  stays at `/` (or both) is his to say.
- The page loads noVNC 1.7.0 from jsDelivr's mirror of noVNC's GitHub
  repository at that release's commit
  (`63107bd06d9e1f6136ff21aeda8cd62cbf0d433e`), not from npm: the npm
  package holds no files a browser can load directly, and a commit cannot be
  changed afterwards, where a version tag could.
- Integrity (added after the board check, 2026-11-16): the page carries the
  SHA-384 hash of each of the 52 noVNC files it loads, as the `integrity`
  part of an import map, and the browser refuses any file that does not
  match; the page then says so and points to the built-in viewer. So a CDN
  that served altered files could stop `/novnc` from working but not run
  its own code against the board. The hashes are made from a clone of noVNC
  at the pinned commit by `tools/novnc_integrity.py`
  (`core/src/net/novnc_files.inc`, 9.5 KB in the page); changing the pinned
  version means running it again. What is left: a browser too old to know
  import-map integrity ignores the hashes and loads the files unchecked
  (current Chrome, Safari and Firefox know it; the exact first versions
  have not been looked up), and the page itself comes from the board over
  plain http, like everything else it serves. One bundled file with a
  single hash was the alternative; noVNC publishes no such file, so it
  would have meant hosting our own build somewhere.
- Costs: the viewing computer needs the internet, and noVNC warns that it
  wants an https page, which the board does not have. It worked without one
  in Chromium against the simulator (same picture as the built-in viewer,
  mouse and keys), with the CDN's files served from a local copy because
  the cloud container cannot reach jsDelivr: the real address has not been
  fetched.

### Chosen for finding the board by name (asked 2026-11-16, to confirm)

- The owner asked whether the remote screen can be announced over mDNS. It
  can: the board answers to `chinchilla.local` and announces `_rfb._tcp`
  (5900) and `_http._tcp` (80), both named "Chinchilla", so VNC viewers that
  browse the network list it and `http://chinchilla.local/` opens the page.
  Done with Espressif's `mdns` component (1.15.1), in the Wi-Fi adapter.
- The name is fixed in the code for now. Put to the owner: another name, or
  a name he can change (in the Network panel, kept in
  `C:\.config\network.ini`). Two boards on one network would be told apart
  by the component itself, which adds a number to the second.
- Firmware 1.28 MB with it (1.21 MB before).

### Chosen for the files on the desktop (2026-11-16, to confirm)

- Icons go in columns down the left, 70 px apart, a new column (76 px to the
  right) when one is full: 5 rows at 640 x 480. Folders first, then files by
  name ignoring case, as in the File manager.
- At most 20 files show (24 icons in all); the rest are not shown.
- Folders in the desktop folder show and open in the File manager.
- Names starting with a dot are hidden. Names longer than 11 characters are
  cut to 9 and "..".
- The folder is read again every 2 s (a few directory reads) instead of
  hooks in every program that writes files.

### One-shot fast start for debug restarts (2026-11-16)

Asked for by the owner: restarts made by the agents skip the deliberately slow
start-up. `tools/dbg.py reboot --fast` (Reboot with flag bit 0) or
`tools/dbg.py boot fast-next` (Boot screen 2, for before flashing) arms one
fast start: the next boot behaves as `fastboot = true`, logs "one-shot fast
boot", and clears the flag; boot.ini is untouched. The flag is a word in NVS:
RTC no-init memory was tried first and survives a software restart, but the
reset after flashing cleared it on this board. The agents use it for their
own restarts and post-flash restarts, except when the start-up screens are
what is being tested or shown. Measured: desktop 0.27 s after the video
starts, against 11.3 s for a normal start.

### Submenus with a slow pointer (2026-11-16)

Found by the owner: moving slowly from Programs into its submenu closed it.
The submenu opens flush against its parent, and the pointer crossing the
parent's border (part of no item) counted as a change of item, which closed
the submenu at once; a slanted path closed it on the next item. Now, while a
submenu is open, borders, separators and other items of its parent change
nothing until the pointer rests on another item for 300 ms (each move starts
the wait again); a click on another item takes over at once. Menus close only
on a click outside, Escape or choosing an item. The same code serves every
cascading menu (Start menu folders, menu bar submenus). test_slow_submenu
moves 2 px every 20 ms, straight and on a slant.

### Chosen for painting each pixel once (2026-11-16, to confirm)

- Title bars, menu bars, menus, the taskbar, desktop icons, opaque widgets
  and small windows that draw over their widgets are painted through a
  scratch band of 12 screen lines (7.5 KB of internal RAM, `Canvas::layered`):
  the usual drawing calls run into the band, then each band is copied to the
  screen once. The screen itself stays a single picture (no double buffering,
  as decided); only the band is extra.
- Big widgets that already paint each pixel once (the Editor's text, the
  pictures in the viewer, the Culebra board) skip the band, because it runs
  a widget's painting once per 12 lines.
- test_paint counts screen writes for a full repaint, activating, moving and
  dropping a window, menus and submenus, minimising and restoring, the clock,
  dragging a file icon, and opening every program and dialog: no pixel is
  written twice. Measured on the board with Documents and the Editor open: a
  full repaint takes 35.0 ms (33.8 ms before).
- Escape cancels a file drag; a file let go on the empty desktop moves into
  C:\Documents\Desktop.

### Chosen for the Trash (2026-11-16, to confirm)

- The Trash is the hidden folder C:\.trash. A name already there gets a
  number: "notes (2).txt". C:\.config\trash.ini remembers where each entry
  came from, so File > Restore puts it back (numbered if the name is taken
  again, its folder made again if it is gone). The status bar shows where
  the chosen entry came from.
- Empty Trash is a button in the Trash window's toolbar (the owner: "an
  option in the navigation bar"), greyed out when empty, and in its File menu.
- Delete asks "move ... to the Trash?" first. Dragging onto the Trash does
  not ask. Delete inside the Trash, and Empty Trash (which says how many),
  remove for good after asking.
- Folders go to the Trash whole. Drives other than C: have no Trash: Delete
  there removes for good, and the question says so.
- The icon shows paper in an open bin, its lid leaning beside it, when the
  Trash holds anything (own art).
- The desktop icon is called "Trash" (it said "Recycle Bin").
- The Defragmenter does not read the disk (its picture is made up), so the
  Trash makes no difference to it; free space counts the Trash as used.
- Dragging: an outline 32 px square follows the pointer; the desktop icon or
  folder it would go into is highlighted. Folder-to-folder moves work too:
  onto a folder in a File manager window or on the desktop, or onto the
  window's empty space (into the folder it shows); same drive only. Desktop
  file icons can be dragged. Escape does not cancel a drag yet, and dropping
  on the empty desktop does nothing (it could move into
  C:\Documents\Desktop).

### Chosen for safe saves and the hourglass (2026-11-16)

Journal: `docs/journal/2026-11-16-safe-saves-and-hourglass.md`.

- Decided by the owner: the busy pointer is drawing A, the sand timer (15x22,
  own art), out of four (`docs/look/hourglass-options.png`). Not chosen: a
  round glass, the arrow with a small glass beside it, a timer with
  chinchilla ears.
- To confirm: the temporary file sits beside the document under a hidden
  name, `.~<name>.tmp` (not a visible `name.tmp`, not one `C:\.tmp` folder).
- To confirm: every whole-file write saves this way, the settings files
  included, not only the Editor.
- To confirm: the hourglass shows for every save and load in the Editor,
  however short, and does not move or animate. A temporary file left by a
  power cut stays hidden until that document is saved again.
- A save needs room for both copies until its last step; a disk too full
  refuses the save and keeps the old document.
- `Volume::rename()` still refuses an existing name (the Rename command);
  `Volume::replace()` is the one that takes a file's place.

## To confirm on the bench

- The picture is stable at 640x480 with the framebuffer in PSRAM.
- The colours and the four greys look right in both modes.
- Safe saves and the hourglass (PC only so far): the six checks in section 7
  of `docs/journal/2026-11-16-safe-saves-and-hourglass.md`.
- The picture stays stable with Wi-Fi active and during flash writes.
- Wi-Fi (never run on the board yet): the radio starts and how long it
  takes; a scan lists the networks around; a wrong password is reported as
  such and a right one connects; the board joins by itself after a restart;
  the clock is set from the network; free internal RAM with Wi-Fi up; the
  picture stays stable and the frame rate holds during a scan and while
  connected.
- Remote screen (compiled and linked for the board, never run there): a VNC
  viewer connects to the board's address, and a browser opens
  `http://<address>/`; the picture is right; keys and mouse work; how long a full-screen update takes and how the desktop feels
  meanwhile; the VGA picture stays stable with a viewer connected. If
  updates are slow, the UI loop's 10 ms pace (FreeRTOS at 100 ticks a
  second) is the first thing to look at.
- Finding by name (compiled and linked, never run on the board): the log
  says `answers to chinchilla.local (mDNS)`; `ping chinchilla.local` from
  the laptop answers; `http://chinchilla.local/` opens the viewer; the two
  services show in a browse (`avahi-browse -art` or `dns-sd -B _rfb._tcp`);
  free internal RAM with it (`tools/dbg.py memory`); the picture stays
  steady.

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

### Chosen for the process table (2026-11-16, to confirm)

Backlog item 2. The options and the reasons are in
`docs/journal/2026-11-16-process-table.md`. Each is easy to change. Checked on
the board and merged 2026-11-16: the PIDs there are init 1, dbgd 2, vncd 3,
httpd 4, desktop 5, netd 6, and the change costs 3.2 KB of internal RAM (1.2
for the table, the rest for Task Manager's rows).

- The table is `core/proc`: 32 rows, PIDs from 1 to 32767 that are not given
  out again until the count has gone round, a parent, a name, a state
  (running, exiting) and flags (system, protected, respawn, and "windowed":
  ends with its last window). The flags are stored; what protected and respawn
  do is backlog item 4.
- How the OS knows who is asking: the table holds the current process. The
  window manager sets it around every call into a program (keys, mouse, paint,
  timers, menu actions, close), and a window or timer made meanwhile belongs
  to it. Programs do not pass a PID. The other options were a PID in every
  call, or programs registering what they own.
- PID 1 is a row called `init` from the first moment, standing for the
  start-up code until init is a real program (item 18).
- One process per program, not per window: the File manager's folder windows
  and the Trash window are one process. The parent is whoever launched it
  (the desktop, or the File manager for a file it opens); when a parent ends,
  init adopts its children. Shared dialogs belong to whoever asked. The Screen
  Saver and Network windows are processes of their own; the Shut Down dialog
  is the desktop's.
- A window program ends when it has no window left at the end of the event
  that closed the last one, not at once (the Picture viewer's Open dialog
  closes before its picture window opens). Timers it left are stopped.
- **Reversed 2026-11-16 by the owner** (see "Only real tasks are listed"
  below): the services had rows with Unix-style names, `dbgd`, `netd`, `vncd`
  and `httpd`, that were names only, all running on the UI task. Those rows
  are gone. `desktop` (protected) stays: it is the UI task. The names are
  kept for the day each service is a task.
- Task Manager lists processes: PID, name and the main window's title. By
  default the programs with windows, without itself, as before; "Show all
  processes" shows the whole table. This replaces "every window that has a
  taskbar button" in the section above. End Task closes all the windows of
  the process; on a process without windows it says it cannot yet (signals,
  item 5). The other options were always the whole table, or two tabs. **Confirmed by
  the owner, 2026-11-16**, on a card with those three options.
- The table has no lock: one task uses it. The first item that starts a
  process from another task has to add one.

### Chosen for the message queue (2026-11-16, to confirm)

Backlog item 8, built on a branch, tested on the PC, compiled for the board
and not yet run on it. The options and the reasons are in
`docs/journal/2026-11-16-message-queue.md`. Each is easy to change.

- Other tasks never touch windows, timers or the process table. They post a
  message (`core/msg`), and the UI task runs it between two rounds of its
  loop.
- A message is a function to call on the UI task, addressed to a process:
  `msg::post(pid, fn, context, a, b)`. It is dropped if the process has ended
  by the time it is delivered, and its function runs as that process. PID 0
  is the OS itself and is always delivered. The other options were numbered
  messages to windows, as Windows did, or a bare function with no address.
  **Confirmed by the owner, 2026-11-16**, on a card with those three options.
- The queue holds 32 messages (0.8 KB of internal RAM with its wake-up
  semaphore, none from the heap). When it is full the sender chooses: give
  up at once, or wait for room for a given time.
- The UI loop sleeps until a message, serial bytes, or the window manager's
  next timer (`Wm::idle_us()`), one second at most. Serial bytes wake it
  through the serial driver's notification hook. Timers are as exact as
  before: one kernel tick, 10 ms.
- The network is still looked at by the clock, since nothing can wake the
  loop for a socket: every 10 ms while a viewer is connected or a web page
  is being sent, every 50 ms while only waiting for a connection, never
  with no port open. The other option was a watcher task (about 3 KB of
  internal RAM); it goes away by itself when the VNC server becomes a task.
  **Confirmed by the owner, 2026-11-16** ("Ok", after asking who would own
  the watcher: nobody does today, the UI task looks at the network itself).
- The UI task runs at most 16 messages per round, then timers and painting.

### Chosen for signals and flags (2026-11-16, to confirm)

Backlog items 4 and 5, built on a branch and tested on the PC only. The
options and the reasons are in `docs/journal/2026-11-16-signals-and-flags.md`.
Each is easy to change. This replaces what the two sections above say End
Task does.

- One function sends a signal: `proc::Table::signal(pid, signal)`, with TERM
  and KILL. It does nothing to the process: the signal waits on the row, and
  the window manager acts on it at its safe point (the end of each event and
  tick). It answers Ok, NoSuchProcess, NotPermitted (protected) or
  NotSupported (a service). The other option was to act at once in the
  sender's call.
- TERM asks every window of the process, as its close button would, after
  bringing it to the front. A window held by a dialog is not asked: the
  dialog comes forward and the program stays.
- KILL closes the process's menus and windows (dialogs first), stops its
  timers and removes its row, without `close_requested()`. `closed()` still
  runs, because window objects are used again by the next launch.
- Protected processes (`init`, `desktop`) refuse both signals. Task Manager
  greys End Task out on them.
- A process without windows cannot be signalled yet (NotSupported), and Task
  Manager has a message for it. Since the service rows were removed (see
  "Only real tasks are listed" below) no row gives that answer today. The
  first real service gets a stop function with its row (backlog item 5b).
- A signal sent from another task (the shell's `kill`, later) must go
  through the UI message queue (`msg::post`, `core/msg/queue.hpp`), so that
  `signal()` runs on the UI task and the sleeping loop wakes at once. Signals
  sent on the UI task, as End Task does, need nothing more. (Noted when the
  message queue merged, 2026-11-16.)
- End Task sends TERM. If the process is still there 5 seconds later, a box
  offers End Now (KILL) or Cancel, but only once the Task Manager is the
  window in front, so it never lands on a question the program is asking.
  End Task again on the same process offers at once. The other options were a
  box after 5 seconds whatever is on screen, or the button turning into "End
  Now". **Confirmed by the owner, 2026-11-16**, on a card with those three options.
- Respawn: `Table::set_restart(pid, start, context)`. When such a process
  ends, for any reason, its start function runs at the next safe point as
  init, and the new process is init's child with a new PID. No process on the
  real desktop has the flag yet (the taskbar gets it in item 21), and there
  is no brake on restarting too fast (with init, item 18).


### Chosen for the file system lock (2026-11-16, to confirm)

Backlog item 9. Journal: `docs/journal/2026-11-16-file-system-lock.md`.

- One lock for the whole file system, in `fs::Storage`, around every call
  (`Storage::set_lock()`, a `ports::Lock`). All drives share it. The other
  options were a lock in each drive, one lock per drive, a read-write lock, a
  file system task with a queue (the AmigaOS way), and `std::mutex` in the
  core.
- It is held for one call. The calls made of several steps (`begin_save`,
  `finish_save`, `read_file`, `write_file`) hold it across all of them.
  Between the pieces of a long read or write it is free.
- It is not recursive. Code that holds it must not call `Storage` again; a
  KILL must not take a task that holds it (see "Signals" above).
- It does not stop two tasks from working on the same file. A program that
  saves from a job locks its own window (item 10); the shell will need a
  rule.
- With no lock set, `Storage` is as before (the simulator and one-thread
  tests). The board sets one at start-up.

### Only real tasks are listed as processes (2026-11-16)

The owner's rule, 2026-11-16: "don't advertise non real tasks, it's
confusing". The story is in `docs/journal/2026-11-16-only-real-tasks.md`.

- A row in the process table stands for something that runs: a window
  program, or a task of its own. A service that is polled from the UI loop
  gets no row.
- The rows `dbgd`, `netd`, `vncd` and `httpd` are removed. The Wi-Fi
  manager's timer is the desktop's. Before any program starts the table is
  `init` 1 and `desktop` 2.
- A service gets its row, with the name planned for it, in the change that
  makes it a task (items 14, 18 and 26).
- `init` stays as PID 1 although it is not a task yet: it is the root of the
  tree and keeps the numbers stable until item 18. **To confirm with the
  owner.**
- The other option was to keep the rows marked "(part of the desktop)".
