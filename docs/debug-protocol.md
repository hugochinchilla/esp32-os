# Debug protocol

How a computer talks to the board over the serial port: screenshots, injected
keyboard and mouse events, and status. The board side is in `core/src/dbg/`,
the computer side is `tools/dbg.py`.

Version 1. Working on the board since 2026-11-16.

## Link

- UART0, through the USB-C socket marked UART. 921,600 baud, 8 data bits, no
  parity, 1 stop bit. 115,200 baud is the fallback (see
  `firmware/os/sdkconfig.defaults`).
- Log text and commands share the port. After start-up, everything the board
  sends is a frame, log lines included, so a log line can never land in the
  middle of a screenshot. Text the chip prints before the OS starts (boot
  messages) is not framed; the tool shows it as plain text.

## The serial console on the same wire

With `console:once` in `C:\.config\startup.txt` the command shell is on
this port too (backlog item 14, `docs/journal/2026-11-17-serial-console.md`).
Nothing changes for a tool that only speaks frames.

- To the board: a zero byte opens a frame and the next zero closes it. Bytes
  that arrive outside a frame are typed text and go to the shell at once. A
  frame that fails its check is dropped and the wire stays "inside a frame",
  so the bytes of a frame never reach the shell. If nothing arrives for half
  a second while a frame is open, it is given up and what it held is taken
  as typed (a stray zero must not swallow the keyboard).
- From the board: what the shell shows is sent as plain text between
  frames, never inside one. Between two zeros, what passes the frame check
  is a frame and the rest is text.
- `tools/dbg.py console` is a terminal for it (Ctrl+] leaves); any serial
  terminal program works, and shows the frames as noise between the lines.

## Frames

On the wire, a frame is a zero byte, the encoded frame, and another zero byte:

```
00  <COBS-encoded frame>  00
```

COBS encoding removes every zero from the frame, so a zero only ever appears
between frames. Anything between two zeros that does not decode to a valid
frame is dropped (the board) or shown as text (the tool). Because of that, a
damaged or lost byte costs one frame and the link recovers on the next.

Before encoding, a frame is:

| Bytes | Field |
| --- | --- |
| 1 | Type |
| 1 | Sequence number, chosen by the computer and repeated in the reply |
| 0 to 1024 | Payload (at most 256 towards the board) |
| 2 | CRC-16/CCITT-FALSE of everything before it, low byte first |

All numbers are little-endian. Requests carry at most 1024 bytes of payload,
replies at most 1024.

## Commands

The computer sends a request; the board replies with the same type plus 0x80
and the same sequence number. Every reply payload starts with a status byte:
0 ok, 1 unknown command, 2 bad arguments.

