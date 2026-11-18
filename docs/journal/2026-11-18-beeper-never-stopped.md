# The beeper that never stopped

2026-11-18, the first board turn of PR 51 (the beeper, backlog item 48).

## What happened

The owner: "the bepper sounded on post, just never stopped sounding", and
"it produces scanlines while beeping hard (not a problem for a short
beep)". The board thread: the self-test showed "Detecting sound .......
piezo beeper" at 8.7 s, the BIOS handed over at 13.94 s, the OS started
normally with no missed refills. Neither firmware logged anything about the
beeper. Not known: whether the sound went on after the desktop appeared,
whether the chime played.

Asked, the owner added: "the previous boot sounded during the whole bios
screen, and even during the reboot to the OS, and while the OS was on
screen the beep keep going". So the sound did not begin with the beep at
the end of the self-test: it was there while the BIOS's screens were, which
is from about when the driver took the pin and held it at its "silent"
level.

## What was found, reading the code

Not one cause that is certain, since the cloud has no board. Three faults:

1. **The OS never started the beeper's driver.** `beeper::begin()` was
   called in the BIOS only, so in the OS `beeper::play` did nothing: no
   chime, and nothing that would silence a pin the BIOS left sounding.
2. **Silence was "the pin held high"**, right only for a passive piezo
   disc wired to 3V3. A restart from the BIOS into the OS is a reset of
   the processors, which does not reset the PWM or the pin: whatever the
   pin was doing goes on into the OS.
3. **Nothing ended a tone if the step timer did not fire**, which is what
   the board thread suspects in the BIOS. Reading ESP-IDF's `esp_timer.c`
   gives no reason for it not to fire, so this one is not explained.

A beeper that sounds for ever with a steady level on its pin, loud enough
to put lines on the picture, is also what a buzzer with its own oscillator
does (an "active" buzzer), not a bare piezo disc, which is silent on a
steady level. The owner is asked which part it is: with an active one the
pitch is the part's own and the chime cannot be played on it.

## What was changed

- Silent is now the pin let go (output disabled), not held high: nothing
  flows through the beeper however it is wired and whatever it is. A tone
  gives the pin back to the PWM.
- Both firmwares start the driver first thing, so a pin left sounding by
  the last session is silenced at the next start; a shutdown handler lets
  go of the pin before a restart.
- `shell::let_beep_end` tells the beeper to stop when the tune's time is
  up, timer or no timer; a timer that cannot be started silences at once
  and says so.
- The driver logs "beeper: GPIO14: ready", and the OS a line per tune.

Tested: PC tests; both firmwares compile and link. Not run.

## What is left

The second board turn; the owner's answer about the part; the lines on
the picture during a long chime (a resistor in series with the beeper, or
a quieter chime, if they show).
