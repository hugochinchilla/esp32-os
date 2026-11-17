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

### Dialogs and settings tools are programs (2026-11-17)

Decided by the owner; the story is in
`docs/journal/2026-11-17-dialogs-as-programs.md`.

- A new dialog or settings tool (Display is the example) is a program of its
  own, not a part of the desktop: its own row in the process table while open;
  no memory kept while closed (window, widgets and buffers created when it
  opens, given back when it closes); it reaches the desktop only through calls
  any program could make; its code is a file of its own.
- When this was decided every program was one fixed object on the UI task,
  and closing it freed nothing. Since "Program lifecycle" below (backlog
  items 19 and 34, the same day) no program or dialog is: all are made when
  opened and destroyed when closed, so nothing is owed any more.
- Not taken: a FreeRTOS task per dialog now (the stack costs more internal RAM
  than the dialog frees, and the widgets assume one task); waiting for
  loadable programs (item 27) before changing how dialogs are written.

### A BIOS stage in its own partition (2026-11-17)

Decided by the owner: the self-test moves to a BIOS, a small firmware of its
own in its own app partition, which ends by restarting into the OS; init
then shows the splash. First a restart is measured on the board (what the
monitor does, how long a second start takes), with no change to the flash.
Recommended and to be checked against ESP-IDF v5.5: a small hook in the
bootloader starts the BIOS unless a flag in RTC memory says the BIOS has
just run. Not chosen: one image with two logical stages (nothing would
really be separate); the BIOS inside the bootloader (about 32 KB, no
drivers, no PSRAM). Nothing is built. The details still open are under
Still open; the whole conversation is in
`docs/journal/2026-11-17-bios-stage.md`.

Measured on the board the same day
(`docs/journal/2026-11-17-bios-restart-measured.md`): a restart loses the
picture every time, for 3.5 to 3.9 s; the video signal is back 0.76 s after
the reset and the display needs about 3 s more. So the handover from the
BIOS to the OS is a blank of about 3.8 s that ends on the desktop, and a
splash would have to stay about 4 s to be seen. The owner accepted the blank
("Yes, go ahead").
The bootloader hook was confirmed by reading ESP-IDF v5.5's source (not
compiled): our own copy of `bootloader_start.c` starts an app of subtype
`test`, the BIOS, unless the reserved RTC memory
(`CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC`) says it has just run.

### BIOS stage: flash and details (2026-11-17)

Decided by the owner: the BIOS takes 1 MB from C:. The BIOS partition is
0x10000 to 0x100000, the two update slots stay 1.94 MB each (from 0x100000
and 0x2f0000), `otadata` at 0x4e0000, C: from 0x500000 with 11 MB; C: is
wiped once, NVS (the Wi-Fi networks) is not. Not chosen: a 384 KB BIOS with
two slots of 1.75 MB (tight for both); a 1 MB BIOS with a single OS slot (no
way back after a bad update). Not applied yet: the table changes when the
BIOS is built.

Taken as recommended, the owner did not object (to confirm): the BIOS's
settings live in NVS, not `boot.ini`; the BIOS shows the crash screen;
an OS image received over serial is a later step.

### BIOS screen font (2026-11-17)

Decided by the owner: the BIOS's screen is a true text screen, 80 by 30 characters of 8x16 on
640x480, in Spleen 8x16 (already in the repo and the firmware), chosen from
five free fonts drawn in `docs/look/bios-text/`. The mascot sits on top as a
picture over cells left empty, so the serial port can mirror the text
exactly. That the text screen replaces the earlier drawing
(`docs/look/boot/boot-b-self-test.png`) is the agent's reading of the font
choice, to be confirmed. Not chosen: Terminus, Unscii (the agent's
recommendation), Tamzen, GNU Unifont.

### Watchdog, crash record and error screen (2026-11-17)

Backlog item 7. Chosen by the agent under the owner's go-ahead to work
through the backlog; the options and reasons are in
`docs/journal/2026-11-17-watchdog.md`. Each is easy to change. Built and
tested on the PC, compiled for the board; not yet run on the board.

- **The watchdog is a heartbeat**: the UI loop reports each time it comes
  round, and a supervisor checks once a second how old the last report is.
  This catches a program that spins and one that waits for ever. The
  supervisor is a callback on ESP-IDF's timer task (priority 22, core 0): no
  task and no stack of its own, nothing added to the video interrupt, the
  Wi-Fi task or the network task. ESP-IDF's own task watchdog stays as it
  was (a warning on the serial port, no restart).
- **15 seconds** of silence, `[watchdog] timeout_s` in `C:\.config\boot.ini`
  (5 to 600; 0: off). The start-up, before the loop runs, is not watched.
  `sys::Watchdog` has room for four tasks; only `ui` is watched today.
- **After a hang the machine restarts at once**, without the shutdown
  handlers (they could wait on what the stuck task holds).
- **Crashes** (bad pointer, `abort()`, assert, stack overflow, interrupt
  watchdog) are caught by wrapping ESP-IDF's `esp_panic_handler` at link
  time; ESP-IDF's own handler runs afterwards as before, so the serial
  report is unchanged.
- **The crash record** is 188 bytes in memory a restart does not clear
  (`RTC_NOINIT_ATTR`), checked by magic word, version and checksum, reported
  once. It names the program (from the process table, when the failing task
  is the UI task), the task, running or waiting, the uptime, and for a crash
  the reason, the addresses and up to 16 return addresses. For a hang it
  does not say where the program was stuck. Nothing is written to flash at
  the moment of the crash. With no record, an abnormal reset reason from the
  chip (brownout, hardware watchdog) is reported instead.
- **The error screen** is a full screen at the next start, before the
  self-test and also on a fast start, in the self-test's style with the
  mascot on its back (own design). It counts down 15 seconds and goes on by
  itself (`[boot] crash_hold_s`; 0 waits for a key), because a bare board has
  nothing to press a key on; a key or mouse button ends it. The debug link
  answers while it shows.
- **Reports are kept** in `C:\crash.txt`, added to at the start after the
  crash; the file starts again past 16 KB.
- **Testing**: `tools/dbg.py fault hang|pointer|abort [--pid N]` makes the
  board fail on purpose, charged to a program if asked; `tools/dbg.py boot
  crash` draws the screen.
- Not taken: ESP-IDF's task watchdog with panic switched on (it would make
  every 5 s of honest work fatal and names no program); a supervisor task of
  its own (a stack for nothing); a core dump partition (can be added
  later); a message box on the desktop instead of the screen; a longer limit
  while the hourglass shows.

### Program lifecycle (2026-11-17)

Backlog items 19 and 34. Chosen without asking first, under the owner's
"work through the backlog" of 2026-11-16; each can be overruled. The options
and reasons are in `docs/journal/2026-11-17-program-lifecycle.md`.

- **A program's window is made when it opens and destroyed once it has
  closed**, with its widgets and whatever buffers it asked for. No fixed
  window objects are left in `core/src/shell/`. Of a closed program only the
  code and, for one that runs once at a time, one pointer remain.
- **The memory is external RAM**, through the Memory port
  (`ports::Memory::allocate` and `release`). It is where the same objects
  were as fixed ones since the day before, so internal RAM is not touched
  and its heap is not cut up by programs coming and going.
- **The window manager destroys a closed window at its safe point** (the end
  of the event or tick, where process rows go and signals take effect), never
  while the window's own code is running. `wm::Window::set_dispose` marks a
  window for it; the shell's `make_window`, `one` and `open_one`
  (`core/src/shell/internal.hpp`) are how programs use it. A new window or
  dialog is written this way; it is no longer possible to "owe" the memory.
- **The Editor can run several times** (four at most), each a process with
  its own window, text and dialogs; a file already open in one comes to the
  front. The File manager keeps its one process with up to three windows.
  Every other program runs once at a time (`shell::App::many` switches it).
