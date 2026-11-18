# The relay of the web version

The web version of Chinchilla (`web/`) runs inside a browser page, and a page
cannot open a connection to a site: it may only ask for web addresses, and a
page served over https may not ask for a plain-http one at all. FrogFind and
the other sites Vizcacha is for are plain http.

So the page asks a relay: `worker.js`, a Cloudflare Worker. Vizcacha writes
its request into what it believes is a connection
(`host/desktop/web_dialer.hpp`); the page reads the address out of it and
asks the relay with `fetch()`; the relay fetches the site and hands the
answer back. On the board and on Linux none of this exists: there Vizcacha
talks to the site through real sockets.

What the relay does and refuses is at the top of `worker.js`. In short: only
pages served from `ALLOWED_ORIGINS`, only GET, only public `http://` sites on
port 80 or 8080, at most 512 KB of answer.

## Putting it on Cloudflare

A free Cloudflare account is enough (100,000 requests a day).

With the dashboard, no tools:

1. dash.cloudflare.com, "Workers & Pages", "Create", "Worker" ("Hello
   World"). Name it `chinchilla-relay` and deploy.
2. "Edit code", replace everything with the contents of `worker.js`, deploy.
3. Note its address: `https://chinchilla-relay.<account>.workers.dev/`.

Or with Cloudflare's tool, from this folder:

    npx wrangler login
    npx wrangler deploy

## Telling the page

Build the page with the relay's address:

    emcmake cmake -S web -B build/web -DCHINCHILLA_RELAY=https://chinchilla-relay.<account>.workers.dev/
    cmake --build build/web

The published page is built by `.github/workflows/pages.yml`: add the same
`-DCHINCHILLA_RELAY=...` to its `emcmake cmake` line.

To try a relay without rebuilding, open the page with
`?relay=https://chinchilla-relay.<account>.workers.dev/` after its address.
With no relay at all, Vizcacha says there is no network.

## If the page moves

`ALLOWED_ORIGINS` in `worker.js` lists the sites whose pages may use the
relay: `https://www.hugochinchilla.net` and, for trying it at home,
`http://localhost:8080`. A page served from anywhere else is refused (403).

## Trying it

    curl -H 'Origin: http://localhost:8080' \
      'https://chinchilla-relay.<account>.workers.dev/?url=http%3A%2F%2Ffrogfind.com%2F&agent=Mozilla%2F4.0%20(compatible)'

prints FrogFind's home page. Without the `Origin` line it prints "This relay
serves the page of Chinchilla only."
