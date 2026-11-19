# Decisions

What this project will and will not do, so that anyone picking it up (person
or agent) does not have to ask again. This file is the index: read it whole,
then open only the decisions that touch your work. Each decision is a file of
its own in [`docs/decisions/`](decisions/). Nothing is deleted there; a
decision that no longer holds is marked as replaced.

Until 2026-11-18 everything was in this one file. Other documents and code
comments name a decision by its title, as in `docs/DECISIONS.md`, "Found on
the bench": find that title in the list below. The old "Decided" table is
now the four entries that begin "Decided:".

## Always applies

Short enough to keep here, so that nobody has to open a file for them. The
rules for how to work (branches, tests, journal, language) are in
[`CLAUDE.md`](../CLAUDE.md) and are not repeated.

- **The goal** is a 90s-style desktop OS, Windows 98 in look, called
  Chinchilla, for one ESP32-S3-N16R8 board (16 MB flash, 8 MB octal PSRAM)
  with a hand-built VGA adapter. It is a recreational project: no releases,
  no users, no security goals, no logins or passwords
  ([architecture](decisions/decided-architecture.md),
  [rejected](decisions/rejected.md)).
- **The owner decides.** Design choices are put to him as options with a
  recommendation. What an agent chose without an answer is recorded as
  `to-confirm` and stays his to overrule.
- **Nothing is bought, nothing is copied.** Hardware is built from parts on
  hand. Art, fonts, sounds, names and sample files are original or freely
  licensed, with the licence recorded; no Microsoft assets and no copy of
  another product's name or look
  ([hardware](decisions/decided-hardware.md), [look](decisions/decided-look.md)).
- **The picture** is 640x480 at 60 Hz in 256 colours (3 bits red, 3 green,
  2 blue), one byte per pixel, one picture in PSRAM with no double
  buffering. The pointer is mixed in as each line goes out. Colours the
  board cannot show are dithered ([video](decisions/decided-video.md)).
  Nothing may hold up the video refill
  ([missed refills](decisions/missed-video-refills.md)).
- **The code** is C++ on ESP-IDF v5.5 with FreeRTOS underneath, as ports and
  adapters: `core/` is portable, builds and is tested on a PC, and never
  includes an ESP-IDF header; the hardware side is adapters under
  `firmware/os/main/` ([architecture](decisions/decided-architecture.md)).
- **The model is AmigaOS's**: real tasks with priorities, one shared memory
  space, no protection between programs
  ([OS architecture](decisions/os-architecture.md)).
- **Programs** redraw their windows when asked and keep no stored picture.
  A window exists only while it is open
  ([lifecycle](decisions/program-lifecycle.md)); a dialog or settings tool
  is a program of its own ([dialogs](decisions/dialogs-are-programs.md));
  programs reach the system only through the OS calls
  ([OS calls](decisions/os-calls.md)); what is drawn takes its colours from
  the theme ([themes](decisions/window-themes.md)).
- **Settings** are one INI file per program in `C:\.config`; there is no
  registry ([settings](decisions/program-settings.md)).
- **No backward compatibility**: a changed format is read in its new form
  only. The saved Wi-Fi networks are the one thing to leave alone
  ([rule](decisions/no-backward-compatibility.md)).

## Reading a line of the list

`- Title (date, who, status): summary`

- **who** is `owner` (he decided or asked for it), `agent` (chosen while
  building), or `owner+agent` (his decision, with details the agent chose;
  the text says which is which).
- **status** is `decided`; `to-confirm` (an agent's choice the owner has not
  confirmed: follow it, and it may be overruled); `open` (a question);
  `replaced` (kept as the record; the summary names what replaced it);
  `idea` (not scheduled); `reference` (findings, not a choice).

## Recording a decision

1. A new decision is a new file, `docs/decisions/<a-few-words>.md`, in the
   same change as the work. It starts with this block, then the text:

   ```
   ---
   title: "The mouse wheel"
   date: 2026-11-18
   by: owner+agent
   status: decided
   area: desktop
   summary: "The wheel scrolls what is under the pointer (backlog 61)"
   ---
   ```

   `summary` is one line of at most 160 characters that lets a reader judge
   whether to open the file; name the backlog item. The areas are the
   headings of the list below. The text says what was chosen, by whom, and
   what was not chosen and why, in a few bullets; the long story goes in the
   journal entry, which the text names.
