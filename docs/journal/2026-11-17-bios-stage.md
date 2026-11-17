# A BIOS before the OS: the discussion so far

2026-11-16 and 2026-11-17. A conversation, not a build. Nothing here has been
compiled or run. Two things are decided (the font, and that the BIOS gets its
own partition once a restart has been measured on the board); the rest is
open and listed at the end. Backlog item 46.

## What was asked

The owner, 2026-11-16:

> Y abre otro hilo en el que quiero poder conversar sobre la idea de que el
> self-test del arranque y el splash screen sí deberían ser como etapas
> separadas, como en un PC, ¿no? Que está la EFI, o el UEFI, perdón, y lo que
> antes era la BIOS. Porque tal y como estamos ahora planteándolo, parece que
> init levantaría el proceso de self-test, y a mí me parece que estaría muy
> divertido si pudiéramos hacer que hubiera primero una partición con el
> código estilo BIOS que hace el self-test. Incluso algún día puede que
> llegue a tener un menú de configuración para establecer la hora de sistema
> y cosas similares, y que luego dé arranque a la partición del sistema
> operativo, y ahí es donde se inicia el proceso init, que ya sí que sería el
> encargado de mostrar el splash, si lo hay.

Then, 2026-11-17, after the first answer:

> The BIOS could have its own serial link with a text mode instead of the
> full debug mide the os has. It would also allow to do things as the servers
> with oob management do

> With a true test mode can we have a logo con top?

> Yes, do a mockup, show me fonts

> Spleen

## What was found

- **How the chip starts.** The ROM in the chip loads Espressif's small
  bootloader, which reads the partition table and `otadata` and starts one of
  the two app slots. That app does everything: the crash screen, the
  self-test, the splash, the desktop (`core/src/shell/boot.cpp`,
  `firmware/os/main/main.cpp`).
- **The flash today** (`firmware/os/partitions.csv`, since the updates over
  Wi-Fi of 2026-11-17): `nvs` 24 KB, `phy_init`, `ota_0` and `ota_1` of
  1.94 MB each, `otadata`, then C: on the 12 MB from 0x400000. The image is
  1.38 MB. The first answer in this conversation was written against the
  older table (one 3.94 MB app) and said the BIOS would use "the room kept
  for a second update slot"; that room is now in use, so the BIOS has to be
  carved out of the two slots (see Costs).
- **The monitor sets the pace.** It needs about 2.4 s after the video signal
  starts before it shows a picture, 5.4 s if the screen is black
  (`2026-11-16-boot-screens.md`).
- **The one-shot fast start** is a word in NVS, not RTC memory: RTC no-init
  memory survives a software restart, but the reset after flashing cleared it
  on this board (DECISIONS, "One-shot fast start"). The first answer
  suggested keeping it in RTC memory; that was wrong for the post-flash case.
- **The crash record** is 188 bytes of RTC no-init memory, shown as an error
  screen before the self-test.

## The options

**Where the BIOS lives.**

| | What it is | Cost | Verdict |
| --- | --- | --- | --- |
| A | One image, two logical stages: the code is reorganised so a "firmware stage" runs before init | Small. Nothing is really separate | The fallback |
| B | A BIOS app in its own partition: video, self-test, later Setup. It ends by restarting into the OS | A second firmware to build; flash taken from the update slots; a blank on the monitor at the handover | Recommended |
| C | The BIOS inside Espressif's bootloader | About 32 KB of room, no FreeRTOS, no drivers, no PSRAM: the video driver would have to be written again from nothing | Not realistic |

A jump from one app into another without a restart is not something ESP-IDF
offers: the handover is a restart.

**How B picks what to start.**

1. *The standard mechanism*: the BIOS marks the OS as the boot partition and
   restarts; the OS marks it back. Two flash writes per start, it fights with
   the update system's own use of `otadata`, and an OS that dies early leaves
   the BIOS skipped.
2. *Recommended: a hook of about 20 lines in the bootloader* that reads a
   flag in RTC memory. No flag (power-on, reset button, the reset after
   flashing): start the BIOS. Flag set by the BIOS just before its restart:
   start the OS, by the usual `otadata` choice, so updates and rollback work
   as they do now. An empty or broken BIOS partition falls through to the OS.
   From memory of ESP-IDF, to be checked against v5.5.

**Costs of B.**

- *Flash.* The 3.94 MB app area has to hold three things. For example a BIOS
  of 384 KB and two slots of 1.75 MB (the 1.38 MB image would have 21 % of a
  slot free instead of 29 %); or a bigger BIOS and one OS slot, giving up the
  way back after a bad update. C: does not move in either case. The BIOS has
  no Wi-Fi, which is most of the OS's size (556 KB before Wi-Fi, with the
  whole desktop), so 384 KB is a guess that has to be measured.
- *RAM.* None while the OS runs: the BIOS is gone after the restart.
- *Time.* A second pass through the ROM, the bootloader and the start of an
  app: perhaps half a second, to be measured. The larger cost is the video. A
  restart almost certainly stops the VGA signal, so the monitor goes blank
  for its 2.4 s between the BIOS and the OS, as a 90s monitor did going from
  text mode to graphics. The splash then has a real job: init shows it at
  once so the monitor locks on something that is not black.
- *Work.* Two firmwares; the video and serial adapters move to a component
  both use; the first flash with the new table is by cable and whole.

