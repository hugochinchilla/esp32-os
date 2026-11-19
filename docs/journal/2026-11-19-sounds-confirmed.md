# The sounds, heard and confirmed

2026-11-19. Backlog items 58 and 59. Nothing built: the record only.

## What was asked

The owner was sent two recordings of what the Linux program plays (a whole
start, then a chime and a shut-down) and asked whether the beep was too
loud or too quiet beside the chime and whether `boot.pcm` and `close.pcm`
were still the ones. He answered in the project chat:

> los sonidos están perfectos

## What it settles

- The emulated beeper's one short beep at 1,000 Hz, at the loudness set by
  judgement (9,000 of 32,767 against a chime that peaks at 29,000).
- `boot.pcm` as the welcome chime and `close.pcm` as the Shut Down melody.

## What is left

Still the agent's choices in `docs/decisions/sound-port.md`, which stays
`to-confirm`: the error beep codes (he has heard only the "all is well"
beep), the knock with message boxes, the silent Restart, and the
recordings being compiled into the PC and browser builds only.
