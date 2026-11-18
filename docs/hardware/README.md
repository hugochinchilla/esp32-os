# Adapter board: VGA, audio and beeper

This is the hand-built board. The manufactured board (the "hat", in
`hardware/hat/` once pull request 2 is merged) is a separate design on the same pin map, so one firmware
serves both. What is soldered on the hand-built board today, what is still
to add and the supply test to run while adding it are in
[perfboard-build.md](perfboard-build.md).

A carrier board on 5x7 cm perfboard (18 x 24 holes). The dev board plugs into
two 1x22 socket strips, and each resistor sits in the row of its own GPIO pin,
so the video part needs no wires. The board also carries the audio output filter, a
beeper and a supply capacitor.

![Board layout](vga-adapter-256.png)

Regenerate the drawing with `python3 tools/gen_vga_adapter_diagram.py` from the
repository root (writes the SVG).

## Pin map

| Signal | GPIO | Resistor | Data bus bit |
| --- | --- | --- | --- |
| Red, top bit | 4 | 510 ohm | D7 |
| Red, middle bit | 5 | 1 k | D6 |
| Red, low bit | 6 | 2 k | D5 |
| Green, top bit | 7 | 510 ohm | D4 |
| Green, middle bit | 15 | 1 k | D3 |
| Green, low bit | 16 | 2 k | D2 |
| Blue, top bit | 17 | 510 ohm | D1 |
| Blue, middle bit | 18 | 1 k | D0 |
| Blue, low bit | 8 | 2 k | copy of D1 |
| HSYNC | 9 | 100 ohm | |
| VSYNC | 10 | 100 ohm | |

The three resistors of one colour join at their far ends and go to the VGA
connector. Peak level into the monitor's 75 ohm input is about 0.68 V.

A pixel is one byte, `RRRGGGBB`, clocked straight onto D7..D0. Blue has only
two bits in the byte, so its third pin repeats blue's top bit. That gives blue
the levels 0, 2/7, 5/7 and 7/7, which keeps white neutral and gives four exact
greys.

## VGA connector (15-pin)

| Connector pin | Signal |
| --- | --- |
| 1 | Red |
| 2 | Green |
| 3 | Blue |
| 13 | HSYNC |
| 14 | VSYNC |
| 6, 7, 8 | Ground: the returns of red, green and blue. In a cable these are the shields of the three thin coaxial wires |
| 10 | Ground: the return of the two sync signals |
| 5 | Ground. The hat uses this pin to sense a monitor instead (pulled up, read on GPIO47); whether the hand-built board follows is open, see [perfboard-build.md](perfboard-build.md) |
| Shell | Ground: the cable's outer braid |

Ground every one of them. With the colour returns left open the picture
still shows, but each colour's current then comes back through whatever
ground it finds, which is the path the radio's current also uses.

### Wire colours of the cut VGA cable in use

Wire colours are not standardised. These were found with a multimeter on the
cable used for this build and apply to that cable only.

| Wire colour | Connector pin | Signal |
| --- | --- | --- |
| Brown | 13 | HSYNC |
| Yellow | 14 | VSYNC |
| White | 10 | Ground (not enough on its own, see below) |
| Shield braid | shell | Ground. Must be soldered to the board's ground: without it the VGA-to-HDMI adapter shares no ground with the board and shows no picture |

## Reserved and spare pins

| Use | GPIO |
| --- | --- |
| USB host data (fixed by the chip) | 19, 20 |
| Debug UART (USB-C socket marked UART) | 43, 44 |
| Clock chip on I2C: SDA, SCL | 1, 2 |
| Audio out to a 3.5 mm jack | 13 |
| Beeper | 14 |
| SD card: CLK, CMD, D0 | 39, 40, 41 |
| USB power switch enable and fault, optional | 21, 42 |
| Spare | 11, 12, 38, 47, 48 (one of 38 or 48 is the on-board LED) |
| Not used: boot strapping pins | 0, 3, 45, 46 |
| Not available: flash and PSRAM | 26 to 37 |

## Before soldering

- Pin names follow the official ESP32-S3-DevKitC-1. Compare them with the
  labels printed on your board.
- The drawing puts the socket strips 10 holes apart. Some boards are 9 apart,
  so test-fit the dev board on the perfboard first.
- A VGA socket's staggered pins do not fit perfboard holes. The drawing ends at
  a pin header; wire it to a solder-cup socket, a breakout or a cut cable.
- 1/4 W resistors are far more than needed (the worst case is about 16 mW).
  Prefer 1% parts.

## Audio output

```
GPIO13 ---[ 1 kΩ ]---+---|(---> AUDIO OUT: jack tip and ring
                     |  + 1 to 10 µF
                  [100 nF]
                     |
GND -----------------+--------> AUDIO GND: jack sleeve
```

The resistor and the 100 nF capacitor smooth the pin's fast switching into an
audio signal. The series capacitor removes the DC offset; if it is
electrolytic, its + leg faces the resistor. The output suits amplified
speakers or a line input, not a bare speaker. A 3.5 mm jack's pins do not fit
perfboard holes, so the board ends at a two-pin header and the jack is wired
to it.

## Beeper

A piezo beeper sits in the top-left corner, between 3V3 and GPIO14, with one
insulated wire under the board to GPIO14. The firmware keeps GPIO14 high when
silent. This direct connection is only for a piezo beeper. A magnetic buzzer or
a 5 V buzzer with built-in oscillator draws too much current for a pin and
needs a transistor.

## Supply capacitor

An electrolytic capacitor of 100 to 470 µF, rated 10 V or more, between 3V3
and ground. It stands in the two empty rows of GPIO3 and GPIO46, in the
middle of the video resistors and away from the antenna: + leg in the GPIO3
row, − leg in the GPIO46 row. One insulated wire under the board takes + up
to the second 3V3 pin, another takes − down to the ground rail. A wide
capacitor stands a little above the neighbouring resistors, or lies on its
side. A 100 nF ceramic capacitor across the same two legs is optional.

It is there to test whether the lines seen on the picture at high Wi-Fi
transmit power come from the 3.3 V supply dipping when the radio transmits
(`docs/DECISIONS.md`, "Found on the bench"). The video voltage is the
3.3 V supply divided by the resistors, so a dip in the supply is a dip in
brightness. The test is described in [perfboard-build.md](perfboard-build.md).

## USB socket for a keyboard or mouse (optional, no firmware yet)

A USB-C breakout board can be wired to the chip's USB pins on the right
socket strip. Nothing in the firmware drives a USB device yet.

| Breakout pin | Goes to |
| --- | --- |
| D− | GPIO19 |
| D+ | GPIO20 |
| VBUS | 5V |
| GND | GND |
| CC1, CC2 | 56 k each to VBUS. Needed only for a device with its own USB-C plug; a USB-C to USB-A adapter works without |

- Keep the two data wires short, the same length and twisted together.
- GPIO19 and GPIO20 are also the data pins of the dev board's socket marked
  USB. Use one or the other, never both at once.
- One device only: the chip has a single port. Two devices need a hub or one
  receiver shared by keyboard and mouse.
- The 5V pin has no current limit of its own here (the hat has a switch for
  that). A 500 mA resettable fuse in the VBUS wire is a cheap precaution.

## Ground

A bare wire runs along the GND row at the bottom left and is soldered at every
hole it passes. The audio ground pin sits on it, the supply capacitor's − leg
is wired to it, and one insulated wire takes ground from it up to the VGA
ground pin. The VGA cable brings five ground wires (three colour returns, the
sync ground and the braid) and the header has one ground pin: twist them
together into that pin, or solder some of them straight onto the bare wire.
