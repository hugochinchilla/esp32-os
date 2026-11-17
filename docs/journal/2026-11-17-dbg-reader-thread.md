# The PC tool reads the port all the time

2026-11-17. Backlog item 14b.

## What was asked

The board thread found that console text can be lost: while `type` of a
300 KB file streamed to the serial console, three screenshots and a 100 KB
put and get lost a block of about 270 lines, three runs out of three
([the serial console](2026-11-17-serial-console.md), "A known limit").
Frames and files stayed exact. Its belief, not proven: the link has no flow
control, `tools/dbg.py` has one thread, and while it is busy and does not
read, the PC's buffer fills and the USB serial bridge drops what the board
goes on sending. The item was left open with two ways out named.

## The options

**1. Read the port on a thread in `dbg.py` (chosen).** A thread does
nothing but move bytes from the port to a queue of any length; the rest of
the tool takes them from the queue when it gets to it. Nothing changes on
the board. If the belief is right, this is the whole cure; if it is wrong,
it costs forty lines on the PC and the board test will say so.

**2. Pace on the board.** The console would send a block and wait, or send
no faster than some rate. Not chosen first: it slows every listing for
every client to cure a fault that, if the belief holds, is the PC tool's;
and it costs firmware and a board turn to try. It stays the next step if
option 1 does not cure it.

**3. Flow control on the wire (RTS/CTS).** The proper cure for a serial
link, and not available as the board is wired: the port's RTS and DTR
lines hold the board's reset and boot pins (the note in `Board.__init__`
of `tools/dbg.py`).

The choice is the agent's, of the two the backlog named. To confirm, and
to be proven on the board.

## What was built

- `tools/dbg.py`: `Board` starts a reader thread when the port opens. It
  reads with the same short timeout as before and puts each piece on a
  queue. `_read()` takes what has arrived from the queue. The thread ends
  when the port goes away or the tool closes, and says so through the same
  queue, so "the port went away" is still told where it was.
- The console command waited on the port and the keyboard together with
  `select()`. The thread now drains the port, so a pipe gets one byte for
  each piece queued and `select()` waits on the pipe (`Board.fileno()`).
- `tools/test_dbg_reader.py`, PC test `dbg_reader`: a stand-in port that
  holds 16 KB and drops the rest, as the bridge is believed to. A "board"
  sends 4,000 numbered lines at twice the real link's speed while the
  program sleeps a second and asks for nothing. All lines must arrive in
  order.

## How it was tested

- The new test against the tool as it was: 195,616 bytes dropped. With the
  thread: all 4,000 lines arrive.
- All 40 PC tests pass, three runs in a row. Those include the tool
  against the simulated board, the serial console test and the update
  tool, which use the same class.
- Not tested on the board. The simulated board cannot show the fault: its
  port is a pseudo-terminal, which makes the sender wait. The proof is the
  board thread's own case: `type` of the 300 KB file with three
  screenshots and a 100 KB put and get, and a count of the lines.


## On the board (added by the board thread, 2026-11-17)

Branch commit 555de73 with main ca93091 merged in (only the journal index
conflicted). Against main it changes only `tools/dbg.py`, its new test and
`host/CMakeLists.txt`; the board ran main's firmware throughout. PC tests 42
of 42 (`dbg_reader` new). `start console` was put in the start-up file for the
console checks and taken out again at the end (the file is byte for byte the
default).

1. The case of backlog 14b: `type` of a 300 KB file on the serial console
   while three screenshots and then a 100 KB put and get run. With this
   tool, three runs: 4,200 of 4,200 lines each time, no gap. With main's
   tool, the same run right after: 3,928 of 4,200, the lines 84 to 354 lost.
2. Every path through the reader, with the console on: ping, info, stats,
   memory, tasks, windows, screenshot, key, text and mouse, ls, a 300 KB put
   and get compared byte for byte, time, Wi-Fi status and list, a boot
   screen with repaint, `dmesg`, `misses` (exit 0), `--reset` through the
   control lines (answered 92 ms after), `reboot` and `reboot --fast` (the
   port going and coming back), `monitor` over a restart (147 lines, the
   "boot to desktop" line among them), and `console` on a pseudo-terminal
   (`dir`, `ps`, Ctrl+] left with status 0). `tools/ota.py` does not use
   `dbg.py`'s code, so it was not part of this.
3. Timing against main's tool: ping median 0.50 ms both (most 0.67 against
   0.61 ms), a screenshot 0.164 s against 0.167 s, a 300 KB put 28.9 s both
   and the get 6.2 s against 11.8 s in that run (main's get has varied from
   5.3 to 11.8 s in earlier runs).
