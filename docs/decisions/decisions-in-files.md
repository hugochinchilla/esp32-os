---
title: "One file per decision"
date: 2026-11-18
by: owner
status: decided
area: project
summary: "`docs/DECISIONS.md` is an index and each decision is a file in `docs/decisions/`; the list is written by a tool"
---

# One file per decision

Journal: `docs/journal/2026-11-18-decisions-in-files.md`.

- The owner's idea and choice (on a card, 2026-11-18): one file per
  decision in `docs/decisions/`, each starting with a block that gives its
  title, date, who decided, status, area and a one-line summary. Not chosen:
  one file per area (about twelve files of 10 to 25 KB: more is read than
  is needed, and two branches working in one area edit the same file).
- `docs/DECISIONS.md` keeps its path and becomes the index, so that the
  references to it in code, notes and the journal still lead somewhere.
  What always applies stays in the index itself, not in a file to open (the
  owner offered both; the agent took this one, to confirm).
- The agent's, to confirm: the list in the index is written from the files
  by `tools/decisions_index.py`, and the PC tests fail when it is out of
  date, so that parallel branches add files and never edit the same lines
  by hand. Titles are the old headings, so a reference by title is still
  found. The old tables (Rejected, Still open, Ideas kept for later, Found
  on the bench, To confirm on the bench, Planned order of work) are a file
  each; the Decided table is four files by area.
- The text of each decision was moved unchanged. Phrases such as "below" or
  "the section above" in it refer to the order of the old single file.