- **Open, Save As and the text question are made per asker**; two programs
  can each have one up. The message box stays one at a time.
- **Settings tools are processes of their own** (Display, Screen Saver,
  Network, Task Manager, and now Shut Down). **A dialog a program asks for**
  (message box, Open, Save As, Options, the Wi-Fi password) is made and
  destroyed the same way but belongs to the process that asked: "Save
  changes?" is the Editor's, which End Task and Shut Down rely on.
- **Closing a program ends it.** What should outlive that goes in its
  settings file: Culebra's best score and speed, Cactus Patch's size. A game
  half played, the Defragmenter's pretend disk and Task Manager's graph are
  not kept.

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
- ~~The quick-launch icons are drawn but do nothing yet.~~ Taken out on
  2026-11-16 at the owner's request: the taskbar shows only what works
  (Start, the window buttons, the Wi-Fi signal icon, the clock). They come
  back when they launch something. The window buttons start at x = 72.
  They are back with item 30 ("Chosen for the quick-launch icons"), and the
  window buttons start after them.
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
- The desktop's background is one of 16 named colours with one of 10
  patterns of 8x8 over it (the first is none), drawn in a darker shade of
  the colour. Chosen in the Display dialog, from Start > Settings >
  Display or a right click on the desktop; kept in `desktop.ini`. Asked for
  by the owner on 2026-11-17; the lists and the shade are the agent's
  defaults, his to change. The default is the blue it always was.
- Every program has its own icon at 32x32 and 16x16, drawn by
  `tools/mockup/icon_proposals.py` and chosen by the owner on 2026-11-16
  (sheets in `docs/look/icons/`). The Editor's is a spiral notepad and a
  pencil, his pick of three. The 16x16 serves the Start menu, title bar,
  taskbar and Task Manager; there is no smaller size. A process with no
  window shows a cog. This replaces the borrowed icons listed below.
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
  three File manager windows. Back has a blue arrow pointing left and Up a
  folder with a green arrow rising from it (they shared the folder icon
  until 2026-11-16). My Board lists the drives. Deleting a folder
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
  pixels. Memory: about 183 KB of external RAM, the compressor's included,
  taken when a viewer connects and given back when the last one leaves
  (2026-11-17, `docs/journal/2026-11-17-remote-screen-memory.md`): a service
  nobody is using holds no buffers, the same rule as for windows. Giving it
  back after a pause was rejected: asking for the memory again costs nothing
  that can be measured.
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

### Chosen for the viewer's system keys (2026-11-17)

- Alt+Tab, Alt+F4, Ctrl+Alt+Del and the logo key never reach a web page: the
  viewing computer keeps them. The board's page has a button for each
  (`docs/journal/2026-11-17-viewer-system-keys.md`). The board's side needed
  nothing.
- The buttons go where the picture leaves more room (right edge or top);
  with no room they lie faint on its top right corner.
