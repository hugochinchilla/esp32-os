---
title: "Chosen in the remote screen milestone"
date: 2026-11-16
by: owner+agent
status: decided
area: network
summary: "The VNC server: RFB versions, one viewer, changed rectangles, pixel formats, ZRLE, keys, the browser viewer"
---

# Chosen in the remote screen milestone

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
