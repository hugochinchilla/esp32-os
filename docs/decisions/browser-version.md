---
title: "Chosen for the browser version"
date: 2026-11-17
by: agent
status: to-confirm
area: tools
summary: "Chinchilla in a web browser, built with Emscripten from the same source as the PC program (backlog 33)"
---

# Chosen for the browser version

Heading in the single-file log: "Chosen for the browser version (2026-11-17, to confirm)".


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
