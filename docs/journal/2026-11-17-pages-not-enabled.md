# Pages was not turned on

2026-11-17, early afternoon. Follows
[The first run of the Pages workflow](2026-11-17-pages-first-run.md). Only
the README changes.

## What was asked

The owner:

> The page deployment ci fails. Fix it, and alzó link the resulting page con
> the readme under a try it con the browser section

## What was found

After the build fix (PR 35) the workflow's first half passes: runs 3, 4 and
5 all build the page and upload it. Its second half, the deployment, fails
in each within a few seconds:

    Failed to create deployment (status: 404) ... Ensure GitHub Pages has
    been enabled: https://github.com/hugochinchilla/esp32-os/settings/pages

The workflow file is in place, but the repository's Pages setting was never
switched on. Turning the publication on had two steps (the workflow file,
and Settings > Pages > Source: "GitHub Actions") and only the first was
done.

## The options

1. The owner switches it on in the repository's settings: one choice in a
   list.
2. The thread that drives the owner's laptop does the same through GitHub's
   interface for programs, with his sign-in.
3. The workflow switches it on itself. GitHub does not let a workflow's own
   token do this; it would need a personal token of the owner's stored as a
   secret, which is more work and more risk than the setting.
4. Publish from a `gh-pages` branch instead. It needs the same setting.

## What was chosen

1 or 2, whichever comes first: the laptop thread was asked, with the
owner's message, and the owner was told. There is nothing to fix in the
workflow; it is the setting. 3 and 4 do not avoid it.

## What was built

The laptop thread turned Pages on with the owner's sign-in (source: GitHub
Actions, nothing else changed) and ran the failed deployment of run 5
again. It passed.

The page is at `https://www.hugochinchilla.net/esp32-os/`, not at the
`github.io` address everyone expected: the owner's own GitHub site has his
domain set, and GitHub applies it to every project's pages too.
`https://hugochinchilla.github.io/esp32-os/` answers with a redirect (302)
to it.

`README.md` gets a "Try it in the browser" section near the top with that
address. Checked from the cloud: the address serves the page (title
"Chinchilla", the text "Starting Chinchilla..."), also over https. Not
checked: the published page running in a browser; the cloud machine's
network does not reach that host, and the same build was run in Chromium
before publishing.

## What was learned

A step that needs a person should be checked, not assumed, before the
first run: "the build failed" hid "and the deployment would have failed
too" for an hour.
