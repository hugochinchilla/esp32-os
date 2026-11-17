# The serial console, on the debug link's wire

2026-11-17. Backlog item 14.

## What was asked

"Serial console sharing the wire with the debug link", marked "with care".
The architecture says: "the console and the debug protocol are both live on
it at all times. Bytes between two zero bytes that pass the frame check go
to the debug service, everything else to the console, so `tools/dbg.py`
keeps working whatever state the shell is in." Rejected then: a console that
switches into the debug protocol by a command, because the agents' tool
would be locked out whenever the shell is stuck.

## What was found

- The debug link already frames everything between zero bytes and drops
  what is not a frame, and the tool already shows unframed text from the
  board as text. So the wire can carry both without changing a frame.
- The rule as written ("between two zeros that pass the check") decides
  only when the closing zero has arrived. A person at a terminal never
  sends a zero: their typing would wait for ever.
- The care is in two places. A frame's bytes must never be taken as typing:
  they are any bytes, an Enter among them, and the shell deletes files. And
  a zero that no frame follows must not leave the keyboard dead.
- The shell's `session_environment` kept one note of whose memory a session
  uses; telnet and the console would have shared it.

## Options

**Telling typing from frames.**

- *Chosen: a zero opens a frame; outside a frame every byte is typed and
  goes to the shell at once.* A good frame closes it. A frame that fails
  its check leaves the wire "inside a frame", because its last zero may be
  the first of the next one.
- Not chosen: *decide at the closing zero* (the rule as first written).
  Typing would need a zero to be seen.
- Not chosen: *a command that switches the wire to the console.* Rejected
  by the owner's architecture already.
- Not chosen: *a second serial port.* The board has one USB serial socket
  wired; the other pins are spoken for.

**A zero with no frame after it** (noise, Ctrl+@ at a terminal).

- *Chosen: if nothing at all arrives for half a second while a frame is
  open, the frame is given up and what it held is taken as typed.* The
  silence is measured between two moments when the loop found no bytes
  waiting, not between calls: a frame cut in two by a busy desktop is not
  mistaken for typing, because its second half is already waiting.
- Not chosen: *a time limit from the opening zero.* The loop can be late
  for longer than any limit while the bytes sit in the driver.
- Not chosen: *nothing.* One stray byte and the console is deaf until the
  tool next speaks.

**What the session's output looks like on the wire.**

- *Chosen: plain text between frames*, under the link's own lock so it is
  never inside one. Any terminal program shows it; the tool tells it from
  frames by the check, as it did for the chip's boot text.
- Not chosen: *console output in frames of its own.* Cleaner for the tool,
  unreadable in a plain terminal program, which is the point of a console.

**Is it on?**

- *Chosen (to confirm): off unless the start-up file says `start console`.*
  It is a task of its own, 12 KB of internal RAM for good, and a shell that
  anything connected to the port can type into. The line is in the file,
  commented out.
- Not chosen: *always on.* The item says "with care", and the memory is
  wanted elsewhere on most days.

**After `exit`.**

- *Chosen: the next session greets*, on the same task, as a login prompt
  comes back on a Unix console.

## What was built

- `dbg::Protocol`: `set_console`, `line_idle`, `send_text`; `feed` tells
  typing from frames. Without a console set it behaves as before.
- `core/sh/console.hpp`: `sh::Console`, a TTY that lasts and one session
  after another on a task of its own.
- The board: `console` is something the start-up file can start; a row in
  the process list, protected. The loop tells the link when it found no
  bytes. The simulator the same, and `CHINCHILLA_STARTUP` gives it a
  start-up file for one run.
- `tools/dbg.py console`: this terminal as the board's console, with the
  tool's frames told apart; Ctrl+] leaves.
- Each caller of `session_environment` gets its own memory note.
- `docs/debug-protocol.md` says how the wire is shared.

## How it was tested

- `host/tests/test_console.cpp`: typing with and without a frame around
  it; a frame a byte at a time; frames whose contents are commands (`del
  *.*`, `format c:`) never typed; a bad frame straight before a good one,
  and the two sharing a zero; a stray zero and the half second; a slow
  frame that is still a frame; a run longer than any frame; text and frames
  on the way out; then a real session on a thread with the tool's requests
  in the middle of a line, 28 pings while a command waits for a key, and
  `exit`.
