---
title: "noVNC from a CDN: dropped"
date: 2026-11-17
by: owner
status: decided
area: network
summary: "The `/novnc` page was removed; our own viewer at `/` is enough. Keeps the record of what was built"
---

# noVNC from a CDN: dropped

Heading in the single-file log: "noVNC from a CDN: dropped (the owner, 2026-11-17)".


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