**Settings.** NVS plays the CMOS memory: both stages read it. The BIOS's keys
in `boot.ini` (`monitor_wait_ms`, `selftest_pace`, `selftest_hold_s`,
`fastboot`) would move there; `splash_ms` belongs to the OS and stays. The
alternative is LittleFS in the BIOS so it can read `boot.ini`: more code and
a mount in the BIOS. The clock: the chip has no battery, so a time set in
Setup should survive a restart but not a power cut, a dead CMOS battery at
every power-on, until the clock chip already suggested in DECISIONS (DS3231)
is fitted.

**Fast start.** A restart asked for by the OS (Shut Down, Restart) can leave
the RTC flag set, so the BIOS is skipped: the warm boot of a PC, where
Ctrl+Alt+Del left a mark in memory and the BIOS skipped the memory count. The
one-shot flag for the agents stays in NVS, because it has to survive the
reset after flashing; the BIOS reads it and hands over at once without
starting the video.

**The serial port (the owner's idea).** The BIOS speaks plain text on the
serial port instead of the OS's framed debug protocol, what servers call
console redirection. It fits what is decided for the OS, where anything on
the wire that is not a debug frame is console text. Agents read lines instead
of screenshots; the BIOS's picture is then checked by the PC tests and the
webcam only. On out-of-band management, the honest limit: a server has a
second small computer that stays alive beside the OS; here there is one chip
and the BIOS is gone once the OS runs. It is a management console before the
OS, not beside it. What that still gives:

- a remote power button (the USB serial adapter already resets the board; a
  key in the first moments stops in Setup);
- Setup over serial: the clock, the countdown, what to start, "start this
  once";
- an event log: why the last restart happened, from the crash record;
- a boot loop guard: after, say, three starts that end in a watchdog or a
  crash, stop in the BIOS and wait;
- an OS image received over serial and written to an OS slot, with no
  flashing tool. The largest piece; it can come last.

The watchdog (backlog item 7) is what brings a hung OS back to the BIOS.

**The screen: the approved drawing, or a true text screen.** The approved
self-test (`docs/look/boot/boot-b-self-test.png`) is drawn in Spleen 6x12 at
free positions. A true text screen is a grid of 8x16 characters, 80 by 30 on
640x480 (the classic 80 by 25 would leave black bands), which the serial port
can mirror exactly. A logo is possible on it, as Award's BIOS had: either as a
picture over cells left empty (simple, full colour; recommended) or built
from custom characters as Award did (two colours a cell, more work for the
same picture). On serial a line of text stands where the logo is.

**The font** for the text screen. No IBM or Microsoft font may be used. Five
free ones were drawn (`docs/look/bios-text/`, made by
`tools/mockup/bios_text.py`):

| | Font | Licence | Character |
| --- | --- | --- | --- |
| A | Spleen 8x16 | BSD | Already in the repo, same family as the desktop font |
| B | Terminus 8x16 | SIL OFL | The classic Linux console font, clean and thin |
| C | Unscii 16 | Public domain | Drawn to recall 80s and 90s computer text, heavy strokes |
| D | Tamzen 8x16 | Free licence | Slim, modern terminal look |
| E | GNU Unifont | SIL OFL | Plain and neutral |

## What was chosen

- **The font is Spleen 8x16**, by the owner. The agent had recommended
  Unscii, for looking most like a PC BIOS and for making the BIOS look unlike
  the OS; the owner preferred the family the whole system already uses. The
  file is already in `third_party/fonts/spleen/` and the firmware
  (`core/src/gfx/font_spleen8x16.cpp`), so it costs nothing new.
- Choosing a font for the text screen is read as choosing **the text screen
  with the mascot as a picture on top** over the earlier drawing. This is the
  agent's reading, to be confirmed.
- **The BIOS gets its own partition (option B), after measuring first**, by
  the owner, on a card with three answers ("Own partition, measure first",
  "One image, two stages", "Keep talking"). Not chosen: one image with two
  logical stages, because nothing would really be separate, which was the
  point of the idea; the bootloader, for the reasons in the table. The
  measurement comes first because the two unknowns (what a restart does to
  the monitor, how long a second start takes) decide how the handover should
  look, and measuring them needs no change to the flash.

## How the PC maps onto this

The honest mapping: the chip's ROM and Espressif's bootloader are the true
BIOS and boot sector (the bootloader reads a partition table and picks the
active one, as the boot sector did), and they cannot be decorated. Our BIOS
app is closer to UEFI: a small system with its own video and setup screen
that hands over and disappears. NVS is the CMOS memory, the RTC flag is the
warm-boot mark, and the restart is the moment the firmware lets go.

## What was learned

- The first answer was written from a checkout a day old. The flash layout
  and the one-shot flag had both changed on `main` in between; the answer was
  corrected here. Read `main` again before answering about it.

## What is left

Open, for the owner:

1. Decided: B. How it picks what to start (the bootloader hook is
   recommended) waits for the check against ESP-IDF v5.5.
2. How the app area is split for B: a small BIOS and two smaller update
   slots (recommended), or a larger BIOS and one OS slot.
3. Whether the blank of about 2.4 s at the handover is acceptable, with the
   splash covering it.
4. Whether the BIOS's settings move from `boot.ini` to NVS. Recommended: yes.
5. Whether the OS image over serial is in the plan as a later step.
   Recommended: yes.
6. Who shows the crash screen once there is a BIOS: the BIOS, as its event
   log (recommended), or the OS as today.

To measure first, in one turn on the board and with no change to the flash:
what a plain restart does to the monitor, and how long the second start
takes. Then check against ESP-IDF v5.5 that the bootloader hook can choose
the partition from an RTC flag.
