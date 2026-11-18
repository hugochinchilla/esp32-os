# Transmit power in the Network program

2026-11-18. Backlog item 57. Built and tested on the PC; the firmware part
has not run.

## What was asked

The owner, in the project chat, among nine requests for the backlog:

> add a setting on the network app to modify the tx power of wireless under and "advanced..." button

and later, to start it with two other items: "Vale empieza las 3".

## What was found

- The setting exists: `tx_power_dbm` under `[wifi]` in
  `C:\.config\network.ini`, 2 to 20 dBm, default 20 (`DECISIONS.md`, "Wi-Fi
  transmit power"). The only way to change it was to edit the file.
- Only `firmware/os/main/main.cpp` read it, once, at start, and called
  `esp_wifi_set_max_tx_power` itself. The radio port (`net::WifiPort`) had
  no call for it, so nothing in `core/` could change the power while the
  board ran.
- The same file holds `[telnet]` and `[update]`, so whoever writes the
  power has to keep the rest of the file.
- The Network panel (`core/src/shell/network.cpp`) had four buttons beside
  the list, and room for a fifth above Close.
- There is no slider control. A horizontal scroll bar is one: arrows, a
  thumb to drag, clicks on the track, the wheel.

## The options considered

When the change takes hold:

1. **On OK, at once.** Needs one new call on the radio port,
   `set_tx_power(dbm)`, which the board answers with
   `esp_wifi_set_max_tx_power`. The owner can lower it and see the lines on
   the picture go, or raise it and see the signal come back, without a
   restart.
2. **At the next start.** No change outside the Network program: OK only
   writes the file and the dialog says a restart is needed. A restart takes
   the start-up screens each time, for a setting that is tuned by eye.

The control:

1. **A slider from 2 to 20 in steps of 1 dBm**, with the value written
   above it. The whole range of the file, and the scroll bar already does
   the work.
2. **Three radio buttons**, Low (2), Medium (11), High (20). Simpler to
   read, but the value in between that just stops the lines on a given
   board cannot be chosen, and a value typed in the file would show as none
   of the three.
3. **A typed number.** Needs a message for every wrong value and says
   nothing about the range until one is typed.

## What was chosen

The owner was asked on a card in the thread with the first of each
recommended: a slider, applied on OK. The work went ahead on that
recommendation while the card was open; a later entry says so if he
chooses otherwise.

## What was built

- `net::WifiPort::set_tx_power(int dbm)`, and `net::Manager::set_tx_power`,
  which keeps the value within `net::kTxPowerMin` (2) and
  `net::kTxPowerMax` (20). `net::kTxPowerDefault` is 20. The manager does
  not keep the value: the file does.
- The board's adapter (`firmware/os/main/wifi.cpp`) answers with
  `esp_wifi_set_max_tx_power` and logs `wifi: transmit power N dBm
  (ESP_OK)`, at start and at each change (the line used to come from
  `main.cpp`, as `os: wifi transmit power ...`). `main.cpp` now sets the power
  at start through the same call, so there is one place that talks to the
  driver.
- The Network panel has a fifth button, "Advanced..." (Alt+A). It opens a
  dialog over the panel, part of the Network program (same process row),
  made when asked for and destroyed when closed: "Transmit power: 14 dBm",
  the bar with Low and High under its ends, two lines saying what it is
  for, OK and Cancel. Left and Right move it by 1 dBm, Home and End go to
  2 and 20, wherever the keys are. OK reads `network.ini` as it is,
  changes the one key, saves it, and tells the radio. Cancel and Escape
  change nothing. If the file cannot be written a box says so and the
  radio is left alone.
- The pretend radio on the PC (`host/fake_wifi.hpp`) remembers what it was
  told, for the tests.

Tested on the PC:

- `render_net_scenes`: the dialog opens at 20 dBm with no file; Escape
  after moving it writes nothing and does not touch the radio; OK at 11
  writes `tx_power_dbm = 11`, keeps a key that was already in the file,
  and the radio is told 11; it cannot go below 2; it opens again on the
  kept value; a click on the bar's right arrow gives 12; End gives 20.
  Pictures `net-15` to `net-17`, the last in each of the three other
  themes. The twelve older pictures of the panel changed by the new
  button and nothing else.
- `test_lifecycle`: the panel is one block bigger (14); the dialog is 9
  blocks while open, has no process of its own, and leaves nothing when it
  closes alone or with the panel.
- The whole PC suite and `tools/check_os_calls.py` pass.

Not tested: nothing here has run on the board. See "What is left".

## What was learned

- A scroll bar's page of 1 gives a thumb of 13 pixels on a 264 pixel
  track, and one step per click, notch or key. That is what a slider of 19
  values wants.
- The first draft put the two lines of help in a text area, which draws a
  sunken white box and cut the second line. Two plain lines on the face
  read as help, not as a field.

## What is left

- A turn on the board: the power changes at once and holds, the value
  survives a restart, the saved networks are untouched, and
  `tools/dbg.py misses` stays clean.
- Whether the driver takes a new top power on a link that is already up,
  or only from the next join, is to be seen there.
