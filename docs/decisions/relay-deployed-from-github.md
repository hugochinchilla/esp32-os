---
title: "The relay is deployed from GitHub"
date: 2026-11-18
by: owner+agent
status: decided
area: tools
summary: "The web version's relay goes to Cloudflare from a GitHub workflow"
---

# The relay is deployed from GitHub

Heading in the single-file log: "The relay is deployed from GitHub (the owner's decision, 2026-11-18)".


Journal: `docs/journal/2026-11-18-relay-workflow.md`.

- The owner's: the web version's relay goes to Cloudflare from a GitHub
  workflow, again whenever it changes; he turns the workflow on and gives
  the secrets.
- The agent's, to confirm: the trigger is the folder `web/relay/` on
  `main`; `web/relay/test.mjs` must pass first; the secrets are
  `CLOUDFLARE_API_TOKEN` (Account > Workers Scripts > Edit, one account)
  and `CLOUDFLARE_ACCOUNT_ID`; the page's build takes the relay's address
  from the repository variable `CHINCHILLA_RELAY`. The workflows wait in
  `ci/` until the owner copies them to `.github/workflows/`.
