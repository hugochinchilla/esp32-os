# Hand-built board: what is fitted, what is next, and the supply test

The layout and every value are in [README.md](README.md). This page is the
state of the one board that exists and the order to change it in.

## State on 2026-11-18

| Part | State |
| --- | --- |
| Video: nine colour resistors, two sync resistors, header to the VGA cable | Fitted, in use |
| VGA ground: pin 10 (white wire) and the cable's braid | Fitted |
| VGA ground: colour returns, pins 6, 7 and 8 | Not connected, and cannot be: the cut cable in use has no wires for them (the owner, 2026-11-18). A cable or socket that has them should ground them |
| Beeper on GPIO14 | To fit |
| Audio output on GPIO13 | To fit |
| Supply capacitor, 3V3 to ground | Fitted 2026-11-18. A permanent part of the board since the test (the owner's decision) |
| USB-C breakout on GPIO19 and GPIO20 | Parts arrived; optional, the firmware has no USB host yet |

## What to solder

1. **Beeper.** A piezo beeper between 3V3 and GPIO14, nothing in series.
   Top-left corner; one insulated wire under the board to GPIO14.
2. **Audio.** GPIO13 through 1 k. From the resistor's far end: 100 nF to
   ground, and 1 to 10 µF in series (+ leg towards the resistor) to a two-pin
   header. Header to the jack: signal to tip and ring, ground to sleeve.
3. **Supply capacitor.** 100 to 470 µF, 10 V or more, in the rows of GPIO3
   and GPIO46. + by wire to the second 3V3 pin, − by wire to the ground rail.
4. **Colour returns.** Find the shields of the three thin coaxial wires in
   the cut cable (pins 6, 7 and 8, by multimeter from the plug) and solder
   them to ground with the white wire and the braid.

Steps 3 and 4 are the two candidate cures for the lines. Fit them last and
one at a time, with a look at the picture in between, or the test cannot tell
them apart.

## The supply test

The question, from the hat's design: do the lines at high Wi-Fi transmit
power come from the 3.3 V supply dipping during a transmit burst (a capacitor
helps), from the radio's current sharing thin ground wires with the video
return (grounded colour returns and a ground plane help), or from both? The
hat already has the ground plane and grounded returns; a second 100 µF
capacitor across 3.3 V next to the dev board is proposed and waits on this
result.

Set-up, the same for every row: same monitor or VGA-to-HDMI adapter, same
power supply and cable, same place relative to the router. In
`C:\.config\network.ini`, `[wifi]`, set `tx_power_dbm = 20` and restart.
Watch from the first picture until about ten seconds after the Wi-Fi bars
appear; that is when the lines showed on 2026-11-16. Look by eye: the webcam
does not show them.

| Step | Board | Lines at 20 dBm | Lines at 2 dBm | Notes |
| --- | --- | --- | --- | --- |
| A | Beeper and audio fitted, no capacitor, returns open | | | The baseline. Lines expected, as on 2026-11-16 |
| B | A plus the supply capacitor | **None** (the owner, 2026-11-18: "ya no veo scanlines por wifi") | | Colour returns still open. To confirm with the owner: that step A showed the lines the same day, and which capacitor is fitted (470 µF 10 V and a 100 nF disc were the ones picked from his kits) |
| C | B plus the colour returns grounded | Not possible with this cable: it has no return wires | | |
| D (optional) | C with the capacitor lifted or cut out | | | Tells whether the returns alone are enough |

For each cell write none, fewer or fainter, or the same. If B leaves fainter
lines, try a larger capacitor or a second one; if it changes nothing, a
capacitor from 5V to ground is a quick extra try (the dip may be on the USB
cable's side of the regulator).

### Result so far (2026-11-18)

With the capacitor fitted and the colour returns still open, the owner no
longer sees the lines. That points at the 3.3 V supply dipping during
transmit bursts as the cause, or at least the main one, and says the hat
should carry a capacitor on 3.3 V too. It is one look by eye on
one board with one monitor; the details marked "to confirm" in the table are
not yet confirmed. Step C cannot be run: the cable has no wires for the
colour returns.

Later the same day the owner reported that Wi-Fi at 20 dBm works well, and
decided that the capacitor goes on the board for good, on the hand-built
board and on the hat. Whether the firmware's 2 dBm default rises is still
open.

What each outcome means for the hat:

- B cures or clearly reduces them: the hat gets a capacitor on 3.3 V.
- Only C cures them: the hat's ground plane and returns already cover it and
  the extra capacitor is not needed.
- Neither: the cause is elsewhere (radiation into the cable or the HDMI
  adapter). Next tries are a clip-on ferrite on the VGA cable and small
  capacitors on the three colour outputs, for which the hat can carry empty
  footprints.
- If 20 dBm comes out clean, the 2 dBm default in "Wi-Fi transmit power"
  (`docs/DECISIONS.md`) can be looked at again.

After the test set `tx_power_dbm` back to 2, or remove the line.

## Open: VGA pin 5

On the hat, pin 5 is not ground: it is pulled up to 3.3 V and read on
GPIO47, and a monitor pulls it low. No firmware reads GPIO47 yet. When it
does, the hand-built board needs one of these so that the same firmware
works:

| Option | Cost |
| --- | --- |
| Wire pin 5 to GPIO47 with a 10 k pull-up to 3V3, as the hat (suggested once the firmware reads it) | One wire across to the right socket strip and one resistor |
| Tie GPIO47 to ground: "a monitor is always there" | One wire; no detection |
| Leave it, and have the firmware treat "no monitor" as "unknown" | Nothing on the board; a rule in the firmware |

Until then pin 5 stays as it is. The owner decides.
