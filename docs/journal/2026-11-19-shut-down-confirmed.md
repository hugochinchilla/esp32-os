# The Shut Down sequence, confirmed

2026-11-19. Backlog item 60, PR 82. No code changed.

## What was asked

The six choices made while building the sequence
([the entry](2026-11-19-shut-down-sequence.md), section 3) were put to the
owner with a recording of the fade, and again after the board check of
batch 21 with one thing the check had shown: when Cancel calls Shut Down
off, the programs that had nothing to ask have closed already. His answer:

> Parece que está todo bien así. Mira, me gusta lo del fundido del apagado.
> ¿Podríamos replicarlo para el encendido también? Podríamos leer el color
> de fondo del escritorio y presentar primero ese color y luego hacer un
> fundido del escritorio cargado, de la misma manera que hacemos el fundido
> a la pantalla de apagado.

## What was done

- `docs/decisions/shut-down-sequence.md` is `decided`: the 20 seconds from
  the moment Shut Down is chosen, Cancel calling it off, the fade of colour
  in place, the second fade to black, the whole drawing on black, the
  melody with the fade.
- What the board showed (batch 21, the coordinator's report): about 1.4 s
  from Enter to the last screen with nothing open; a step of the fade
  takes about 20 ms at 640 x 480 and 31 to 38 ms at 800 x 600, and steps
  still come every 60 ms; no refill missed; a question answered No goes on
  by itself; one nobody answers is ended and the last screen is up by 26 s;
  the remote screen ends on the last screen; the firmware grew by 2.4 KB.

## What is left

The fade at start-up is a new request, for a thread of its own: the
desktop's colour first, then the loaded desktop fading in out of it, with
the steps of `Wm::fade_step` run the other way.
