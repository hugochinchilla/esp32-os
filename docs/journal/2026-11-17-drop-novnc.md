# The noVNC page is dropped

2026-11-17, the board thread, on `main`.

## What was asked

The owner, in the project thread: "drop the novnc". On 2026-11-16 a second
page, `http://<board>/novnc`, had been added beside our own viewer at `/`,
so the two could be compared. It loaded noVNC 1.7.0 from the jsDelivr CDN at
a pinned commit and carried the SHA-384 hash of each of its 52 files
(`DECISIONS.md`, "noVNC from a CDN"). Our viewer works without the internet;
noVNC needed it, and warned that it wanted https, which the board does not
have.

## Options

- *Remove the page only, keep the web server's support for a second page.*
  Not chosen: nothing else uses it, and the owner's rule today is to keep
  things simple and not keep what is not used.
- *Remove the page, its hashes, the tool that made them, and the second-page
  support (chosen).* `/` and the WebSocket are untouched.

## What was removed

- `core/src/net/viewer_page.cpp`: the noVNC page (`kNoVnc`, `novnc_page()`,
  `novnc_page_size()`).
- `core/src/net/novnc_files.inc` (9.5 KB of hashes) and
  `tools/novnc_integrity.py`, which made it.
- `net::WebGate::set_second_page()`, its three members, the request's
  `second` flag and the branch that served it (`core/include/core/net/web.hpp`,
  `core/src/net/web.cpp`).
- The two calls that set it: `firmware/os/main/main.cpp` and `host/sim.cpp`
  (the Linux desktop program and the browser build never set it).
- `tools/test_vnc_sim.py`: the checks of the noVNC page became one check that
  `/novnc` answers 404.
- `firmware/os/README.md`: the sentence about `/novnc`. `DECISIONS.md`: the
  section now says it was dropped and why, the earlier notes kept below.

No CMake entry, third-party notice or backlog item named noVNC.

## How it was tested

- PC tests 42 of 42 (`vnc_tool` with the new 404 check).
- Firmware: 0x167a10 bytes before, 0x164ee0 after: **11,056 bytes smaller**.
- On the board, after a flash: `/novnc` answers HTTP 404; `/` gives our
  viewer page (9,376 bytes). In a headless Chromium on the laptop the
  viewer connected over the WebSocket and drew the full screen (640 x 480,
  303,073 pixels that are not black), and three keys typed in the browser
  reached an open Editor (its title gained the star). A VNC viewer on port
  5900 took a full screen in 2.97 s. `dbg.py misses`: none.
