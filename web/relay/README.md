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

### From GitHub, every time it changes (the owner's choice)

`ci/github-relay-workflow.yml` deploys the relay whenever a file in this
folder changes on `main`: `worker.js`, or `wrangler.toml`, its
configuration. It runs `test.mjs` first and does not deploy a relay that
fails it. To turn it on, once:

1. Copy `ci/github-relay-workflow.yml` to `.github/workflows/relay.yml`
   (agent sessions cannot push workflow files).
2. On Cloudflare, make an API token: My Profile > API Tokens > Create
   Token. Either the template "Edit Cloudflare Workers", which is what
   Cloudflare's guide uses and allows more than is needed, or a custom
   token with one permission, **Account > Workers Scripts > Edit**, and
   under Account Resources only the account the relay goes to. No zone
   permission is needed: the relay lives at `workers.dev`, not on a domain.
3. On GitHub: Settings > Secrets and variables > Actions > Secrets > New
   repository secret, twice:
   - `CLOUDFLARE_API_TOKEN`: the token.
   - `CLOUDFLARE_ACCOUNT_ID`: the account's id (Cloudflare's dashboard,
     Workers & Pages, "Account ID" at the right).
4. Run it: Actions > "Deploy the relay" > Run workflow. Its log ends with
   the relay's address, `https://chinchilla-relay.<account>.workers.dev`.

The first deploy of an account may ask for a `workers.dev` name to be
chosen on Cloudflare's dashboard (Workers & Pages) before it succeeds.

### By hand

With the dashboard: dash.cloudflare.com, "Workers & Pages", "Create",
"Worker" ("Hello World"), named `chinchilla-relay`; then "Edit code",
replace everything with the contents of `worker.js`, deploy. Or with
Cloudflare's tool, from this folder: `npx wrangler login`, then
`npx wrangler deploy`.

## Telling the page

The page is built with the relay's address:

    emcmake cmake -S web -B build/web -DCHINCHILLA_RELAY=https://chinchilla-relay.<account>.workers.dev/
    cmake --build build/web

The published page is built by `.github/workflows/pages.yml`.
`ci/github-pages-workflow.yml` is that file with the address taken from a
repository variable: copy it over `.github/workflows/pages.yml`, and on
GitHub add the variable `CHINCHILLA_RELAY` (Settings > Secrets and
variables > Actions > Variables) with the relay's address, ending in `/`.
Then run "Publish the browser version" once.

To try a relay without rebuilding, open the page with
`?relay=https://chinchilla-relay.<account>.workers.dev/` after its address.
With no relay at all, Vizcacha says there is no network.

## If the page moves

`ALLOWED_ORIGINS` in `worker.js` lists the sites whose pages may use the
relay: `https://www.hugochinchilla.net` and, for trying it at home,
`http://localhost:8080`. A page served from anywhere else is refused (403).

## Trying it

`node web/relay/test.mjs` tries the relay's rules with the site faked: no
Cloudflare, no network. And once it is deployed:

    curl -H 'Origin: http://localhost:8080' \
      'https://chinchilla-relay.<account>.workers.dev/?url=http%3A%2F%2Ffrogfind.com%2F&agent=Mozilla%2F4.0%20(compatible)'

prints FrogFind's home page. Without the `Origin` line it prints "This relay
serves the page of Chinchilla only."