- Buttons only (the owner's choice, 2026-11-17). Not built: the browser's
  Keyboard Lock (Chrome only, refused to a page served over http, never
  gives Ctrl+Alt+Del) and stand-in keys such as Ctrl+Alt+End.

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

### noVNC from a CDN: dropped (the owner, 2026-11-17)

- **Dropped** (the owner, 2026-11-17: "drop the novnc"). Our own viewer at
  `/` works without the internet and is enough. The `/novnc` page and its
  file hashes cost about 9.4 KB and 10.9 KB of firmware; removing them,
  `tools/novnc_integrity.py` and the web server's support for a second page
  (it had no other user) made the firmware 11,056 bytes smaller.
  `/novnc` now answers 404. The notes below are what was built and why, kept
  as the record. Journal: `docs/journal/2026-11-17-drop-novnc.md`.
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
Changed on 2026-11-17: see "Submenus hand over at once" below.

### Our static objects in external RAM (2026-11-17)

Decided by the owner, on the internal RAM card ("Move our objects to external
RAM"). The core's static objects (windows, dialogs, listings, rectangle
lists, settings buffers) live in external RAM: `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`
and the `extram_bss` scheme for libcore in `core/linker.lf`, so `core/` needs
no ESP-IDF header. In the firmware, the remote-screen server, the debug
protocol, the Wi-Fi manager, network.ini, the web gate, the Wi-Fi scan
records and the start-up timeline are marked `EXT_RAM_BSS_ATTR`. Kept in
internal RAM: the pointer's code and data (the video interrupt composes it),
the paint band (`canvas.cpp`, written and copied on every repaint), the
window manager object (every event and repaint goes through it; with it in
external RAM one refill was missed once in three runs), the bounce buffers,
and everything ESP-IDF places itself. Free internal RAM 97,963 -> 218,799
bytes, largest block 31.7 -> 139 KB; repaints within 1 %; 0 late, 0 missed
refills. New permanent static windows still owe their memory (see "dialogs
as programs").

### Chosen for the desktop program (2026-11-17, to confirm)

Backlog item 32; the owner asked for it and left the details to the agent
("dejo a tu criterio"). Journal: `docs/journal/2026-11-17-desktop-program.md`.
Build and use: `README.md`, "Chinchilla as a desktop program".

- `host/desktop/main.cpp` is to the PC what `firmware/os/main/main.cpp` is to
  the board: adapters and the loop, on SDL2. `core/` is not changed. It is
  built only where SDL2 is installed; the PC tests need nothing new.
- The window can be any size. The picture keeps 4:3, centred, black around
  it. It is first drawn a whole number of times larger with hard pixel
  edges, then smoothed down to fit: sharp at 1x, 2x, 3x, even in between.
- Chinchilla's own pointer is shown and the PC's is hidden over the window.
  Mouse positions are absolute; the mouse is not captured.
- Keys go to the core by their place on the keyboard (USB codes, which is
  how SDL numbers them); the core types US characters, as on the board.
  Keyboard layouts belong in the core, for the board too.
- The PC keeps F11 (whole screen) and F12 (a picture of the window).
  Ctrl+Alt+End stands in for Ctrl+Alt+Del.
- C: is a folder, `~/.local/share/chinchilla/C` unless `--drive` names
  another. Ordinary files; a new folder gets the samples.
- No sound. The core has no sound port and makes no sound yet, so there is
  nothing to play. The program gets sound in the change that gives the core
  its sound port.
- No Wi-Fi: Network says it is not available. The simulator's pretend
  networks are not shown to people who are not testing. The clock is the
  PC's.
- The start-up screens show, as on the board (`--fast`, a key or a click
  skips them), without the wait for the monitor.
- Closing the window is Shut Down: programs are asked first. A second close
  within 3 seconds leaves at once. Restart starts the program again.
- The memory figures in Task Manager and the self-test are the simulator's
  pretend ones, not the PC's and not limits.
- An `inetd` that serves nothing (the owner's choice, 2026-11-17), here and
  in the browser: the start-up file is the board's, which starts inetd,
  and without one every new C: opened with a "Start-up" box. The file stays
  one file for every machine. The row has no task behind it, the one
  exception to that rule. Journal: `docs/journal/2026-11-17-empty-inetd.md`.

### Chosen for the browser version (2026-11-17, to confirm)

Backlog item 33, made by the agent under the owner's free hand. Journal:
`docs/journal/2026-11-17-chinchilla-in-the-browser.md`. Build and use:
`README.md`, "Chinchilla in a web browser".

- One source for the PC and the browser: `host/desktop/main.cpp`, with the
  differences marked `__EMSCRIPTEN__`. `web/` holds the Emscripten build
  and the page. `core/` is not changed.
- The start-up screens show as on the board. They wait in a loop, which a
  browser allows only with Emscripten's Asyncify, so it is built in.
- C: is kept in the visitor's browser (IndexedDB, through Emscripten's
  IDBFS with `autoPersist`), read back before anything starts. Nothing
  leaves the visitor's computer. In a private window it lasts until the
  page closes.
- The canvas is the whole page and the program scales the picture into it
  with the same code as in a window.
- The whole thing is one file, `index.html`, 1.7 MB: it can be put on any
  server or sent as it is.
- Publishing: GitHub Pages, built by a workflow on every change to `main`
  (`ci/github-pages-workflow.yml`). Not active until the owner copies it
  and turns Pages on, as with the build workflow. The built page is not
  committed to the repository.
- Restart loads the page again. F11 and F12 are the browser's.
- The browser build does not treat warnings in `core/` as errors
  (2026-11-17, after the first Pages run failed on one): only it uses
  Clang, and a warning GCC does not give must not stop the publication.
  Its own code still does. Journal: `docs/journal/2026-11-17-pages-first-run.md`.

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
  meanwhile; the VGA picture stays stable with a viewer connected. (The
  remote screen runs on the services task since item 8b; the UI loop's pace
  no longer limits it.)
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
- **Replaced 2026-11-17 by the services loop** (next section): the network
  was still looked at by the clock, since nothing can wake the
  loop for a socket: every 10 ms while a viewer is connected or a web page
  is being sent, every 50 ms while only waiting for a connection, never
  with no port open. The other option was a watcher task (about 3 KB of
  internal RAM); it goes away by itself when the VNC server becomes a task.
  **Confirmed by the owner, 2026-11-16** ("Ok", after asking who would own
  the watcher: nobody does today, the UI task looks at the network itself).
  For now only: the owner found it odd that the UI looks after the network
  (a board with no monitor might not run the UI at all) and chose, on a card,
  a **services loop** as its own backlog item (8b): one task shared by the
  network services, asleep on all their sockets, that reaches the desktop
  only by messages. The other options were to build it in the same change or
  to leave the network in the UI loop.
- The UI task runs at most 16 messages per round, then timers and painting.

### Chosen for the services loop (2026-11-17, to confirm)

Backlog item 8b, built on a branch, tested on the PC, compiled for the board
and not yet run on it. The owner chose the services loop itself (above); he
was away for the rest, so the points below are the agent's and his to
change. The options and the reasons are in
`docs/journal/2026-11-17-services-loop.md`.

- One task, `inetd`, runs every network service: the remote screen (VNC) and
  its web page today, the telnet listener next. 4 KB of stack in internal
  RAM, priority 4, just under the UI task. It is started after the desktop,
  and only if a port could be opened. It has a row in the process table
  (system flag), one row for the one task; `vncd` and `httpd` stay unused
  names. The other options were a task per service, or leaving the network
  in the UI loop.
- It sleeps in `select()` on all the services' sockets (`net::Services`,
  `net::PosixWait`). Each service says what it waits for (`watch()`); the
  loop knows nothing of VNC. With nobody connected it does not wake at all.
  The UI loop no longer looks at the network.
- The services task never touches windows, timers or the process table. A
  viewer's keys, mouse and typed text reach the window manager as messages
  (`msg::PostedInput`); when the queue is full the sender waits a second,
  then the event is lost and counted. Anything a later service needs from
  the process table also goes through `msg::post()`.
- The grid of what was painted, written by the UI task and read by the
  services task, has a lock (a `ports::Lock`). The screen's pixels have
  none: a block read while it is being painted is sent again, because the
  window manager reports a rectangle after painting it. The other option
  was a lock around painting.
- While a viewer waits for the screen to change, the services task looks
  every 10 ms, since the UI task cannot end a `select()`. The other option,
  an event the UI task signals (ESP-IDF's `eventfd`), is left for later.
- A service with work left comes straight back; after 40 ms without a sleep
  the loop rests 10 ms, so lower tasks and the idle task always run.
- A departing viewer's held buttons are released where that viewer last had
  the pointer.

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
- Protected processes (`desktop` today) refuse both signals. Task Manager
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
  `desktop` 1.
- A service gets its row, with the name planned for it, in the change that
  makes it a task (items 14, 18 and 26). The first: the network services
  became one task with one row, `inetd`, in item 8b (2026-11-17).
- There is no `init` row either, until init is a real program (item 18).
  **The owner's decision, 2026-11-16**: "make desktop pid 1 for now". The
  first process made is the root of the tree, PID 1, and today that is the
  desktop. Orphans go to the root and respawned processes are its children.
  When init exists it takes PID 1 and the desktop becomes its child. This
  replaces "PID 1 is a row called `init` from the first moment" in "Chosen
  for the process table", and "init is PID 1" under "OS architecture" holds
  again from item 18.
- The other option was to keep the rows marked "(part of the desktop)".

### Chosen for the TTY (2026-11-17, to confirm)

Backlog item 12. Journal: `docs/journal/2026-11-17-tty.md`. Chosen by the
agent under the owner's "dejo a tu criterio" of 2026-11-16.

- `tty::Tty` (`core/include/core/tty/tty.hpp`) sits between a device (it
  calls `input()` with typed bytes and is a `ports::ByteSink` for what is
  shown) and a program (`read()` a line, `write()` text).
- The TTY edits the line, as Unix's cooked mode does, so every device and
  every program gets the same editing. The other options were the shell
  (readline's way) and each device. A raw mode hands keys over as they come.
- More editing than Unix: arrows, Home, End, Delete, Ctrl+A/E/W/U/K, and
  earlier lines with up and down (384 bytes of them), all in the TTY. Tab
  does nothing until the shell can complete names.
- The line is redrawn with characters, spaces and backspaces only, no escape
  codes. Known limit: editing the middle of a line that has wrapped looks
  wrong on a real terminal. Lines stop at 159 characters.
- A program waits for a line on a new port, `ports::Event` (`set()`,
  `wait(ms)`); without one `read()` never waits. The board's adapter comes
  with item 14.
- Ctrl+C drops the typed line and the lines typed ahead, shows `^C` and
  raises a flag: `read()` answers `kInterrupted` once, a busy program looks at
  `interrupted()`. Making it the INT signal of the process table is the
  shell's job (item 13). Ctrl+D on an empty line, and the device going away,
  are "end of input".
- Text is UTF-8 at both ends (typed bytes, echo, `read()`, `write()`), like
  the rest of the OS's interfaces. Only the line being typed is Latin-1
  inside, one byte per column; a character Latin-1 lacks becomes `?`. The
  other options were Latin-1 everywhere and UTF-8 everywhere.
- After a Ctrl+C, what the program writes is dropped until it has taken
  notice. `input()` raises the interrupt before asking for the lock, so a
  command that prints without pause still sees it.

### Chosen for the command shell (2026-11-17, to confirm)

Backlog item 13. Journal: `docs/journal/2026-11-17-command-shell.md`. Chosen
by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- The command shell is `sh::Session` in `core/sh/` (`core/shell/` is the
  desktop). It reads lines from a `tty::Tty` and knows nothing about the
  device. `run()` is the whole session for a task of its own; `greet()` and
  `step()` are the same in pieces for whoever has no task to give.
- Commands are built in, found in any case, each with its Linux name and its
  DOS names: `ls dir`, `cd chdir`, `pwd`, `cat type`, `cp copy`,
  `mv move ren rename`, `rm del erase`, `mkdir md`, `rmdir rd`, `touch`,
  `df`, `echo`, `ps tasklist`, `kill taskkill`, `free mem`, `uptime`,
  `ver uname`, `cls clear`, `help ?`, `exit logout`. `X:` changes drive.
- Options are written with `-` only (`ls -l`, `rm -r`, `kill -9`). A `/` is
  always part of a path, because paths take both slashes. DOS's `/w`, `/s`
  are not understood. The other options were DOS switches only, or guessing.
- One folder the session is in, not one per drive as DOS keeps. A path with
  a drive letter counts from that drive's root.
- `*` and `?` in the last name of a path, for `ls`, `cat`, `cp`, `mv`, `rm`,
  `rmdir`. Each command expands them itself (the DOS way); there is no heap
  to hold a list. A star does not match names that start with a dot, so
  `del *` leaves `C:\.config` and `C:\.trash` alone.
- `rm` deletes for good; it does not use the Trash. `rm -r` and `rmdir`
  refuse the folder the session is in and a drive's root.
- `mv` onto an existing file replaces it in one step. Between drives it
  copies and deletes, files only. `cp` writes the safe way (temporary file,
  then rename).
- `ps` and `kill` run on the UI task through a `sh::UiCall` (a message into
  the UI queue and an `Event` to wait on), because the process table is the
  UI task's. A session on the UI task itself calls straight in.
- Ctrl+C is the TTY's flag; the commands that loop look at it between
  pieces. It is not yet a signal of the process table, since built-in
  commands are not processes.
- `type` shows a file as Latin-1, as the Editor reads it.
- `date` came with item 24; redirection, pipes and `more` came with item 23
  ("Chosen for pipes and redirection" below); `open`,
  `run` and `edit` came with item 16, `top` with item 3; `dmesg` is still
  to come.
- `build/host/chinchilla-console [FOLDER]` is the shell in a terminal of the
  PC, for trying it without a board.

### Chosen for the Terminal window (2026-11-17, to confirm)

Backlog item 15. Journal: `docs/journal/2026-11-17-terminal-window.md`.
Chosen by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- "Terminal" in Start > Programs (`core/src/shell/terminal.cpp`): a window
  that is the device of a TTY, with a `sh::Session` behind it. Up to three at
  once, each a process.
- Changed the same day: the session is now a process of its own, `sh`, and
  the window only an emulator. See "The Terminal is an emulator; the
  session is a process of its own" below; what follows about the task, the
  lock and the kits holds for the session.
- **Its session is a job of the Terminal's process, on a task of its own**,
  wherever the machine has tasks (2026-11-17,
  `docs/journal/2026-11-17-terminal-task.md`): the desktop goes on while a
  command works and Ctrl+C stops it. Closing the window, or End Task, hangs
  up the TTY; the process goes when the session's task has returned. The
  TTY's lock guards the grid as well, and the session's task tells the UI
  task to paint with one message until it has looked. The lock and the two
  events of a session come from a pool the machine hands in
  (`shell::set_session_kits`, three on the board). Its task asks for a 12 KB
  stack, to be measured.
- Without tasks (the browser, the PC desktop program) the session runs on
  the UI task, a line at a time (`Session::step()`), as it was first built:
  the desktop waits while a command works.
- The grid is `tty::Screen`: 80 by 25, fixed, 6x12 font. It knows CR, LF,
  backspace, tab, bell, and the ANSI sequences to erase (`J`, `K`) and to
  place and move the cursor (`H`, `A` to `D`); others are ignored. A line
  wraps at the right edge and a backspace goes back over the wrap. The other
  options were a glass teletype and a full VT100.
- 175 lines above the screen are kept (16 KB with the screen, in the
  window's own memory). Page Up and Page Down look back. No scroll bar.
- Light grey on black, a blinking block cursor, a line under the character
  when another window has the keyboard. The icon was drawn by the agent.
  Both are for the owner to overrule.
- Escape is not sent to the session.

### Chosen for pipes and redirection (2026-11-17, to confirm)

Backlog item 23. Journal: `docs/journal/2026-11-17-pipes.md`. Chosen by the
agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **A pipe is a hidden file and the commands run one after the other**, as
  on MS-DOS: `A | B` runs A with its output in `.~pipeNa.tmp` in the root of
  the session's drive, then B with that file as input, then removes it. Up
  to six commands on a line. Rejected for now: real pipes with a task per
  command (the right way once a program that never ends needs one; the
  commands would not change), and a buffer in memory (it has a size).
- `> FILE` writes a file, `>> FILE` adds to it, `< FILE` reads one; anywhere
  on the command, quoted names allowed. `<` only on the first command of a
  pipe and `>` only on the last. No `2>`, `&&`, `;` or variables.
- **Errors always go to the terminal**, never into the file or the pipe.
- A file gets Latin-1 and the terminal UTF-8: the session converts what a
  command prints when its output is a file, and typed input when a command
  reads it. The filters work in Latin-1.
- The filters: `more`; `grep` with the DOS names `find` and `findstr`
  (plain text, not patterns; `-i -v -n -c`); `head` and `tail` (`-n N`;
  `tail` shows 2 KB at most); `wc` (`-l -w -c`); `cat`/`type` without a file
  copies its input. `ls` writes one name a line into a file or pipe. No
  `sort` yet.
- A command reads typed lines (until Ctrl+D) only where the session has a
  task of its own. On the UI task it says what it needs instead of waiting,
  and `more` does not stop between screens.

### Chosen for Run and the shell's open, run and edit (2026-11-17, to confirm)

Backlog item 16 in part. Journal: `docs/journal/2026-11-17-run.md`. Chosen
by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **Start > Run...** is a program of its own (`core/src/shell/run.cpp`). It
  takes a program's name (any case, or the start of it when only one begins
  so) or a path (a folder opens in the File manager, a file with its
  program; without a drive it is looked for on C:). It does not take shell
  commands. When nothing can be opened the dialog stays, under a message
  that says why. The last thing run is kept in `C:\.config\run.ini` and is
  there, selected, the next time. No list of earlier entries.
- **The shell reaches the desktop through a port**, `sh::Desktop`, so it
  still builds and runs without one. `open PATH...` (DOS name `start`),
  `run [PROGRAM]` (alone: the list) and `edit [FILE...]`. A failure is a
  line on the terminal, not a message box.
- `edit` opens the desktop's Editor, and a name that is not there yet is
  made as an empty file. A console editor is item 22.
- A program started from Run or from a Terminal is the desktop's child.
- `top` waits for item 3 and `dmesg` for a log kept in memory.

### Chosen for per-process figures and top (2026-11-17, to confirm)

Backlog item 3, and `top` from item 16. Journal:
`docs/journal/2026-11-17-process-figures.md`. Chosen by the agent under the
owner's "dejo a tu criterio" of 2026-11-16.

- **Processor time** is the UI task's time while the process was current
  (the table has a clock and charges each change of the current process),
  plus the run time of its jobs' tasks, added whenever the figures are
  shown (item 11) and when each job ends. Time with
  no process current is the OS's and is in no row. Rejected: a task per
  program (a different architecture), and sampling (coarse).
- **Memory** is what the process's windows asked the Memory port for and
  have not given back, plus the stacks of its running jobs. A 16-byte note
  in front of each block says whose it is. Memory given out at start, and
  what the services take from the heap, is in no row.
- **Shown** as two columns, "Time" and "Mem", in the Task Manager (64 pixels
  wider) and in `ps`. Time as `4.2s`, `3:07`, `1:02:45`; memory as `640K`,
  `1.2M`, `-`.
- **`top`** sorts by share of one core since its last look, every two
  seconds, until `q` or Ctrl+C; `-n N` for N screens. Without a key to wait
  for, or into a file or a pipe, it prints one table of totals and returns.

### Chosen for the priority classes (2026-11-17, to confirm)

Backlog item 11. Journal: `docs/journal/2026-11-17-priorities.md`. Chosen by
the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **Low 3, Normal 4, High 5** as FreeRTOS priorities; the desktop's task has
  5, the video 10. Low and Normal never keep the desktop from an event;
  High takes turns with it. Rejected: High above the desktop (a busy job
  would freeze it).
- **A class per job** (`job::Spec::priority`, Normal by default), named in
  the portable code as `ports::Priority` and turned into a number by the
  adapter. The services' task (`inetd`) is Normal by the same function.
  Rejected: a priority per process set from the Task Manager (the windows
  share the desktop's task, so it would only reach the jobs).
- **A task does not delete itself.** When its function returns it sleeps,
  and the job runner deletes it once the result has reached the UI task. So
  a running job's processor time can be read safely, and the Task Manager,
  `ps` and `top` show it while the job runs
  (`proc::Table::bring_up_to_date()`).

### Chosen for file dates (2026-11-17, to confirm)

Backlog item 24. Journal: `docs/journal/2026-11-17-file-dates.md`. Chosen by
the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **One date per file: when it was last written**, kept as seconds since
  1970 in UTC (`ports::DirEntry::modified`) and turned into local time when
  shown. On the board it is the flash file system's own date.
- **A file written while the machine does not know the time has no date**
  (shown blank). The board learns the time from the network; a date before
  2000 is taken as not known. Rejected: showing 1970; stamping later. Open
  for the owner: starting from the last time kept in flash, or a clock chip.
- **Shown as `2026-11-17 14:05`**: a first column in `dir` and `ls -l`, and
  in the File manager's status bar for the chosen file. `date` prints the
  day and time. Left for later: a Details view in the File manager.

### Chosen for the quick-launch icons (2026-11-17, to confirm)

Backlog item 30. Journal: `docs/journal/2026-11-17-quick-launch.md`. Chosen
by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **Three icons by the Start button: File manager, Editor, Terminal.** One
  click starts the program, as from the Start menu. Rejected for now: "show
  the desktop" (there is no minimise-all), and a list the user edits (needs
  shortcuts as files).
- **They behave like the other taskbar buttons**: flat, pressed in while
  held, acting on release. No raised edge under the pointer, no tooltip.
- **The window manager draws them; the desktop says what they are**
  (`Wm::set_quick_launch`, four at most). The window buttons start after
  the last one.

### Chosen for the console editor (2026-11-17, to confirm)

Backlog item 22. Journal: `docs/journal/2026-11-17-console-editor.md`.
Chosen by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **`ted`: a full-screen editor with no modes.** `edit` opens the desktop's
  Editor where there is a desktop and `ted` where there is none. Rejected:
  a line editor; modes as in vi; the name of an existing editor.
- **Keys**: Ctrl+S (or Ctrl+O) saves, Ctrl+Q (or Ctrl+X) leaves and asks
  first if there are changes, Ctrl+F and Ctrl+G find, Ctrl+K cuts the line
  out. Ctrl+C does not close it. No undo, copy or replace yet.
- **It sends only the rows that changed**, with three escape sequences
  (erase the screen, place the cursor, erase to the end of the line).
- **The text is one block of 128 KB** from the Memory port, taken while ted
  is open and counted as the Terminal's; that is the largest file it opens.
- In the Terminal window, Page Up and Page Down go to a program that has
  taken the keys (ted), and to the history otherwise.

### Chosen for telnet (2026-11-17, to confirm)

Backlog item 17. Journal: `docs/journal/2026-11-17-telnet.md`. Chosen by the
agent under the owner's "dejo a tu criterio" of 2026-11-16, except the
default, which the owner decided.

- **On by default** (the owner's decision, 2026-11-17, on a card in the
  thread; it turns round what the agent had built). Port 23, no password
  (the owner's rule: no users). `[telnet] enabled = false` in
  `C:\.config\network.ini`, read at start-up, switches it off. The other
  option, off until the file says otherwise, was what the agent had
  recommended, because a shell that deletes files appears on any network
  the board joins; the owner accepts that.
- **One session at a time**; a second caller waits. Rejected: several, until
  someone wants them.
- **The socket work is on the services task and never waits; the session
  runs on a task of its own**, started at connect and given back at leave.
  Rejected: a task blocked on the socket.
- **A program that writes faster than the network takes waits** (the TTY
  asks for room before each 256-byte piece, `Tty::on_room`); typed bytes
  keep arriving meanwhile, so Ctrl+C works. Rejected: dropping output.
- The client is asked for character mode with our echo and for its window
  size; nothing else.

### Chosen for init (2026-11-17, to confirm)

Backlog item 18. Journal: `docs/journal/2026-11-17-init.md`. Chosen by the
agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **`init` is PID 1 and the desktop is its child, PID 2.** This ends "make
  desktop pid 1 for now" (see "Only real tasks are listed as processes"),
  as that note said it would. init has no task of its own: it is the UI
  task's loop, the code that runs before and between programs. Rejected: a
  task that only sleeps; no row at all.
- **The start-up file is `C:\.config\startup.txt`**: `start NAME` for a
  part of the system (`desktop`, `inetd`), `run WHAT` for a program or a
  file as Start > Run... takes it, `#` for a comment, done from the top
  down. Rejected: an .ini file (order is the point); a shell script (maybe
  later); names without verbs. *Changed 2026-11-17 (the owner's choice): the
  lines are fields, `WHAT:ACTION`, as in inittab; see "init starts again
  what ends". The verbs are not read any more.*
- **Without a file the usual lines are used and the file is written.** A
  wrong line is logged and skipped. The desktop is started even if no line
  asks, until there is a console to fall back on (item 14).
- **The network ports open when `inetd` starts**, after the desktop in the
  usual file. Telnet stays a service of `inetd`, switched in `network.ini`.
- **The brake**: a respawn program that ends within 2 seconds of starting,
  five times in a row, is not started again. Rejected for now: waiting
  longer each time.

### Chosen for the serial console (2026-11-17, to confirm)

Backlog item 14. Journal: `docs/journal/2026-11-17-serial-console.md`.
Chosen by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **A zero opens a frame; outside a frame every byte is typed** and goes to
  the shell at once. This sharpens "bytes between two zeros that pass the
  check go to the debug service, everything else to the console" under "OS
  architecture": a person never sends the closing zero. A frame's bytes
  never reach the shell, also when the frame fails its check.
- **A frame left open with nothing arriving for half a second is given up**
  and what it held is taken as typed. Rejected: a limit from the opening
  zero; no way out.
- **The session's output is plain text between frames.** Rejected: frames
  of its own.
- **Off unless the start-up file says `start console`** (12 KB of internal
  RAM, and a shell on the port). Rejected: always on.
- After `exit` the next session greets. The row is `console`, protected.

### Chosen for the system log and dmesg (2026-11-17, to confirm)

The last piece of backlog item 16. Journal:
`docs/journal/2026-11-17-dmesg.md`. Chosen by the agent under the owner's
"dejo a tu criterio" of 2026-11-16.

- **The log is a ring of 16 KB in memory** (external RAM on the board),
  lost at a restart. Rejected: a file on C: (flash wear); a list of lines.
- **It takes what goes to the debug link as log text**, at that one point.
- **A task that finds the log busy drops its text** rather than wait.
- `dmesg` shows it oldest first, without colour codes. No `-c`, no follow.

### Chosen for updates over Wi-Fi (2026-11-17, to confirm)

Backlog item 25. Journal: `docs/journal/2026-11-17-updates.md`. Chosen by
the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **The app area is now two slots of 1.94 MB** (`ota_0`, `ota_1`) and
  `otadata`, as "Flash layout" foresaw. C: has not moved. The first flash
  with this table is by cable.
- **The PC sends the update**: `tools/ota.py <address>`, port 3232, one line
  of text, the image, one line back. Rejected: the board fetching it; an
  upload page; HTTP.
- **Checked twice** (CRC-32 of what arrived, then ESP-IDF's image check)
  before the next start changes.
- **A new version is on trial for ten seconds**; if it does not last them,
  the bootloader starts the one before at the next reset. Rejected:
  confirming at once; confirming by hand.
- **Off unless `[update] enabled = true` in `C:\.config\network.ini`.**
  No password, by the owner's rule. Rejected: always on.

### Chosen for ending services (2026-11-17, to confirm)

Backlog item 5b. Journal: `docs/journal/2026-11-17-stopping-services.md`.
Chosen by the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **A process without windows can be signalled only if its row has a stop
  function**, which really ends what the row stands for and then removes
  the row. The window manager calls it at the safe point. Rejected:
  deleting the task from outside.
- **`inetd` has one.** Ending it lets the viewer and the telnet session go,
  returns their memory, closes the ports and removes `inetd` and `telnetd`.
  It does not come back until the next start. TERM and KILL are the same.
- **The services loop's sleep is at most one second on the board**, so it
  hears the stop; an idle board's services task wakes once a second where
  it did not wake at all (item 8b). Rejected: a socket to itself.

### The OS calls (the owner's decision, 2026-11-17)

Backlog item 20. The owner was asked with the first option recommended and
decided: the thin layer in C++ for now, and a backlog item (37) to look at
it again at the end and take it to numbered calls with handles. The
smaller points below are the agent's and still to confirm.
[Journal](journal/2026-11-17-os-calls.md), [the list](os-calls.md).

- Programs ask the system through `core/os/os.hpp` (namespace `os`): a thin
  layer in C++ over the window manager, the drives, the process table and
  the ports. Rejected for now: numbered calls with handles (every program
  and widget rewritten first, and its shape depends on how programs are
  delivered, which is open). The thin layer does not rule it out.
- A program that has moved is listed in `tools/check_os_calls.py`, a PC
  test that fails if it names `manager()`, `storage()`, `system_port()` or
  `proc::table()` again. New programs start on the list.
- A missing call is added to `os.hpp` and to `docs/os-calls.md`; a program
  does not go around.
- Files are still paths and `fs::File`. Open files owned by the process and
  closed at its end are the next step, not part of this one.
- The dialogs any program may open stay in `shell.hpp`: they are programs.
- init: what is needed (the desktop) starts before anything that is not,
  wherever its line is; a failed line of the start-up file opens a box
  after the start (`shell::startup_trouble`).

### Chosen for the taskbar as a process (2026-11-17, to confirm)

Backlog item 21, first part. The owner was asked (a card in the thread,
the first option recommended); built on it meanwhile.
[Journal](journal/2026-11-17-taskbar-process.md).

- `desktop` (protected) is the window server: windows, pointer,
  background, screensaver, network, Ctrl+Alt+Del. `taskbar` is its child,
  a system process that is not protected and has the respawn flag: the
  taskbar with the Start menu and the quick-launch icons, and the desktop
  icons. Named `taskbar`, not `shell`.
- Ended (TERM or KILL alike), it takes those away and the table starts it
  again. Windows stay where they are and the work area does not change.
  Programs it started become init's children.
- The window manager paints once between the ending and the restart. No
  pause is added; whether to add one is the owner's to say.
- The brake of item 18 applies. After it, `run taskbar` from a console
  starts it again.
- The desktop starts it; there is no line for it in the start-up file.
- The drawing code is still in the window manager. Cutting that file in
  two is the second part.

### The window server and the desktop shell (the owner's decision, 2026-11-17)

Backlog item 21, second part. The owner was shown three depths (the
process only; the code cut in two on one task; two tasks that talk by
messages) and chose the second.
[Journal](journal/2026-11-17-window-server-and-shell.md).

- The window server (`core/wm`) knows windows, menus, the pointer and the
  background. The taskbar and the desktop icons are the shell's
  (`core/desk`), drawn by its code and owned by the `taskbar` process.
- The order agreed with the owner: this cut, then the OS calls taken to
  handles (item 37), then separate tasks together with item 26.
- The shell's surfaces are panels (two places: behind the windows, and the
  strip in front of them), not windows of a special kind. Proposed by the
  agent, confirmed by the owner on 2026-11-17 ("yes, panels are ok").
- The agent's, to confirm: menus, the time of day and the height of the
  strip stay with the server; the desktop panel paints the background
  where it has no icon.
- Painting is not an OS call: the server calls a window or a panel to
  paint and hands it a canvas. It becomes one with item 37.

### The settings tools ask through the OS calls (2026-11-17, the agent's, to confirm)

Backlog item 20, second part. Display, Screen Saver, Network and Shut Down
were each half a window and half something the system keeps going. The
window is a program and asks through new calls (`os::set_backdrop`,
`os::set_screen_saver`, `os::preview_screen_saver`, `os::last_saver_run`,
`os::shut_down`, `os::network`); the other half is the system's (the
screensaver's runner in `core/os`, `network_service.cpp`, `power.cpp`).

- One call for each thing a tool sets, not one general "set a desktop
  setting by name" call: names would be an interface nothing checks at
  build time, and the road to a registry, which the settings decision
  turned down.
- The network is handed over whole (`os::network()`), as the drives are to
  the picture decoder, not as a call for each thing the panel does. Those
  calls belong to item 37 (numbered calls with handles).
- Display and Screen Saver keep nothing in memory while closed: they read
  their settings file when they open.

Journal: `docs/journal/2026-11-17-os-calls-settings-tools.md`.

### The PC tool reads the port on a thread (2026-11-17, the agent's, to confirm)

Backlog item 14b: console text was lost while `tools/dbg.py` was busy. The
tool now reads the port on a thread of its own, into a queue. Pacing on
the board was not chosen first (it slows every listing to cure what looks
like the PC tool's fault) and is the next step if the board shows the
loss again. Flow control on the wire is not possible: the bridge's RTS and
DTR lines reset the board. Journal:
`docs/journal/2026-11-17-dbg-reader-thread.md`.

### init starts again what ends (the owner's request, 2026-11-17)

Backlog item 39. The owner asked for the restart rule to move from the
process table into `init`, as `respawn` in a Unix `/etc/inittab`.
[Journal](journal/2026-11-17-init-respawn.md).

- The process table starts nothing again. It tells a supervisor
  (`proc::Supervisor`, which is init) of every ending. The brake (ended
  within 2 s, 5 times in a row: not started again) is init's.
- `startup.txt` says what comes back, in inittab's fields (the owner's
  choice, over a new verb `respawn NAME` and over a verb that also takes
  programs): `WHAT:ACTION`, the action `once`, `respawn` or `off`, after
  the last colon so a path keeps its own. `desktop:once`,
  `taskbar:respawn`, `inetd:once`, `console:off`. A WHAT that is not a
  part of the system is a program or a file. The lines of the first form
  (`start NAME`, `run WHAT`) are not read: the agent had kept them
  understood so the board's file went on working, and the owner had that
  taken out the same day (see "No backward compatibility").
- The agent's, to confirm: two fields and not inittab's four (there are
  no runlevels, and an id beside the name would say the same thing
  twice); `off` is new with the fields; the desktop cannot be `off`; a
  program cannot be `respawn` yet.
- The agent's, to confirm: a thing that comes back is watched by its
  process, which has the thing's name; the taskbar is needed and comes
  back unless the file says `taskbar:once` (or `taskbar:off`), so a file
  from before still works; the taskbar is init's child; `respawn` on the
  network services or the console starts them once and says so in the log.
- Not done: init on a task of its own that waits for endings. That is
  item 26.

### No backward compatibility (the owner's rule, 2026-11-17)

"We are not making releases for users; backward compatibility is not
needed; the data on the board can be wiped whole every time, C: too." So a
format that changes is read in its new form only, and the board's copy is
rewritten. The saved Wi-Fi networks are to be kept where that is free;
otherwise the owner gives the credentials again. First applied to
`startup.txt` (the verbs `start` and `run` are gone from init's parser).
Still in the code from before the rule, to be taken out when next touched
or if the owner says so: moving an old `C:\docs` into `C:\Documents` at
start-up (`desktop.ini`, `[documents]`).

### The Terminal is an emulator; the session is a process of its own (2026-11-17, the owner's request)

Backlog item 44. Journal: `docs/journal/2026-11-17-terminal-split.md`. This
replaces, in "Chosen for the Terminal window", the session being a job of
the Terminal's process.

- **Two processes** (the owner): `Terminal` is a terminal emulator, a
  window with a grid, the keys and a scroll bar, and nothing else. The TTY
  and the command shell are a session, `sh::TerminalSession` in `core/sh`,
  running as a process `sh` that is the child of the one that asked for it.
  The shell's task, its stack and what its commands use are `sh`'s.
- **Closing the window ends the session, as xterm does** (the owner's
  choice on a card; a session that lives on to be reattached was the other
  option). The TTY is hung up; `sh` goes when its task has returned. The
  window does not wait: it is destroyed when it closes, like every other.
- The other way round, `exit`, Ctrl+D, or `kill` and End Task on `sh` close
  the window (the agent's, to confirm).
- A program asks for a session with `os::open_session(device, why)` and is
  its device: a `ports::ByteSink` for what is shown, `typed()` for the keys.
  The system makes the process and takes it away (`sessions.cpp`). With
  this `terminal.cpp` uses only the OS calls: all 17 programs have moved.
- Not chosen: one `tty` server process holding every session (one row
  would carry three shells' figures, and `kill` could not end one of
  them); splitting the classes but keeping one process (nothing would be
  independent of the window).
- Left as they are for now: the serial console and telnet make their own
  TTY and shell; the shell's commands reach the system through `sh::Env`.

### Open files are handles the system keeps (2026-11-17, the agent's, to confirm)

Backlog item 37, first step. Journal:
`docs/journal/2026-11-17-file-handles.md`. Started on the recommended
option of a card the owner has not answered yet.

- `os::file::open` and `begin_save` give a number, not an `fs::File` in
  the program. The system keeps the table of which process holds which
  file, and closes what a process leaves when it ends; a save not finished
  is thrown away and the old file stays.
- From a background job the call is given the job, which says whose the
  file is. Not chosen: a per-task variable (hidden state), or opening on
  the UI task (flash work where the job was meant to keep it away).
- No lock: a slot is claimed by an atomic flag. The table (2 KB) comes
  from the Memory port once, so it is external RAM on the board.
- ponytail: six open in the machine, two for one process.
- The picture decoder reads from an `image::Source`; the Picture viewer
  hands in one over a handle it opened, held only while it decodes.
  `os::file::storage()`, which handed a program the drives whole, is gone.
- **Handles now, the queue with item 26** (the owner's choice on a card,
  2026-11-17).
- Another program's window is a number (`os::WindowId`, the number the
  window server already gave each window): `os::main_window(pid)` no
  longer hands out a pointer into another program's object. A program's
  own windows stay C++ objects. The agent's, on the recommended option of
  a second card (how far window handles go) that the owner has not
  answered yet.
- **Windows and widgets wholly by number** (the owner's choice on the
  second card, 2026-11-17). `core/os/ui.hpp`: a program asks for a window
  and controls and gets numbers; the objects are the system's; events
  reach it through one function. First cut built with Shut Down as the
  sample. [Journal](journal/2026-11-17-ui-handles.md).
- The list of calls is what a "DLL" of the window system would export (the
  owner's idea): versions are one table that only grows, with a number.
  Loading it is item 27.
- **Thin C++ classes on top of the calls** (the owner's choice on a card,
  2026-11-17; the calls used directly, Win16 style, was the other
  option): `core/app/app.hpp`, one header compiled into each program.
  `app::Button` is a number and the functions that call the system with
  it. Programs are written with these, never with `core/wm`'s classes.
- **Drawing by calls on a number** (the owner's choice on a card,
  2026-11-17; keeping the system's canvas object for now was the other
  option). `os::ui::draw`: the canvas's own calls, one for one, on a
  number good only while the Paint event is answered. Programs write
  `app::Canvas`, a thin class of the same shape, so a paint reads as
  before. Bitmaps and texts are still handed over as pointers into the
  program's memory, as Win16 did.
- What the programs needed that was not a plain control became controls
  of the system's (the agent's, to confirm): the grid of characters and
  the picture a program paints (Terminal, games, viewer), the text
  editor with its own text and undo memory (Editor), and the view of
  icons with names (File manager). A program's toolbar buttons and
  panels are pictures it paints itself. The one thing a program holds
  that is not a number is the Editor's reader for its text while a save
  job runs (good only while the window is locked).
- All 17 programs are written with the thin classes. What is left on
  `core/wm`'s classes is the desktop itself and its own windows (About,
  Welcome, the widget gallery, the message box).
- Events through a queue: with item 26.

### Chosen for background jobs (2026-11-17, to confirm)

Backlog item 10. Journal: `docs/journal/2026-11-17-jobs.md`. Chosen by the
agent under the owner's "dejo a tu criterio" of 2026-11-16, within what "OS
architecture" above already says about saves and signals.

- **A job is a task of its own for as long as its work takes**
  (`core/job/`, `ports::Tasks`): made when the job starts, gone when the
  work returns. No worker task waits for work and no stack is held between
  jobs. Eight jobs at most in the machine. Rejected: one worker with a queue
  (holds its stack for ever; a long job blocks the saves behind it), a pool.
- The work runs on the job's task and may read the program's memory; it may
  not touch windows, timers or the process table. Its result comes back
  through the UI message queue and `done` runs on the UI task as the owner.
- **A process cannot end while it has a job**: the count is on its row
  (`proc::Process::jobs`). A KILL stays waiting on the row, the job is told
  to stop, and the KILL takes effect once the job has returned. A window
  program that has closed its last window stays in the table, and its
  closed windows are not destroyed, until then. Rejected: deleting the
  job's task on KILL (it may hold the file system's lock).
- **A locked window** (`Window::set_locked`) is enforced by the window
  manager, not by each program: no keys, text, clicks in the client area or
  menu bar, dropped files or requests to close (close button, Alt+F4, TERM,
  Shut Down, which is called off). It can still be moved, sized and
  minimised. The pointer is the hourglass over it and the arrow elsewhere.
  Nothing else shows the lock. Rejected: a "saving" flag checked by the
  program in each action; a modal "Saving..." dialog.
- **The Editor saves through a job** with its window locked. The save does
  not stop early on a KILL: the file is finished first.
- On a machine without tasks (the browser, the PC desktop program, most
  tests) there is no `ports::Tasks` and the caller does the work inside
  `start()`, under the whole-desktop hourglass, as before.
- On the board a job's task has priority 4, one below the UI task: the
  Normal class of item 11 ("Chosen for the priority classes").

### Programs on tasks of their own (2026-11-17)

Backlog item 26, first step. [Journal](journal/2026-11-17-program-tasks.md).

- **The server waits for the program** (the owner's choice on a card,
  2026-11-17): each call into a program is handed to its task and the UI
  task waits for it, with a limit; a program that does not come back is
  painted grey and marked as not responding. Rejected: the program
  painting under a lock on the screen (a hung program holding it freezes
  the desktop), a picture kept per window (RAM, and windows keep no
  picture), not doing the item.
- A game that takes the whole screen may later draw straight into the
  frame buffer whatever was chosen here (the owner's question, with Doom
  in mind): the server stands aside, as DirectDraw's exclusive mode. Not
  built; not in the backlog yet.

The rest is the agent's, to confirm:

- A program may have a task of its own (`core/prog`). Every call into its
  code goes through `proc::Table::call` and is handed to that task while
  the caller waits: one of the two runs at a time, so windows, timers and
  the process table still have no locks.
- A call back into a program that is itself waiting is made where the
  caller is. Rejected: letting the two wait for each other.
- Which programs: a mark in the program table, one program at a time.
  Culebra first.
- **4 KB of stack** from internal memory, counted as the program's, and
  **the cost is accepted** (the owner's choice on the board thread's
  card, 2026-11-17: "Merge with a 4 KB stack"; merging as it was with
  8 KB, and not yet, were the others). Measured on the board: about 7 %
  more processor time while playing, and at most about 2.8 KB of stack
  used.
- The limit is two seconds. A program that has not answered by then is
  late: calls for it are dropped until it has finished, what it paints
  is grey, the Task Manager says "Not responding", and closing its
  window ends it without asking.
- No lock around the OS calls: each begins with `enter()` and ends with
  `leave()` (a count). A late program sleeps in `enter()` and is let in
  for 10 ms a round when the UI task has nothing else under way.
  Rejected: a mutex held by the UI task whenever it runs.
- What goes to a program goes by copy, never as a pointer into the
  caller's stack.
- A late program's task runs at the idle priority until it answers: it
  may spin for ever, and the kernel's idle tasks must still get their
  turn (ESP-IDF's task watchdog watches them, 5 s).
- A late program's task is taken away when it is ended; it is outside
  every OS call by then, so it holds no lock.
- Not yet: the queue of events (what a late program missed is lost),
  the other programs, programs that call each other's code directly.

### Missed video refills are logged (2026-11-17, the owner's request)

- `dbg.py tasks` stays as it is (the owner, 2026-11-17: "we can leave the
  thing be as is"): it holds the kernel's lock for about 0.8 ms per call and
  can make the video miss refills. Only the debug link asks for it. Rejected
  for now: raising the video interrupt's level (needs ESP-IDF changed or a
  handler of our own at a high level) and a firmware list of task handles to
  make the call cheap.
- **Every missed refill reaches the system log**, counted by the interrupt
  and written once a second by the scanlog task (`sys::MissedRefills`): one
  line at the first miss of a burst, then at most one every 10 s with the
  sum. A storm of a miss every second writes about 5 KB in ten minutes.
  Misses during the debug link's task list are named in the line. Late
  refills that finished in time are not logged.
- **Log read** (debug request 0x16) gives the log to `tools/dbg.py dmesg`
  and `tools/dbg.py misses`, which exits 1 when a miss was not the debug
  link's. Agents run it after board checks (CLAUDE.md).


### Chosen in the second review round (2026-11-17, the agent's, to confirm)

Backlog item 41, asked by the owner. Journal:
`docs/journal/2026-11-17-review-round-2.md`.

- The Start button is 60 wide: two clear pixels on each side (the owner
  suggested one; one leaves the sides unequal).
- Display's list is 15 colours the board shows exactly, plus "Chinchilla
  blue (dithered)". The default was left dithered here and made solid the
  same day (see "The default desktop colour is solid"). "Lilac" is gone
  from the list.
- "Other..." opens a grid of the board's 256 colours, not a 24-bit picker
  and not sliders: everything it offers is undithered. The grid is one
  sheet of 16 by 16: a square of 8 by 8 for each level of blue, red
  running back in the right-hand squares and green running back in the
  lower ones, blue going round the four: dark at the top, the light
  colours across the middle, the blues at the bottom. The owner's choice
  (2026-11-17, "I think I like 3 more") after a hue-sorted grid, a strip
  of greys, a 32 by 8 sheet and two other folds were shown to him and set
  aside: `docs/journal/2026-11-17-colour-chooser.md`.
- A colour without a name is kept in `desktop.ini` as `#RRGGBB`.
- The chooser is `colour_dialog()`, for any program, as the file dialogs
  are (the owner's request): in the asking program's process, modal to its
  window, nothing kept between two openings.
- The Terminal's grid follows its window (20x4 to 104x36, opening at
  80x25) rather than staying 80x25 or scaling the font. Lines already
  written are not wrapped again. It has a scroll bar over its history.

### Chosen for the icons' places (2026-11-17, the agent's, to confirm)

Backlog item 42, asked by the owner. Journal:
`docs/journal/2026-11-17-icon-places.md`.

- The desktop keeps the places and gives them to the icons panel; they are
  in `C:\.config\icons.ini`, a file of its own written whole, not in
  `desktop.ini`.
- Every icon can be dragged, the system's four too. One that is no file is
  dragged under a name no file has and is offered to no window
  (`Wm::begin_drag(path, desktop_only)`).
- Snap is off by default and, turned on, moves nothing (the owner's rule).
- Realign keeps the order on the screen (by column, then down), not the
  first order.
- Icons may lie on one another when placed freely.
### Submenus hand over at once (2026-11-17)

Asked by the owner: the 300 ms rest before another item of a parent menu
took over from an open submenu made the Start menu feel sluggish. Now, in
every cascading menu and at every depth:

- **Another item of a parent menu takes over at once**: the open submenu
  (and any below it) closes, the item is highlighted and its own submenu, if
  it has one, opens in the same move. No wait.
- **The grace stays**: the parent's borders and separators, and the space off
  the menus, change nothing, so an inaccurate pointer can come back.
- **One exception, chosen by the owner (2026-11-17, over "always at once")**: a pointer on its way *to* the
  open submenu still crosses other parent items on a slant (his report of
  2026-11-16). While it heads for the submenu (its position four moves ago,
  the new one, and the two near corners of the submenu taken 8 px wide make a
  triangle the new position is in), the item it crosses takes over only if
  the pointer rests on it for 300 ms, as before. Coming back from the
  submenu, or moving up and down the parent, is never "on its way".
- A submenu still opens after a 0.3 s rest on its entry when no other
  submenu of that menu is open (unchanged).

test_submenu_switches_at_once and test_slow_submenu in `host/tests/test_wm.cpp`.

### The default desktop colour is solid (the owner's decision, 2026-11-17)

Backlog item 43. "yes, solid steel blue": the default is Steel blue (73,
109, 182, a colour the board has), first in Display's list. The dithered
"Chinchilla blue" stays in the list, second, and a `desktop.ini` that
names it keeps it. This closes the question left open in the second
review round. Journal: `docs/journal/2026-11-17-solid-default.md`.

### Chosen for the BIOS's text screen (2026-11-17, to confirm)

Step 1 of backlog item 46, built on the PC only
(`docs/journal/2026-11-17-bios-text-screen.md`).

- One grid of 80 by 30 cells with a cursor: what is printed goes to the
  screen and, as plain text, to serial, so the two cannot differ.
- A line that changes in place (the memory count, the countdown) is sent to
  serial as carriage return, "erase line" and the new text. No colour on
  serial.
- "Detecting keyboard" instead of "Detecting mouse"; the memory figures
  bright; a bottom line with an identity string given by whoever starts the
  BIOS.
- Not shown until they are true: a "Boot device" line and a boot menu key.
- The reference picture `docs/look/bios-text/bios-post.png` is drawn by
  `tools/mockup/bios_text.py` from the font's BDF file; the PC test compares
  the screen with it pixel for pixel.

### Window themes (2026-11-17, open)

The owner asked for other looks of the era beside the current one, to be
chosen in Display, changing only what is painted (frames, title bars,
buttons, scroll bars, perhaps icons), never how the desktop works. Two
alternatives were drawn in `docs/look/themes/` (B, one bit; C, workbench),
and three more modern ones at his request (D, sky; E, glacier; F, midnight).
Open, for the owner: which direction, whether a theme may move the title
buttons or change sizes, and whether icons belong to a theme. Nothing is
built. Journal: [first round](journal/2026-11-17-window-themes.md),
[second round](journal/2026-11-17-window-themes-modern.md).
