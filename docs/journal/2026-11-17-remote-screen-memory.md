# The remote screen gives its memory back

## What was asked

The board check of the program lifecycle (PR 22,
[journal](2026-11-17-program-lifecycle.md), section 9) showed one thing that
branch did not touch: the first VNC viewer takes about 183 KB of external
RAM, and the remote-screen server kept it until the board restarted. The
coordinator asked the program-lifetime thread to give that memory back when
the last viewer disconnects, on top of the services loop (PR 24, backlog 8b),
which moves the server into a task of its own.

## What the memory is

Three blocks from the Memory port, asked for in `rfb::Server::start()` at the
first connection:

| Block | Bytes | For |
| --- | --- | --- |
| `out_` | 18,529 | what is waiting to go to the network: one packed 64x64 block and its header |
| `tile_` | 16,385 | one 64x64 block as ZRLE writes it, before packing |
| the compressor's | 147,456 | the last 64 KB of data and two tables of positions (`net::Deflater`) |

182,370 bytes together. The damage grid and the colour table are part of the
server object itself (about 7 KB, already in external RAM since backlog 35)
and stay.

## Options

| | What | For | Against |
| --- | --- | --- | --- |
| **A. Give it back at once** (chosen) | When a poll ends with no viewer, release the three blocks. The next viewer asks for them again | With nobody watching the remote screen holds nothing. No clock, no state. Asking for 183 KB of external RAM takes microseconds, against a connection that takes many milliseconds | A viewer that reconnects in a loop asks and gives back each time |
| B. Give it back after a pause (say 30 s without a viewer) | Keep the blocks a while in case the viewer comes back | Saves the asking on a quick reconnect | A timer in the services task for a cost that is not there; memory held for 30 s for nothing; more to test |
| C. Leave it | Nothing | No change | 183 KB lost to every program after one look from a phone, until a restart. It is the same rule as windows: what is not open holds no memory |
| D. Smaller buffers instead | A compressor with a 4 KB window | Less to hold | Worse packing on every update, for all the time a viewer *is* connected. Does not answer the question |

A was chosen: it is the rule the lifecycle work set for windows, applied to a
service, and the simplest of the four. The cost B avoids was looked for and
is too small to see.

## What was built

- `rfb::Server::free_buffers()` releases the three blocks. `poll()` calls it
  at its end when no viewer is left.
- It is done at the end of `poll()` and not inside `drop()`. `drop()` is
  called from the middle of reading a message and of sending, by code that
  goes on to look at the buffers' counters; and when a new viewer takes over
  from the one connected, the server drops one and starts the other in the
  same poll, and there the buffers are simply kept.
- Only the services task touches these buffers (`damage()`, the one call
  from the UI task, writes the grid only), so no lock is involved.
- A fault found on the way: if the Memory port could give one or two of the
  three blocks but not all, `start()` turned the viewer away and lost the
  blocks it had been given. They now go back, and the next viewer is tried
  afresh.

## How it was tested

On the PC (`host/tests/test_rfb.cpp`, `test_memory`), with the memory
adapter that counts blocks and the address sanitizer on:

- a viewer connected: three blocks, between 180 and 190 KB;
- a second viewer taking over: still the same three;
- the viewer gone: none, and none after further polls;
- a viewer the server throws out (an unknown message): none;
- 50 connections and disconnections: none left;
- after all that, a new viewer still gets a whole compressed 640x480 screen
  and its keys arrive;
- a Memory port that gives 0, 1 or 2 blocks: the viewer is turned away,
  nothing is left held, and with memory again the next viewer is served.

All 25 PC tests pass. The whole firmware builds and links with ESP-IDF v5.5
in the cloud container.

Not run on the board. To check there: `tools/dbg.py memory` at the empty
desktop, with a viewer connected (about 183 KB less external RAM), and after
it disconnects (back to the first figure); the same with the web page's
viewer; and ten connections in a row.
