# The backlog caught up with what is merged

2026-11-19. Documents only: nothing was built, compiled or run.

## What was asked

The owner, 2026-11-19:

> actualiza el backlog primero, despues arranca con 51, 56, y el fallo de
> dbg_tool.
>
> Añade y comienza también una tarea para que los juegos tengan sonido.
>
> chess -> sonido al mover una pieza y sonido especiál para jaque, y jaque
> mate
> serpiente -> sonido de comer fruta y de game over
> buscaminas -> sin sonidos
>
> La papelera al vaciarse debe hacer un sonido como de papel siendo arrugado

This entry is the first sentence, and the new item written down. The work
itself (items 51, 56, 69 and 70) is in threads of their own.

## What was found

`docs/BACKLOG.md` had fallen behind `main`. Each branch wrote its own status
line while it was being built ("on a branch", "not yet on the board"), and
the merge after the board check did not always come back to it. Compared
with the merge commits on `main`, the board results in the journal and the
list of pull requests:

- Sixteen items said less than was true: 47 (themes), 49, 50, 52, 54, 55,
  57, 59, 61, 62, 64 and 66 were merged and checked on the board and still
  read as waiting; 26 and 39 still listed a board check that has since
  passed or a change that has since been merged; 33 said "not published
  yet" with the Pages workflow turned on since 2026-11-17; 53 said the page
  "still waits for the relay", which the owner turned on on 2026-11-18.
- 58, 65 and 67 had the right facts and the wrong first word ("built on a
  branch", "measured", "merged").
- Two items were numbered 47: the window themes and the `dbg_tool` test that
  fails now and then.
- Item 48, the beeper, was named in item 58's text and had no row.
- The table was cut in two. Two paragraphs about items 14 and 1 to 13 sat in
  the middle of it, so on GitHub every row from item 62 down was drawn as
  running text and not as a table.

## What was done

- Each of those status cells now starts with what is true, with the pull
  request and the commit the board ran. The long histories of items 26 and
  27 are kept, behind a first sentence that says where the item stands.
- The second 47 is 69.
- Item 48 has a row: parked, PR 51 open, the handoff note on its branch.
- Item 70 is new, in the owner's words above: sounds for Chess (a move,
  check, checkmate) and Culebra (the fruit, game over), none for Cactus
  Patch, crumpled paper when the Trash is emptied.
- Items 51, 56, 69 and 70 are `doing`. Their branches are not named: the
  threads had just been started and are to write the branch in themselves.
- The two paragraphs are under the table, which is one table again.
- The head of the file says that numbers are not reused and which is the
  next free one (71), since two items got the same number once and three
  others were renumbered on the way to `main` (66, 68, and 52 which was 47).

No option was put to the owner: nothing here is a design choice.

## What was not checked

Statuses come from the record (merge commits, the journal's "On the board"
sections, the pull request list), not from running anything. Two things the
record leaves open and the backlog now says so:

- Item 39: that init no longer reads the old lines was merged (PR 32) after
  the board check of the item; no board turn was run for that change alone.
- Item 59: the owner's confirmation of the two recordings is in PR 73,
  which is open.

Pull requests 18 (the message queue, item 8, done another way) and 60 (item
57, merged by hand) still show as open on GitHub.

## What is left

Items 51, 56, 69 and 70 in their threads. Still `todo`: item 60, the Shut
Down sequence. In part: 27, 37 and 46.
