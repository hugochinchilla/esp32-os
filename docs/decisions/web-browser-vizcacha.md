---
title: "The web browser, Vizcacha"
date: 2026-11-17
by: owner+agent
status: decided
area: programs
summary: "Vizcacha, a browser for 1990s HTML with no scripts or style sheets; HTTPS through a relay (backlog 53)"
---

# The web browser, Vizcacha

Backlog item 53, asked by the owner. Journal:
`docs/journal/2026-11-17-web-browser.md`.

The owner's:

- A browser for HTML of the 1990s: no scripts, no style sheets. The
  benchmark is FrogFind (`frogfind.com`).
- It works in the web page too. The page reaches sites through a relay for
  web requests, a Cloudflare Worker he runs (`web/relay/`), not a TCP
  tunnel and not somebody else's proxy. The board and the PC use sockets.

The agent's, to confirm:

- It is called Vizcacha.
- No DOM: a flat list of items in one block of memory, laid out again when
  the width changes. One font; headings are it drawn larger.
- HTTP/1.0 with a Host line and `Connection: close`; moved sites followed
  five times. No TLS: an https address gets a page that says so and offers
  a FrogFind search. Rejected for now: mbedTLS (internal RAM, certificates,
  and a second and third implementation for the PC and the page).
- Forms with GET only. Tables as rows, frames as links, pictures as a box
  with their `alt` text. A page is 256 KB at most.
- Outgoing connections are OS calls, `os::tcp`: handles the system keeps
  per process (four in all, two each), closed when the process ends;
  `connect` never waits, the dialling is a background job. The machine
  gives the way out as a `net::Dialer` (`os::install_dialer`).
- The relay answers only pages from `https://www.hugochinchilla.net` (and
  `http://localhost:8080`), only GET, only public http sites on port 80 or
  8080, 512 KB at most. Its address is built into the page
  (`-DCHINCHILLA_RELAY=`) or given as `?relay=`.
- The User-Agent is `Mozilla/4.0 (compatible; Vizcacha 1.0; Chinchilla;
  ESP32-S3)`: FrogFind refuses one that does not look like a browser's.