| Type | Name | Request payload | Reply payload after the status byte |
| --- | --- | --- | --- |
| 0x01 | Ping | Any bytes | The same bytes |
| 0x02 | Info | None | Protocol version (1 byte), width (2), height (2), pixel format (1), firmware name (text) |
| 0x03 | Screenshot | None, or flags (1): bit 0 leaves out the mouse pointer, bit 1 says the computer understands encoding 2 | Width (2), height (2), pixel format (1), encoding (1), picture size in bytes (4). Followed by picture frames, see below |
| 0x04 | Key | Key code (2), pressed (1: 1 down, 0 up) | None |
| 0x05 | Mouse | Mode (1: 0 movement, 1 position), x (2, signed), y (2, signed), buttons (1), wheel (1, signed: notches, positive is up), and optionally the wheel sideways (1, signed: positive is right) | None |
| 0x06 | Stats | None | 4-byte numbers: uptime in ms, free internal RAM, free external RAM, frames shown, frames received, receive errors, key events, mouse events, time spent filling video lines in µs (wraps; the first firmware sent only the first eight) |
| 0x07 | Reboot | None, or one byte of flags: bit 0 makes the next start a fast one (no start-up screens), once | None. The board restarts after replying |
| 0x08 | Windows | None | The windows as JSON text, see below |
| 0x09 | Repaint | None | Time taken in µs (4). The board paints the whole screen again |
| 0x0A | Text | Text, UTF-8 | None. The characters are typed into the focused window (Latin-1 letters such as ñ and é work) |
| 0x0B | Time | None, or hours, minutes, seconds (1 each) | Hours, minutes, seconds of the taskbar clock (after setting it) |
| 0x0C | Animations | On (1): 0 or 1 | None. Off keeps the caret shown, so screenshots do not depend on when they are taken |
| 0x0D | File list | Start index (2), folder path | Count (1), more follow (1), then per entry: folder (1), size (4), name length (1), name |
| 0x0E | File read | Offset (4), path | File size (4), then up to 1019 bytes of the file from the offset |
| 0x0F | File write | Offset (4), flags (1: bit 0 creates or empties the file first), path length (1), path, data | None |
| 0x10 | File delete | Path | None. A file, or an empty folder |
| 0x11 | Make folder | Path | None |
| 0x12 | Boot screen | One byte: 0 the self-test again, 1 the splash, 2 nothing shown but the next start is a fast one, once, 3 the error screen after a crash (the crash reported at this start, or a made-up one) | None. Repaint brings the desktop back |
| 0x13 | Memory report | None | Text: heaps and video timing (task stacks are in Tasks, which holds the kernel lock about 0.8 ms per call) |
| 0x14 | Tasks | None | The kernel's tasks, see below. Unknown command when the firmware keeps no run-time statistics |
| 0x15 | Fault | Kind (1): 0 hang (the UI task spins for ever), 1 a write through a null pointer, 2 abort, 3 slow (busy for five seconds, then it carries on; nothing fails). Then optionally a PID (2) to charge it to | None. The board replies, then fails on purpose: the watchdog or the panic handler writes the crash record and restarts it, and the next start shows the error screen. Bad arguments for a PID that is not running |
| 0x16 | Log read | From (8): bytes since start, 0 for the oldest text kept | Where the next piece starts (8), then up to 1000 bytes of the system log (what `dmesg` prints). Text pushed out of the 16 KB ring meanwhile is skipped. `tools/dbg.py dmesg` prints it all |
| 0x17 | Frames | Optional flags (1): bit 0 starts the count again after the reply | What the desktop counted against one video frame (`core/sys/frames.hpp`): budget in µs, pieces of work, pieces that painted, pieces over budget, pieces with a timer a frame or more late, the longest piece, the latest timer (4 each); all the pieces added up in µs (8); eight counts of pieces by length (4 each: under 1, 2, 4, 8 ms, under the budget, under 2 and 4 budgets, longer); how many slow pieces follow (1, at most 16, oldest first), each: when in ms since start-up, total, painting, pixels painted, in timers, the longest single thing, timer lateness (4 each), that thing's PID (2), the work (1: 0 key, 1 mouse, 2 text, 3 tick, 4 message), what the thing was (1: 0 nothing, 1 timer, 2 a window's painting, 3 a panel's). `tools/dbg.py frames` prints it and exits 1 if any piece was over budget |
| 0x18 | Paint bench | None | Text: the time in µs of each kind of drawing over the same 480 x 300 pixels of the screen (the shortest of three), of lines drawn from top to bottom, of copying memory to the screen, and of each open window painted alone: frame, menu bar and inside (`Wm::paint_bench`, `core/src/wm/paint_bench.cpp`). The board draws over its screen for about a second, paints everything again and then replies. `tools/dbg.py paintbench` prints it |
| 0x19 | Video mode | Optional: the number of the mode to change to (1) | How many modes there are (1), the one in use or just asked for (1), then for each mode: width (2), height (2), pixel clock in Hz (4), one frame with its blanking in µs (4). A change is made by the desktop at its next tick, after the reply: the signal stops and starts again, every window is brought onto the new screen and all is painted. Nothing is saved and nothing asks to keep it (that is the Display dialog's part). Bad arguments for a mode that does not exist. `tools/dbg.py mode [N]` |

- The board replies to Key, Mouse and Text before acting on them, so an event
  that restarts the board (Restart in the Shut Down dialog) still gets its
  reply and the tool does not send it again.
- Key codes are USB HID keyboard usage codes (0x04 is A, 0x28 is Enter), the
  same codes a USB keyboard will produce later.
- Mouse buttons: bit 0 left, bit 1 right, bit 2 middle. Each mouse message
  carries the full button state, so a click is one message with the bit set
  and one with it clear.
- Pixel format 1 is one byte per pixel, RRRGGGBB.

## Frames the board sends on its own

| Type | Name | Payload |
| --- | --- | --- |
| 0x70 | Log | Log text |
| 0x71 | Hello | Same as the Info reply, without the status byte. Sent once at start-up |
| 0x72 | Picture data | Chunk number (2), then encoded picture bytes |
| 0x73 | Picture end | Number of chunks (2), CRC-32 of the decoded picture (4) |

## Screenshots

The mouse pointer is not part of the picture in memory; the board mixes it
into each line on the way to the monitor. A screenshot mixes it in the same
way, so it shows what the monitor shows. With flag bit 0 set
(`tools/dbg.py screenshot --no-pointer`) it is left out, which gives the
picture in memory exactly. An empty request means "with pointer", as before.

After the Screenshot reply, the board sends the picture as numbered Picture
data frames and finishes with Picture end. They carry the request's sequence
number. The tool checks the chunk numbers, the size and the CRC-32, and asks
again if anything is wrong.

Encoding 1 is a run-length code. Each token starts with a control byte:

| Control byte | Meaning |
| --- | --- |
| 0 to 127 | Copy the next (n + 1) bytes as they are |
| 128 to 255 | Repeat the next byte (n - 125) times, so 3 to 130 times |

A token never straddles two chunks. A plain desktop shrinks to a few
kilobytes; the worst case (nothing repeats) is 0.8 % larger than the raw
307,200 bytes, about 3.5 seconds at 921,600 baud.

Encoding 2 is sent only when the request sets flag bit 1 (`tools/dbg.py`
always does). It adds copies from a few fixed distances back, which suit the
dithered desktop colour, where nothing repeats three times in a row but
everything repeats every 4 pixels and every 4 rows:

| Control byte | Meaning |
| --- | --- |
| 0 to 127 | Copy the next (n + 1) bytes as they are |
| 128 to 255 | One more byte d follows. Repeat (n - 125) bytes, 3 to 130, starting d' bytes back in the decoded picture, where d' is 1, 2, 4, one row, two rows or four rows for d = 0 to 5. The bytes may overlap what is being written, so copy them one at a time (or in pieces of at most d') |

On the PC tests the dithered desktop takes 308 KB with encoding 1 and 4.7 KB
with encoding 2; a flat colour takes 4.7 KB with either; noise costs 0.8 %
more than the raw picture with either. On the board a screenshot of the
desktop takes 0.4 seconds with encoding 2 (2.2 seconds with encoding 1),
including opening the port.

## Files

Paths are written `C:\Documents\notes.txt` and are UTF-8. File errors come back
with status 3, followed by the error code (`ports::FsError`) and its text,
such as "not found" or "the folder is not empty". `tools/dbg.py` has
`ls`, `get`, `put`, `rm` and `mkdir`; `put` sends about 1000 bytes per
request (150 KB takes 3 seconds each way on the board). C: on the board is a
RAM disk: what is put there is lost at restart.

## Tasks

The Tasks reply lists every FreeRTOS task. After the status byte:

| Bytes | Field |
| --- | --- |
| 8 | The clock the run times are counted on, in µs since start-up |
| 1 | Processor cores. Each can give one clock's worth of run time, so the run times add up to at most cores × clock |
| 1 | Tasks that follow |
| 1 | Tasks left out because the reply was full (29 fit with the longest names) |

Then, per task:

| Bytes | Field |
| --- | --- |
| 2 | The kernel's number for the task. It keeps it for life, so two replies can be matched by it |
| 1 | State: 0 running, 1 ready, 2 blocked, 3 suspended, 4 deleted, 5 unknown |
| 1 | Priority; higher runs first |
| 1 | The core it is tied to, or 0xFF for either. Not the core it is on right now |
| 1 | Flags: bit 0, its stack is in external RAM |
| 8 | Processor time in µs since start-up. Interrupts that arrive while a task runs count as that task's time |
| 4 | Bytes of its stack never used so far |
| 1 + n | Name length, name |

The board sends counters, not percentages: whoever asks takes two replies and
divides the differences. `tools/dbg.py tasks` does that one second apart
(`--interval`) and prints the share of one core since start-up (`%START`) and
between the two replies (`%NOW`), busiest first; `--json` prints the last
reply. Programs on the desktop share the `ui` task and do not appear on their
own.

Reading the list stops task switching on the board for a moment while the
kernel copies it, so it is for a look now and then, not for polling many times
a second.

## Wi-Fi

Request type 0x20. The first payload byte says what; names and passwords
travel as a length byte followed by the bytes.

| Byte | Command | Rest of the request | Reply |
| --- | --- | --- | --- |
| 0 | Status | | link (0 no radio, 1 not connected, 2 connecting, 3 connected), why the last connect asked for failed (0 none, 1 wrong password, 2 not found, 3 no answer, 4 refused), scanning, clock set from the network, signal in dBm (signed), channel, address (4), router (4), the board's MAC (6), number of remembered networks, name of the network |
| 1 | Scan | | nothing; the list is ready when "scanning" is 0 |
| 2 | List | | scanning, count, then per network: signal, channel, security (0 open, 1 password, 2 needs a user name), flags (bit 0 remembered, bit 1 connected), name. Then the count of remembered networks and their names |
| 3 | Connect | name, remember (0 or 1), password given (0 or 1), password | nothing; follow it with Status. Without a password the remembered one is used |
| 4 | Disconnect | | |
| 5 | Forget | name | |

Passwords are never sent back by the board. `tools/dbg.py wifi` shows the
status; `wifi scan`, `wifi connect [NAME]`, `wifi disconnect` and `wifi
forget NAME` do the rest. `wifi connect` asks for the password at a prompt
that does not show it (or reads it with `--password-stdin`); it is never a
command-line argument, so it does not end up in the shell's history.

## Windows

The Windows reply lists the windows from the bottom of the stack to the top:

```json
{"windows":[{"id":1,"pid":5,"z":0,"title":"Welcome to Chinchilla","x":96,"y":30,"w":400,"h":280,"state":"normal","focused":false}, ...],
 "menus":0,"start_menu":false,"selected_icon":-1,"update_us":6246,"event_us":6639}
```

`menus` is how many pop-up menus are open, `start_menu` whether the Start
menu is, `selected_icon` the selected desktop icon (-1: none).
`state` is `normal`, `minimized` or `maximized`. `update_us` is how long the
last repaint took and `event_us` how long the last input event took to
handle, painting included. If the list does not fit in one reply the board
sends `{"error":"too many windows"}`. `tools/dbg.py windows` prints the list;
`tools/dbg.py repaint` repaints and prints the time, and comparing a
screenshot before and after it shows whether anything was left stale.

## Reset lines

The board's reset and boot pins are wired to the serial port's DTR and RTS
lines. `tools/dbg.py` opens the port in a way meant to leave the board
running, so a screenshot shows the current state and not a fresh start.
Confirmed on the bench: uptime and the pointer position survive repeated
connections. `--reset` restarts the board on purpose.

## Planned, not yet in the protocol

A list of windows and widgets as structured data, waiting until the screen is
idle, file transfer, and carrying network traffic for dial-up (see
`docs/DECISIONS.md`).

## Missed video refills

The video interrupt counts refills that were not done in time (`refills /
late / missed` in the memory report). It never logs; once a second the
scanlog task writes a line to the system log when the count has gone up, at
once for the first miss of a burst and then at most once every 10 s with what
came meanwhile:

    W (2000) os: video: 3 refills missed, worst 640 us late, last at line 240
    W (3000) os: video: 2 refills missed (all during a debug task list), worst 812 us late, last at line 96
    W (4000) os: video: 5 refills missed (2 during a debug task list), worst 640 us late, last at line 30

Misses that happened while the Tasks request (0x14) walked the task list are
counted apart and named: that request holds the kernel's lock for about
0.8 ms and is known to cause them (docs/journal/2026-11-17-refills.md). Late
refills that still finished in time are not logged; they stay in the counters.

`tools/dbg.py misses` prints the lines with misses not caused by the debug
link and exits with status 1 if there are any (0 otherwise); `--all` also
prints the debug link's own.

