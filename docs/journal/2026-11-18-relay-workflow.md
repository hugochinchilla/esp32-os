# The relay deployed from GitHub

Follows [Vizcacha, a web browser](2026-11-17-web-browser.md), backlog item 53. Branch `claude/project-thread-w6qa7m`.

## What was asked

The owner, 2026-11-18:

> Mira, para desplegar el relay de Vizcacha, yo tengo una API key de Cloudflare, pues ya tengo cuenta con ellos. Entonces lo que quiero es que tú prepares todo lo necesario para desplegarlo desde un workflow de GitHub. Entonces, que esté configurado en un ficherito, que cada vez que ese ficherito cambie se tenga que desplegar otra vez. Y supongo que no tendrás permisos para activarlo tú, entonces ya me pedirás a mí que haga yo el moverlo de la carpeta CI a la carpeta GitHub Workflows, y yo proveeré el secreto a través de GitHub cuando me expliques con qué nombre lo tengo que crear y qué permisos necesito darle a la API key que generaré para este despliegue.

## What was found

- The relay was already a folder that Cloudflare's tool can deploy: `web/relay/worker.js` and its configuration, `web/relay/wrangler.toml`. The steps in its README were by hand.
- An agent session cannot push to `.github/workflows/`. The page's workflow got there by the owner copying it from `ci/`, and the copy in `ci/` was removed then.
- The page is built without a relay's address: `.github/workflows/pages.yml` runs `emcmake cmake -S web -B build/web` with no `-DCHINCHILLA_RELAY`.
- Cloudflare's guide for GitHub Actions uses `cloudflare/wrangler-action@v4`, the secrets `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID`, and the token template "Edit Cloudflare Workers".

## What was chosen

The owner: deployed from a GitHub workflow, again whenever its file changes; he activates the workflow and provides the secret.

The agent's, to confirm:

- **The trigger is the folder `web/relay/`** on `main`, not one file: the relay is two files, the code and its configuration, and a change to either is a new relay. Also by hand (Run workflow).
- **Cloudflare's own action**, not `npx wrangler` in a step: it is the documented way and keeps the token out of the command line.
- **The relay's rules are tried before it goes out.** `web/relay/test.mjs` runs the Worker in Node with the site faked (ten cases: allowed, moved, too big, wrong page, https, private address, wrong port, POST). A relay that would answer anybody is not deployed.
- **The page takes the relay's address from a repository variable**, `CHINCHILLA_RELAY`, not from a line in the workflow: the address is known only after the first deploy, and changing it then needs no commit. `ci/github-pages-workflow.yml` is the page's workflow with that added, for the owner to copy over the active one.
- **The least token**: one permission, Account > Workers Scripts > Edit, on one account. The template "Edit Cloudflare Workers" also works and allows more (KV, R2, routes on every zone). No zone permission, because the relay lives at `workers.dev`.

Not chosen: the account's id written into `wrangler.toml`. It is not a secret, but it is the owner's, and the repository is public.

## What was built

- `ci/github-relay-workflow.yml`: on a change under `web/relay/` on `main`, Node 22, `node web/relay/test.mjs`, then `wrangler deploy` in `web/relay`.
- `ci/github-pages-workflow.yml`: the active page workflow plus `-DCHINCHILLA_RELAY=${{ vars.CHINCHILLA_RELAY }}`.
- `web/relay/test.mjs`, and the README's steps rewritten.

## How it was tested

`node web/relay/test.mjs` passes here. Both workflow files parse as YAML. **Neither workflow has run**: they cannot from an agent session, and the deploy needs the owner's token. The permission "Workers Scripts: Edit" being enough is from Cloudflare's documentation of the permission, not from a deploy made with such a token; if the first run is refused, the template is the fallback.

## What is left

The owner: copy the two files into `.github/workflows/`, make the token, add the two secrets, run the deploy, add the variable with the address it prints, run the page's workflow. Then Vizcacha in the page can be tried against FrogFind for the first time.