2. A change to an existing decision is made in its file. When the owner
   confirms a `to-confirm`, change the status. When a decision is reversed,
   set the old file's status to `replaced`, say in its summary what replaced
   it, and leave its text as the record.
3. Run `tools/decisions_index.py`. It rewrites the list below from the
   files; never edit the list by hand. The PC tests fail if it is out of
   date. If two branches both changed the list and it conflicts in a merge,
   take either side and run the tool again.
4. Something that always applies and that every thread must know belongs in
   "Always applies" above, in one or two lines, with a link to its file.
   Keep that section short.

## The decisions

<!-- decisions:begin (written by tools/decisions_index.py; do not edit by hand) -->

### project

Goal, ways of working, what was rejected, what is open.

- [Decided: architecture](decisions/decided-architecture.md) (2026-11-16, owner, decided): The goal, C++ on ESP-IDF and FreeRTOS, ports and adapters, the PC build, repository layout, testing, the debug link and tool
- [Ideas kept for later](decisions/ideas-kept-for-later.md) (2026-11-16, owner, idea): Not scheduled: dial-up and a software modem over serial, network over USB, sound, Rust, Bluetooth, a virus
- [Parallel work](decisions/parallel-work.md) (2026-11-16, owner, decided): Threads work on branches with pull requests; the laptop-and-board thread merges after a board check
- [Planned order of work](decisions/planned-order-of-work.md) (2026-11-16, owner, replaced): The first nine-step plan. `docs/BACKLOG.md` is the list of work now
- [Rejected](decisions/rejected.md) (2026-11-16, owner, decided): The table of options turned down and why: colour depths, software USB, Ethernet, own kernel, LVGL, sample files, registry, logins
- [No backward compatibility](decisions/no-backward-compatibility.md) (2026-11-17, owner, decided): No releases, no users: a changed format is read in its new form only; keep the Wi-Fi networks where free
- [One file per decision](decisions/decisions-in-files.md) (2026-11-18, owner, decided): `docs/DECISIONS.md` is an index and each decision is a file in `docs/decisions/`; the list is written by a tool
- [Still open](decisions/still-open.md) (2026-11-18, owner, open): The table of open questions with the options and the suggestion for each; some rows are answered in place

### hardware

The board, wiring, pins, what the bench showed.

- [To confirm on the bench](decisions/to-confirm-on-the-bench.md) (2026-11-16, agent, open): Checks still owed on the real board: picture stability, Wi-Fi, remote screen, finding by name
- [Decided: hardware](decisions/decided-hardware.md) (2026-11-18, owner, decided): The board, the adapter boards and their pin map, the supply capacitor, USB, the clock chip, nothing is bought
- [Found on the bench](decisions/found-on-the-bench.md) (2026-11-18, agent, reference): What the real board showed: pixel clock, bounce buffers, PSRAM and flash effects on the picture, Wi-Fi interference, timings

### video

The picture: signal, memory, painting, frame time.

- [Chosen for painting each pixel once](decisions/painting-each-pixel-once.md) (2026-11-16, agent, to-confirm): Layered painting through a scratch band of 12 lines, so nothing flickers without double buffering
- [Decided: video](decisions/decided-video.md) (2026-11-16, owner, decided): 640x480 at 60 Hz, 256 colours, one picture in memory, the pointer mixed in at scanout, dithered colours
- [Missed video refills are logged](decisions/missed-video-refills.md) (2026-11-17, owner, decided): Missed refills are logged; `dbg.py tasks` stays although it can cause them
- [Chosen for the frame budget](decisions/frame-budget.md) (2026-11-18, agent, to-confirm): The budget is one video frame, 16 800 us; how drawing is measured against it (backlog 63)
- [Screen sizes: 640x480 and 800x600](decisions/screen-sizes.md) (2026-11-18, owner, decided): Display > Resolution changes the picture at once; a box asks to keep it, back after 15 s; 640x480 the default (backlog 68)
- [Chosen for the refills missed at 800x600](decisions/scanout-ahead.md) (2026-11-19, agent, to-confirm): 800x600 copies the frame's third piece to internal RAM in the vertical blank (8,000 bytes); 640x480 is left as it was (backlog 68)