- `tools/test_serial_console.py`, against the simulator with the real tool:
  `echo`, `ps` (init 1, desktop 2, console protected), a file each way, a
  screenshot while `help` prints, the desktop's windows untouched, `exit`.
- All 35 PC tests pass. The firmware compiles and links (0x15c430 bytes).
  It has not run on the board.

## What is left

- On the board, with care: first with the line left out, that the tool is
  exactly as before. Then with `start console`: `tools/dbg.py console`,
  `dir`, `top`, `ted`; the tool's screenshot, `put` and `get` while a
  listing runs; a plain terminal program at 921,600 baud; internal RAM
  before and after (12 KB expected); and the tool's whole test run with the
  console on.
- The console needs the desktop started first, so it is not yet the way to
  start a machine without one.
- `ted` and `top` assume 80 by 24; a serial terminal does not say its size.


## On the board (added by the board thread, 2026-11-17)

Branch commit b611ea1 with main b74568e merged in. The backlog table and the
journal index conflicted; the resolution keeps the board results already on
main and the branch's new lines. PC tests passed (36 of 36; `vnc_tool` failed
once when the tests ran 8 at a time, then passed alone and in a full run one
at a time). The firmware build was clean.

With the console off (the default), the usual `dbg.py` run, against main
b74568e measured just before:

| | main | console off |
|---|---|---|
| ping, median / most | 0.49 / 0.61 ms | 0.49 to 0.50 / 0.61 to 0.63 ms |
| screenshot, median | 0.170 s | 0.170 to 0.172 s |
| 300 KB put / get, byte for byte | 28.6 / 6.7 s, equal | 28.9 / 5.3 to 6.1 s, equal |

Key, text, mouse, windows, memory, tasks, ls, time, Wi-Fi status and list,
the boot screens with repaint, a fast restart and `monitor` behaved as before.
(Two runs showed a 11.8 s get and a `monitor` with one line; repeated, the get
took 6.1 s three times, and `monitor` gave 138 lines when it had the port to
itself. The one-line results came from my script opening a second connection
to the port while `monitor` read it.)

With `start console` after `start desktop` and a restart:

1. `ps`: init 1, desktop 2, console 3 (SP), inetd 4. `tools/dbg.py console`
   on a pseudo-terminal: the prompt, `dir`, `ps`, and Ctrl+] left with exit
   status 0.
2. `top` refreshed 3 times in 5 s and left with q. `ted C:\c.txt` drew its
   screen with the status line, and typing, Ctrl+S and Ctrl+Q worked (the file
   then held the typed text).
3. During `type` of a 300 KB file: three screenshots (0.18 s each), a
   100 KB put and get compared byte for byte (equal), and no frame byte in
   the console text. Ctrl+C stopped a listing.
4. Log lines on the same wire: a VNC viewer connecting writes its log lines
   while the console is on; afterwards `echo` answered normally and nothing
   said "no such command". A log line never ran as a command.
5. A panic (`dbg.py fault pointer`): the Guru Meditation line,
   StoreProhibited, the backtrace and "Rebooting" arrived as readable console
   text. After the restart the console answered again.
6. `idf.py flash` reset and flashed the board with the console on.
7. Internal RAM: 197,171 bytes free with the console on against 210,111 with
   it off (about 13 KB, most of it the session's 12 KB stack); after all the
   console work, back to the byte.
8. At the end `start console` was taken out again: the start-up file is
   byte for byte the default the first start wrote.

**A known limit: console text can be lost.** While `type` of a 300 KB file
streams to the console, three screenshots followed by a 100 KB put and get
lost a block of about 270 lines (around 20 KB) near the start of the listing,
in 3 runs out of 3. Screenshots alone, or put and get alone, lost nothing in
5 runs (all 4,200 lines arrived). Frames and files stayed exact; with the
console off nothing changes. With `dbg.py` instrumented, no frame failed to
decode, so this is not the frame parser. The board writes console text with a
blocking UART write and does not drop it. My belief, not proven: the bytes are
lost between the board and the Python program. The link has no flow control,
and while the one-threaded client is busy (decoding a screenshot, waiting on
put replies) and not reading, the PC's serial buffer fills, the USB serial
bridge is held off, and the bridge drops what the board keeps sending. The
console is the only stream on the link that nobody asked for, which is why
only it is hit. Left open for the console's thread (backlog 14b).
