# Scanlines while Wi-Fi connects, and the 2 dBm decision

## What was asked

The owner saw a disturbance from the first picture after a restart until a
few seconds after the Wi-Fi bars appeared:

> The flickr is more like scanlines than a flickering of colors.

> they may show up once or twice after the bars show up, but then they stop

## What was found

- By eye, back to back: Wi-Fi off at start-up, a steady picture; Wi-Fi on,
  the lines.
- The webcam did not show them: it gives about 30 different frames a second
  and its blur hides single lines. "Not reproduced" was not an answer the
  owner accepted, so the video interrupt itself was measured.
- Instrumentation added to the VGA driver (`firmware/os/main/vga.c`, shown by
  `tools/dbg.py memory`): every bounce-buffer refill against the DMA's 320 us
  cadence, a histogram of lateness, missed deadlines, and a 100 ms timeline
  from start-up to 20 s. With Wi-Fi on, no refill missed its deadline and the
  worst started 12 us late (1 to 2 us with Wi-Fi off), while the owner saw
  the lines. So the picture data was right: the lines came from the radio's
  transmit bursts disturbing the analogue side (the supply, or radiation into
  the resistor DAC, the cable or the VGA-to-HDMI adapter).
- Test: transmit power at 2 dBm instead of 20, the owner watching: no lines.

## Options considered

- Hardware: the antenna away from the VGA board, a ferrite on the cable,
  100 uF and 100 nF decoupling at the DAC, a stronger 5 V supply. Kept as a
  list for later; none needs code.
- Lower transmit power: free, but shorter range.
- Transmit less while joining: go straight to the remembered access point and
  channel instead of scanning every channel first.

## What was chosen and why

Decided by the owner: the radio's top transmit power defaults to the minimum,
2 dBm, adjustable as `tx_power_dbm` in `C:\.config\network.ini` (2 to 20),
with `enabled = false` to leave the radio off. Joining goes straight to the
remembered network's channel and access point (a hint kept in NVS, written
only when it changes), with a full scan only if that fails.

## What was built and tested

`firmware/os/main/wifi.cpp`, `core/src/net/wifi.cpp`, the network settings.
On the board: the radio associates 0.1 s after it starts and has an address
about 1.1 s later (before: 5 s and 6 s after a scan of every channel); at
2 dBm the remote screen still moves about 270 KB/s and a ping loses nothing.
The owner reported no lines after the change.

## What was learned

The camera is not the eye. Measuring inside the interrupt separated "the
picture data is late" from "the analogue signal is disturbed", which no
amount of filming could.

## What is left

The hardware measures above, if the lines ever come back at higher power.