### boot

Flash layout, the BIOS stage, start-up screens.

- [Flash layout](decisions/flash-layout.md) (2026-11-16, owner, decided): The first partition table: one app area and C: above it. Changed since by the update slots and the BIOS partition
- [Start-up timing](decisions/start-up-timing.md) (2026-11-16, owner, decided): How long the self-test and the splash stay on screen (`monitor_wait_ms`, `splash_ms`), and what the self-test shows
- [A BIOS stage in its own partition](decisions/bios-stage.md) (2026-11-17, owner, decided): The self-test is a BIOS, a small firmware in its own partition that restarts into the OS
- [BIOS screen font](decisions/bios-screen-font.md) (2026-11-17, owner, decided): The BIOS screen is true text, 80 by 30 characters in Spleen 8x16
- [BIOS stage: flash and details](decisions/bios-flash-and-details.md) (2026-11-17, owner, decided): The BIOS takes 1 MB from C:; two update slots, C: after them. The sizes were changed by "App slots of 3 MB"
- [Chosen for the BIOS firmware](decisions/bios-firmware.md) (2026-11-17, agent, to-confirm): The `bios` app partition and how our bootloader starts it (backlog 46, step 2)
- [Chosen for the BIOS's text screen](decisions/bios-text-screen.md) (2026-11-17, agent, to-confirm): One grid of 80 by 30 cells mirrored to serial as plain text (backlog 46, step 1)
- [The BIOS's first screen](decisions/bios-first-screen.md) (2026-11-17, owner, decided): The BIOS shows a first screen until 7 s after the reset, because the monitor takes that long
- [App slots of 3 MB](decisions/app-slots-3-mb.md) (2026-11-19, owner, decided): Two update slots of 3 MB, `otadata` at 0xf0000, the BIOS partition 896 KB, C: 9 MB from 0x700000 (backlog 71)
- [The desktop fades in at start-up](decisions/start-up-fade.md) (2026-11-19, owner+agent, to-confirm): After the splash the screen shows the desktop's plain colour, rests 300 ms, and the desktop comes up in seven steps; not on a fast start (backlog 74)

### system

Processes, tasks, signals, init, OS calls, memory.

- [Chosen for signals and flags](decisions/signals-and-flags.md) (2026-11-16, agent, to-confirm): TERM and KILL, acted on at the window manager's safe point; protected processes; End Task (backlog 4, 5)
- [Chosen for the message queue](decisions/message-queue.md) (2026-11-16, agent, to-confirm): Other tasks never touch windows: they post a message that the UI task runs (backlog 8)
- [Chosen for the process table](decisions/process-table.md) (2026-11-16, agent, to-confirm): `core/proc`: 32 rows, PIDs, parents, states, who owns which window (backlog 2)
- [Only real tasks are listed as processes](decisions/only-real-tasks.md) (2026-11-16, owner, decided): A row in the process table stands for something that really runs; polled services get none
- [OS architecture](decisions/os-architecture.md) (2026-11-16, owner, decided): The AmigaOS model: real tasks with priorities, one memory space, no protection; processes, signals, messages, shell, TTY, init
- [Chosen for background jobs](decisions/background-jobs.md) (2026-11-17, agent, to-confirm): A job is a task of its own for as long as its work takes, `core/job/` (backlog 10)
- [Chosen for ending services](decisions/ending-services.md) (2026-11-17, agent, to-confirm): A process without windows can be signalled only if its row has a stop function (backlog 5b)
- [Chosen for init](decisions/init.md) (2026-11-17, agent, to-confirm): `init` is PID 1 and the desktop its child; the start-up file (backlog 18)
- [Chosen for per-process figures and top](decisions/process-figures-and-top.md) (2026-11-17, agent, to-confirm): Processor time and memory per process, and `top` (backlog 3, 16)
- [Chosen for the priority classes](decisions/priority-classes.md) (2026-11-17, agent, to-confirm): Low 3, Normal 4, High 5 as FreeRTOS priorities; desktop 5, video 10 (backlog 11)
- [Chosen for the services loop](decisions/services-loop.md) (2026-11-17, owner+agent, to-confirm): One task, `inetd`, runs every network service off the UI task (backlog 8b)
- [Dialogs and settings tools are programs](decisions/dialogs-are-programs.md) (2026-11-17, owner, decided): A dialog or settings tool is a program of its own: own process row, own file, no memory while closed
- [init starts again what ends](decisions/init-respawn.md) (2026-11-17, owner, decided): init, not the process table, starts again what ends, as `respawn` in an inittab (backlog 39)
- [No queue of events](decisions/no-event-queue.md) (2026-11-17, owner, decided): What happens to a program while it is not responding is lost (backlog 26, 37)
- [No window limit of the system's](decisions/no-window-limit.md) (2026-11-17, owner, decided): The system sets no limit on open windows; running out of memory is allowed to show
- [Open files are handles the system keeps](decisions/file-handles.md) (2026-11-17, agent, to-confirm): Open files are numbers; the system keeps the table and closes what a dead process held (backlog 37)
- [Our static objects in external RAM](decisions/static-objects-in-external-ram.md) (2026-11-17, owner, decided): The core's static objects live in external RAM (`extram_bss`); what stays internal
- [Program lifecycle](decisions/program-lifecycle.md) (2026-11-17, agent, to-confirm): Windows are made when they open and destroyed when they close; `make_window`, `one`, `open_one` (backlog 19, 34)
- [Programs loaded from files](decisions/programs-loaded-from-files.md) (2026-11-17, owner, decided): Native code loaded from C:, with no protection; the file format and the loader (backlog 27)
- [Programs on tasks of their own](decisions/programs-on-own-tasks.md) (2026-11-17, owner+agent, decided): Each program runs on its own task and the server waits for it, with a limit; "not responding" (backlog 26)
- [The OS calls](decisions/os-calls.md) (2026-11-17, owner+agent, decided): Programs ask the system through a thin C++ layer, `core/os/os.hpp`; numbered calls come later (backlog 20, 37)
- [The settings tools ask through the OS calls](decisions/settings-tools-os-calls.md) (2026-11-17, agent, to-confirm): Display, Screen Saver, Network and Shut Down are programs that ask through new OS calls (backlog 20)
- [Watchdog, crash record and error screen](decisions/watchdog-crash-record-error-screen.md) (2026-11-17, agent, to-confirm): Heartbeat watchdog, a crash record kept over the restart, and the error screen (backlog 7)
- [Services are lines of the start-up file](decisions/services-start-up-file.md) (2026-11-18, owner, decided): `telnetd` and `vncd` are lines of init's start-up file; a Services dialog and commands edit them (backlog 62)
- [Sound: the port, the beeper and the system's sounds](decisions/sound-port.md) (2026-11-18, owner+agent, to-confirm): A Sound port with two voices, samples and an emulated PC beeper; silent on the board, real on the PC and in the browser (backlog 58, 59)

### storage

File system, folders, the Trash, settings files, saving.

- [Chosen for safe saves and the hourglass](decisions/safe-saves-and-hourglass.md) (2026-11-16, owner+agent, to-confirm): Saves write a hidden temporary file and swap it in; the sand-timer pointer (the owner's pick)
- [Chosen for the file system lock](decisions/file-system-lock.md) (2026-11-16, agent, to-confirm): One lock for the whole file system, held for one call (backlog 9)
- [Chosen for the Trash](decisions/trash-details.md) (2026-11-16, agent, to-confirm): The Trash is C:\.trash, trash.ini remembers where entries came from, Restore, numbered names
- [File system on the flash](decisions/file-system-on-the-flash.md) (2026-11-16, owner, decided): LittleFS as C:, names ignore case, code runs from PSRAM so that flash writes do not shake the picture
- [One documents folder](decisions/one-documents-folder.md) (2026-11-16, owner, decided): Documents opens C:\Documents, which holds the sample documents and the Desktop folder
- [Program settings](decisions/program-settings.md) (2026-11-16, owner, decided): One INI file per program in C:\.config; no central registry
- [Trash](decisions/trash.md) (2026-11-16, owner, decided): Deleting moves to the Trash; dragging onto its icon; Empty Trash
- [Chosen for file dates](decisions/file-dates.md) (2026-11-17, agent, to-confirm): One date per file, when it was last written, in UTC seconds (backlog 24)
- [The Trash is the File manager on C:\.trash](decisions/trash-is-the-file-manager.md) (2026-11-17, owner, decided): There is no Trash window: a File manager window that shows C:\.trash is the Trash
- [Change notices from the file system](decisions/change-notices.md) (2026-11-18, owner+agent, decided): Storage says which folder changed; the desktop reads its folder only then, in a job, not every 2 s (backlog 66)

### desktop

Window manager, menus, taskbar, themes, icons, pointer.

- [Chosen for the files on the desktop](decisions/desktop-files-details.md) (2026-11-16, agent, to-confirm): Where file icons go on the desktop, their order, the limit of 20, long and hidden names
- [Chosen in step 4](decisions/window-manager-details.md) (2026-11-16, agent, to-confirm): Window manager details: focus, drag outline, maximised windows, the taskbar and its buttons
- [Chosen in the screensaver milestone](decisions/screensaver-milestone.md) (2026-11-16, agent, to-confirm): How the saver takes the screen, what wakes it, the three savers, its dialog and settings file
- [Chosen in the shell milestone](decisions/shell-milestone.md) (2026-11-16, agent, to-confirm): Start menu, menus and how they open, Alt+Tab, double click, desktop icons, keyboard layout, program icons
- [Decided: look](decisions/decided-look.md) (2026-11-16, owner, decided): Windows 98 look, Spleen font, the name Chinchilla and its mascot, system sounds, start-up screens, the screensaver, repainting
- [Files on the desktop](decisions/files-on-the-desktop.md) (2026-11-16, owner, decided): The files in C:\Documents\Desktop show as icons after the system icons
- [Submenus with a slow pointer](decisions/submenus-slow-pointer.md) (2026-11-16, owner, replaced): A submenu stayed open while the pointer crossed its parent; the 300 ms rest was removed by "Submenus hand over at once"
- [Chosen for the icons' places](decisions/icon-places.md) (2026-11-17, agent, to-confirm): Desktop icons can be moved; their places are kept in C:\.config\icons.ini (backlog 42)
- [Chosen for the quick-launch icons](decisions/quick-launch-icons.md) (2026-11-17, agent, to-confirm): Three icons by the Start button: File manager, Editor, Terminal (backlog 30)
- [Chosen for the taskbar as a process](decisions/taskbar-process.md) (2026-11-17, agent, to-confirm): `desktop` is the window server and `taskbar` its child process (backlog 21, first part)
- [Chosen in the second review round](decisions/review-round-2.md) (2026-11-17, agent, to-confirm): Start button width, Display's colour list and colour chooser, Terminal scroll bar and maximising (backlog 41)
- [Submenus hand over at once](decisions/submenus-hand-over.md) (2026-11-17, owner, decided): Another item of a parent menu takes over from an open submenu at once
- [The default desktop colour is solid](decisions/default-desktop-colour.md) (2026-11-17, owner, decided): The default desktop colour is solid Steel blue (backlog 43)
- [The window server and the desktop shell](decisions/window-server-and-desktop-shell.md) (2026-11-17, owner, decided): The code is cut in two on one task: the window server (`core/wm`) and the desktop shell (backlog 21)
- [Window themes](decisions/window-themes.md) (2026-11-17, owner, decided): Themes chosen in Display: Classic, Sky, Glacier, Midnight; a theme changes only what is painted (backlog 47)
- [Slow repaints: faster drawing first](decisions/faster-drawing-first.md) (2026-11-18, owner, decided): Large windows painted slowly (backlog 65): fix the drawing code first, measure, kept window pictures only if still needed
- [The hourglass on demand](decisions/hourglass-on-demand.md) (2026-11-18, owner, decided): A program asks for the hourglass with `os::ui::set_hourglass`; it shows over its client area only (backlog 49)
- [The mouse wheel](decisions/mouse-wheel.md) (2026-11-18, owner+agent, decided): The wheel scrolls what is under the pointer; how far, and which widgets take it (backlog 61)

### programs

The programs and games, one decision each.

- [Chosen for the Task Manager, Shut Down and the Defragmenter](decisions/task-manager-shut-down-defragmenter.md) (2026-11-16, agent, to-confirm): Task Manager on Ctrl+Alt+Del, the Shut Down and power-down screens, the pretend Defragmenter
- [Chosen in the Cactus Patch game](decisions/cactus-patch-game.md) (2026-11-16, agent, to-confirm): Cactus Patch, the mine-sweeping game: name and theme, rules, sizes; the right mouse button reaches windows
- [Chosen in the files milestone](decisions/files-milestone.md) (2026-11-16, agent, to-confirm): Clock format, key and caret timings, modal dialogs, Latin-1 text and file names, File manager and Editor limits
- [Chosen in the image viewer milestone](decisions/image-viewer-milestone.md) (2026-11-16, agent, to-confirm): Picture viewer: own PNG, BMP and JPEG decoders, limits, fitting, the dithering methods, sample pictures
- [Chosen in the snake game](decisions/snake-game.md) (2026-11-16, agent, to-confirm): Culebra: look, rules, keys, speeds, pausing
- [System programs the owner would like](decisions/wanted-system-programs.md) (2026-11-16, owner, idea): The owner's list of system programs to build some day, with what each must and must not be
- [The web browser, Vizcacha](decisions/web-browser-vizcacha.md) (2026-11-17, owner+agent, decided): Vizcacha, a browser for 1990s HTML with no scripts or style sheets; HTTPS through a relay (backlog 53)
- [Chosen for Copy, Cut and Paste of files](decisions/file-copy-cut-paste.md) (2026-11-18, agent, to-confirm): One clipboard for text or files; Copy, Cut and Paste in the File manager and on the desktop (backlog 54)
- [Chosen for selecting several files](decisions/selecting-several-files.md) (2026-11-18, owner+agent, to-confirm): Ctrl+click and a rubber-band rectangle in the File manager (backlog 55)
- [Pictures in Vizcacha](decisions/browser-pictures.md) (2026-11-18, owner+agent, decided): Vizcacha shows a page's PNG, JPEG and BMP pictures, fetched after the text and fitted to the page; GIF stays a box (backlog 53)
- [The chess computer does not answer at once](decisions/chess-reply-pace.md) (2026-11-18, owner+agent, decided): Chess: the computer's move is shown no sooner than 250 to 350 ms after it began to think (backlog 67)
- [The chess game](decisions/chess-game.md) (2026-11-18, owner, decided): Chess: own engine, moving by drag or click, piece sets, board colours, levels (backlog 50)
- [The computer's chess piece lands](decisions/chess-landing.md) (2026-11-18, owner+agent, decided): The computer's piece drops 4 pixels onto its square and bounces once, 233 ms (backlog 64); details to confirm
- [Sounds of the games and the Trash](decisions/game-sounds.md) (2026-11-19, owner+agent, to-confirm): Chess knocks on a move, warns the user of check, says checkmate; Culebra a berry and game over; the Trash crumples paper (backlog 70)
- [The File manager's three views](decisions/file-manager-views.md) (2026-11-19, owner+agent, to-confirm): Icons, List and Details from the View menu, one way for every folder window (backlog 56)
- [The Shut Down sequence](decisions/shut-down-sequence.md) (2026-11-19, owner+agent, decided): Shut Down closes the programs (ended after 20 s), fades the desktop, then to black, and shows the last screen on black (backlog 60)

### console

TTY, command shell, Terminal, serial console.

- [Chosen for pipes and redirection](decisions/pipes-and-redirection.md) (2026-11-17, agent, to-confirm): A pipe is a hidden file and the commands run one after the other, as on MS-DOS (backlog 23)
- [Chosen for Run and the shell's open, run and edit](decisions/run-and-shell-open.md) (2026-11-17, agent, to-confirm): Start > Run... is a program of its own; the shell's `open`, `run` and `edit` (backlog 16)
- [Chosen for the command shell](decisions/command-shell.md) (2026-11-17, agent, to-confirm): `sh::Session` in `core/sh/`: the commands, paths and drives, what each built-in does (backlog 13)
- [Chosen for the console editor](decisions/console-editor.md) (2026-11-17, agent, to-confirm): `ted`, a full-screen editor with no modes, for sessions without a desktop (backlog 22)
- [Chosen for the serial console](decisions/serial-console.md) (2026-11-17, agent, to-confirm): The shell on the serial port shares the wire with the debug protocol: a zero opens a frame (backlog 14)
- [Chosen for the system log and dmesg](decisions/system-log-and-dmesg.md) (2026-11-17, agent, to-confirm): The log is a ring of 16 KB in memory, read with `dmesg` (backlog 16)
- [Chosen for the Terminal window](decisions/terminal-window.md) (2026-11-17, agent, to-confirm): The Terminal program: a window that is the device of a TTY (backlog 15). Its session part was replaced by "The Terminal is an emulator"
- [Chosen for the TTY](decisions/tty.md) (2026-11-17, agent, to-confirm): `tty::Tty` sits between a device and a session: line editing, echo, control keys (backlog 12)
- [The Terminal is an emulator; the session is a process of its own](decisions/terminal-emulator-and-session.md) (2026-11-17, owner, decided): Two processes: `Terminal` is only an emulator, and the shell session is a process of its own (backlog 44)

### network

Wi-Fi, remote screen, telnet, updates.

- [Chosen for finding the board by name](decisions/finding-the-board-by-name.md) (2026-11-16, agent, to-confirm): mDNS: the board answers to `chinchilla.local` and announces VNC and HTTP
- [Chosen in the remote screen milestone](decisions/remote-screen-milestone.md) (2026-11-16, owner+agent, decided): The VNC server: RFB versions, one viewer, changed rectangles, pixel formats, ZRLE, keys, the browser viewer
- [Chosen in the Wi-Fi milestone](decisions/wifi-milestone.md) (2026-11-16, agent, to-confirm): Passwords in NVS, remembered networks, the Network panel, time zone, security modes, power saving off
- [Remote screen access](decisions/remote-screen-access.md) (2026-11-16, owner, decided): VNC (5900) and the web page (80) run whenever Wi-Fi is on, with no password
- [Chosen for telnet](decisions/telnet.md) (2026-11-17, owner+agent, to-confirm): Telnet on port 23, no password, on by default (the owner's decision); the rest is the agent's (backlog 17)
- [Chosen for the viewer's system keys](decisions/viewer-system-keys.md) (2026-11-17, owner, decided): The web viewer has buttons for Alt+Tab, Alt+F4, Ctrl+Alt+Del and the logo key
- [Chosen for updates over Wi-Fi](decisions/updates-over-wifi.md) (2026-11-17, agent, to-confirm): Two app slots and `otadata` (3 MB each since "App slots of 3 MB"); how an update arrives and is confirmed (backlog 25)
- [noVNC from a CDN: dropped](decisions/novnc-dropped.md) (2026-11-17, owner, decided): The `/novnc` page was removed; our own viewer at `/` is enough. Keeps the record of what was built
- [Wi-Fi transmit power](decisions/wifi-transmit-power.md) (2026-11-18, owner, decided): Default 20 dBm, `tx_power_dbm` in network.ini; a board without the supply capacitor sets 2

### tools

The debug tool, the PC and browser builds, deployment.

- [One-shot fast start for debug restarts](decisions/one-shot-fast-start.md) (2026-11-16, owner, decided): `dbg.py reboot --fast` skips the slow start-up once; the flag is a word in NVS
- [Chosen for the browser version](decisions/browser-version.md) (2026-11-17, agent, to-confirm): Chinchilla in a web browser, built with Emscripten from the same source as the PC program (backlog 33)
- [Chosen for the desktop program](decisions/desktop-program.md) (2026-11-17, agent, to-confirm): Chinchilla as an SDL2 program on the PC, `host/desktop/main.cpp` (backlog 32)
- [The PC tool reads the port on a thread](decisions/pc-tool-reads-port-on-thread.md) (2026-11-17, agent, to-confirm): `tools/dbg.py` reads the serial port on a thread of its own so that console text is not lost (backlog 14b)
- [The relay is deployed from GitHub](decisions/relay-deployed-from-github.md) (2026-11-18, owner+agent, decided): The web version's relay goes to Cloudflare from a GitHub workflow

<!-- decisions:end -->
