# The hand-built board gets its own plan

2026-11-18. Documents and a drawing only; nothing here was soldered or run.

## What was asked

> Voy a desconectar la placa y hacer algunos cambios para añadir el beeper y
> la salida de jack, tambien el condensador mencionado en el hilo de diseño
> de la PCB. [...] quiero separar la versión producida de la versión casera
> con perfboard. Recuerdame todos los cambios pendientes a la perfboard,
> ahora solo tiene la salida VGA

And a few minutes later:

> me han llegado las piezas de usb c connection breakout, es posible con eso
> tener conexión para teclado y/o ratón?

## What was found

- `docs/hardware/README.md` and its drawing already described the beeper
  (3V3 to GPIO14) and the audio filter (GPIO13, 1 k, 100 nF, 1 to 10 µF),
  but the board as built has only the video part.
- The board's VGA cable has pin 10 and the braid grounded. The colour
  returns, pins 6, 7 and 8, are open; the README's table said they were
  ground without saying that they were not wired.
- The hat's design thread (2026-11-17) gave two likely causes for the lines
  at high Wi-Fi transmit power, by reasoning and not by measurement: the
  radio's current sharing thin ground wires with the video return, and the
  3.3 V supply dipping during a burst. It proposed a second 100 µF capacitor
  on the hat and a test on the perfboard first: 100 to 470 µF from 3V3 to
  ground.
- The firmware has `tx_power_dbm` in `network.ini` (2 to 20, default 2), so
  the test needs no new code.
- The hat no longer grounds VGA pin 5; it reads it on GPIO47 to sense a
  monitor. No firmware reads it yet.

## Options considered

Where the capacitor goes on the perfboard. Its place matters little
electrically (a few centimetres of wire are nothing against the capacitor's
own resistance at the speed of a transmit burst), so it was chosen by fit:

| Place | For | Against |
| --- | --- | --- |
| Top-right corner, on the GND pin beside the antenna end | − leg lands on a ground pin | The can does not fit beside the socket strip and the dev board's edge; it stands next to the antenna; its + hole neighbours the TX pin |
| Top-left corner, left of the beeper | + is 13 mm from 3V3 | Crowds the beeper; the ground wire is over 5 cm; close to the antenna end |
| **The empty rows of GPIO3 and GPIO46** | In the middle of the video resistors, far from the antenna, 2 cm from the ground rail the video return uses | A can wider than 6 mm overlaps the resistors on either side and must stand above them or lie down |

How to run the test:

| Way | For | Against |
| --- | --- | --- |
| Solder everything, then look | One session | If the lines go, nobody knows whether the capacitor or the grounded returns did it, and the hat's question stays open |
| **Capacitor and colour returns last, one at a time, looking in between** | Separates the two causes | Two more restarts |

## What was chosen

The third place and the test in steps, both as recommendations in the
documents; the owner solders and may do otherwise. The owner's own decision
in this entry is the separation: the perfboard and the hat are two designs
with their own documents on one pin map (`docs/DECISIONS.md`).

VGA pin 5 was not decided: it is recorded as open with three options, to be
settled when the firmware reads GPIO47.

## What was built

- `docs/hardware/perfboard-build.md`: the state of the board, the four
  things to solder, the test table to fill in, what each outcome means for
  the hat, and the open question of pin 5.
- `docs/hardware/README.md`: the ground pins one by one, the supply
  capacitor, and an optional USB-C socket on GPIO19 and GPIO20.
- The drawing (`tools/gen_vga_adapter_diagram.py`) shows the capacitor and
  its two wires. The PNG was rendered from the SVG in a browser and looked
  at.

Not tested: no code changed, so the PC tests were not run; nothing was
flashed.

## The USB-C breakout

Yes for the hardware: D− to GPIO19, D+ to GPIO20, VBUS to the 5V pin, ground
to ground. Not yet for use: the firmware has no USB host. One device without
a hub; the same pins as the dev board's USB socket, so not both at once;
56 k from each CC pin to VBUS only matters for devices with their own USB-C
plug.

## What is left

- The owner fills in the test table; the result goes to the hat's design.
- USB host in the firmware (a backlog item of its own when it is taken up).
- VGA pin 5 on this board, when the firmware reads the monitor sense.

## Later the same morning: the first result

About two hours after the plan, the owner wrote in the project chat:

> puedo confirmar que ya no veo scanlines por wifi

As passed on with it: the capacitor is fitted, the radio at 20 dBm, the
colour returns not yet grounded. So the capacitor alone did it, which is the
supply-dip explanation from the hat's design thread. The parts picked from
his kits an hour earlier were a 470 µF 10 V electrolytic and a "104" disc
that measured 50 nF on his meter (cheap discs run far from their marking;
it does not matter here). Which of them is on the board, and whether the
lines were seen that day before the capacitor went in, is still to confirm.
The result is in the table in `docs/hardware/perfboard-build.md`.
