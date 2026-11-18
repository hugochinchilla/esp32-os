# The decisions log, cut into files

2026-11-18. Documents only; no code of the OS changed.

## What was asked

The owner: "parece que todos los hilos nuevos comienzan por leer las
decisiones, y es un archivo enorme, me pregunto si tiene sentido fragmentarlo
en varios ficheros, un criterio podría ser un fichero por decisión con un
frontmatter que añada la fecha, quien, topic, etc. Y mantener un índice de
forma que solo con leer el índice cada agente sepa que tareas son relevantes
y cuales no, leyendo solo lo relevante para cada caso. El índice puede marcar
algunas decisiones como "core" que deben leerse siempre, o puede que ese
contenido "core" deba permanecer en el índice y no ser extraido a un
subfichero. Esto es una idea conceptual, la implementación más correcta puede
ser diferente a la que sugiero".

## What was found

`docs/DECISIONS.md` was 2,992 lines and 193 KB, roughly 50,000 tokens, and
`CLAUDE.md` tells every thread to read it before anything else. It had grown
by appending: 95 sections under `###`, many of them filed under a heading
they had nothing to do with ("Chosen for the TTY" sat under "Planned order of
work", the chess game under "Still open"), a "Decided" table of 37 rows, and
"Last updated: 2026-11-16" at the top. About seventy other files name it,
most of them as `docs/DECISIONS.md`, "a section's title". Three branches were
adding sections to its end at the time, which is also where they would
collide.

## The options

1. **One file per decision** with a block of fields at the top, and an index
   of one line each. About a hundred small files (the largest ordinary one is
   4.4 KB). A thread reads the index and two or three files. A new decision
   is a new file, so branches do not meet.
2. **One file per area**, about twelve files of 10 to 25 KB and a tiny index.
   Fewer files, but a thread reads a whole area for one decision, and two
   branches in one area edit the same file.
3. Where the "core" goes: in files marked as core in the index, or written
   into the index itself.
4. The index written by hand, or by a tool from the files' fields.

## What was chosen

The owner chose one file per decision, on a card. The rest is the agent's
and his to change:

- `docs/DECISIONS.md` keeps its name and is the index. Moving it would have
  broken every reference; this way they lead to the index, where the title
  they quote is a line with a link.
- What always applies is in the index, nine short points with links. A core
  that has to be opened is a core that is skipped, and the working rules
  were already in `CLAUDE.md`, which every session is given.
- The list is written by `tools/decisions_index.py` from each file's block,
  grouped by area and ordered by date, and `--check` runs with the PC tests.
  A hand-kept list would be the new place where branches collide, each
  adding a line at the same spot; a generated one is repaired by running
  the tool. The tool also refuses a block with a missing field, an unknown
  area or status, or a summary over 160 characters.
- Fields: `title`, `date`, `by` (owner, agent, owner+agent), `status`
  (decided, to-confirm, open, replaced, idea, reference), `area` (eleven of
  them), `summary`.
- The text was moved, not rewritten. Titles are the old headings without
  their brackets, and where a heading said more than a date the file quotes
  it whole in its first line.

## What was built

- `docs/decisions/`: 106 files. 95 are the old `###` sections; six are the
  old tables and lists (Rejected, Still open, Ideas kept for later, Found on
  the bench, To confirm on the bench, Planned order of work); four are the
  Decided table cut by area (hardware, video, architecture, look); one is
  this decision. A script (not kept) did the cutting from a table of line
  number, file name, area, who, status and summary written by hand, and
  checked that every line of the old file is in some new file: only the old
  four-line preface is not, and the index replaces it.
- `docs/DECISIONS.md`: 26 KB instead of 193 KB, about an eighth to read.
- `tools/decisions_index.py`, and the `decisions_index` test in
  `host/CMakeLists.txt`.
- `CLAUDE.md`, `README.md`, `docs/journal/README.md`, the board-turn skill
  and `docs/handoff/backlog.md` say how to read and how to add.

Tested: the PC tests pass (three tool tests needed `pyserial` installed in
the container first, nothing to do with this change). No firmware changed.

## What was learned

- `who` and `status` were guessed from each heading and its first lines.
  Sections that mix the owner's decision with the agent's details are
  `owner+agent` and carry the status of the main decision; the text says
  which point is whose. Some of these labels will be wrong and are cheap to
  fix when met.
- The text still says "below" and "the section above" in places.

## What is left

- A branch that added a section to the old file will conflict when it
  merges with this. The way out: keep this side's `docs/DECISIONS.md`, put
  the new section in a file of its own in `docs/decisions/` with the block
  of fields, and run `tools/decisions_index.py`.
- "Still open" has rows that were answered long ago, and many `to-confirm`
  decisions have in fact been confirmed by use. A pass with the owner over
  the `to-confirm` list would bring the statuses up to date.
