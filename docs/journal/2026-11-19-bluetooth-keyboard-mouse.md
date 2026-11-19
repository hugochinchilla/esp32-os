# Bluetooth keyboard and mouse

2026-11-19. Backlog item 51. Decision file:
`docs/decisions/bluetooth-keyboard-and-mouse.md`.

## What was asked

Item 51 was added on 2026-11-18 at the owner's request: a Bluetooth keyboard
and a Bluetooth mouse, both connected at the same time, beside the USB ones.
On 2026-11-19 he said to start it:

> actualiza el backlog primero, despues arranca con 51, 56, y el fallo de dbg_tool.

## What was found

- The backlog says "beside the USB ones", but there are none: the firmware
  has no USB host (`main.cpp` answers "serial debug link" when asked what
  the input is). Keys and the mouse reach the desktop from the debug link
  and from the remote screen only. Bluetooth is the first real keyboard and
  mouse of the board.
- Everything that brings input ends in one port, `ports::InputSink`
  (`key(usage, pressed)` with USB HID usage codes, `mouse(event)` with a
  movement, buttons, the wheel and the wheel sideways). The window manager
  takes it on the UI task only; another task posts messages
  (`msg::PostedInput`, made for the remote screen). The window manager
  repeats a held key itself, which a HID keyboard does not do.
- The ESP32-S3 has Bluetooth LE only. A keyboard or mouse over Bluetooth LE
  is a "HID over GATT" device: it offers the HID service (0x1812) with a
  report map (the same report descriptor a USB device has), one
  characteristic per report, each with a descriptor that says its number
  and whether it is input, and it wants the link encrypted before it sends
  a key.
- ESP-IDF v5.5 with Wi-Fi and Bluetooth both on shares the one radio by
  time ("software coexistence", on by default). I first told the owner that
  ESP-IDF forces Wi-Fi's power saving on in that case, from memory of older
  versions, where `esp_wifi_set_ps(WIFI_PS_NONE)` with Bluetooth on ended
  in an abort. That is wrong for v5.5: the message is not in its Wi-Fi
  library any more, and `docs/en/api-guides/wifi.rst` says Wi-Fi is simply
  only active in its own slice of the time whatever the setting. The
  message to the owner was corrected. What is left in the library is the
  rule that a Wi-Fi scan uses the default scan times; ours does.
- The app slot is 2,031,616 bytes (`ota_0`, `ota_1`). The firmware was
  about 1.74 MB before.

## The options, and what was chosen

All four were put to the owner with a recommendation; the work went on with
the recommended one, recorded as `to-confirm`.

1. **Pairing the first device.** With no other keyboard or mouse there is
   nothing to press "Pair" with.
   - Chosen: the board pairs by itself a device in pairing mode when it has
     no keyboard (or no mouse) connected, and a window to list, pair and
     forget comes later.
   - Only when asked: nothing surprising ever happens, but the first device
     needs a PC.
   - By itself only for some minutes after power-on: safer against a
     neighbour's device, but a restart is needed for every new device.
2. **A switch.** `[bluetooth] enabled`, on by default; off costs nothing.
   The alternative, always on, leaves no way back to a Wi-Fi that has the
   radio to itself if the remote screen suffers.
3. **No code** at pairing. The alternative, a code on the screen typed on
   the new keyboard, was not worth its work here.
4. **The report layout is read from the device** and not the "boot" layout
   (a fixed 8-byte keyboard report and 3-byte mouse report), because the
   boot mouse has no wheel (item 61 has just added it) and many Bluetooth
   mice do not offer boot mode at all. This one was not asked: it changes
   nothing the owner sees except that more devices work.

## What was built

Three layers, the first two portable.

- `core/input/hid.hpp`: `hid::Layout` reads a report descriptor and keeps
  the stretches that matter: key bits (the modifiers; keyboards that report
  every key as a bit), key arrays (the usual six keys held), mouse buttons,
  X, Y, the wheel and the wheel sideways (the consumer usage "AC Pan").
  `hid::Decoder` follows the reports: keys that went down or up since the
  last one, in an order that types a capital when Shift and a letter arrive
  together (modifiers down first, up last), nothing at all for a report
  that says "too many keys", and everything let go when the device
  disappears.
- `core/input/bluetooth.hpp`: the port an adapter implements (listen,
  what was heard, connect, disconnect, who is connected, forget) and
  `bt::Manager`, which decides: a remembered device that advertises is
  connected; a new one only when pairing by itself is on, it is in pairing
  mode, it says it is a keyboard or mouse, and that kind is missing. One
  attempt at a time, given up after 30 s; a device whose attempt failed is
  left alone for 30 s, then 60, up to 8 minutes. It listens only while
  something is missing or somebody is looking at the list. Remembered
  devices are lines in `C:\.config\bluetooth.ini`.
- `firmware/os/main/bluetooth.cpp`: NimBLE as a central. It listens 30 ms
  in every 100 (a guess, to leave Wi-Fi most of the radio), connects,
  starts security (pairing or the saved keys), finds the HID service, its
  characteristics and their descriptors, reads the report map, turns
  notifications on, and from then on hands each report to the decoder. If
  the keys no longer match (the device was paired again elsewhere) ours
  are deleted and the next attempt pairs afresh. The stack takes its own
  memory from external RAM and its task is on core 0 with the network; the
  adapter's tables (about 6 KB) are allocated in external RAM when
  Bluetooth starts.
- Reports arrive on NimBLE's task. A mouse sends about 125 a second and
  the UI task's queue holds 32 messages, so movements are added up in the
  adapter and fetched by one message at a time; a key or a button change is
  a message of its own, waited for up to 250 ms. A desktop busy for a
  moment gets one long movement afterwards instead of a full queue.
- `shell::set_bluetooth` and a timer of the desktop drive the manager four
  times a second, like the Wi-Fi manager.
- The debug link: request 0x21 and `tools/dbg.py bt` (`status`, `scan`,
  `pair ADDRESS`, `forget ADDRESS`, `auto on|off`). The simulator has a
  pretend radio with a keyboard, a mouse and a thermometer in the air.
- `fs::Ini::set` put a new value right after the `=` when the old value
  was empty and kept the old spaces behind it ("1 =value  "). Fixed: one
  space after the sign.

## How it was tested

- PC: `host/tests/test_input.cpp`. Four report descriptors (the standard
  keyboard, a keyboard with a bit per key, a mouse with report numbers,
  twelve-bit X and Y, wheel and pan, and the plainest three-byte mouse),
  the order of keys, the rollover report, short reports, every cut of a
  descriptor under the sanitizers; addresses as text; the manager with a
  pretend radio: pairing by itself, one at a time, not taking a second
  keyboard, three connections, coming back after sleep, failures and
  their waits, the 30 s limit, forget, the settings file kept across a
  restart and beside another program's lines, the ninth device. The debug
  tool's test pairs and forgets through the simulator. All PC tests pass.
- Firmware: compiled and linked in the cloud with ESP-IDF v5.5,
  `esp32-os.bin` 1,943,520 bytes. **It has not run.** Nothing here has
  talked to a real keyboard or mouse: the NimBLE adapter is written from
  the documentation and the examples, and its first run is the board turn.

## What was learned

- Bluetooth costs about 200 KB of flash, and the app slot is now 96% full:
  88 KB are left of 2,031,616 bytes. The next feature of any size needs
  either bigger slots (a new partition table: everything is flashed again
  and C: shrinks by what the two slots gain) or something trimmed. This is
  the owner's to decide and is not part of this change.
- Check the version before repeating what a library "always did": the
  Wi-Fi power-saving rule had gone.

## What is left

- The board turn: internal RAM with Bluetooth on (estimated 50 to 100 KB,
  never measured), the stack of NimBLE's task, missed refills and lines on
  the picture with Wi-Fi and Bluetooth both busy, the remote screen's lag,
  and above all whether a real keyboard and a real mouse pair and work.
- The Bluetooth program: a window with the devices, Pair, Forget and the
  two switches. After the board turn.
- Not done: a code at pairing, the battery level of a device, the lights of
  a keyboard (Caps Lock), keyboards of other layouts than US (as for every
  input today), turning Bluetooth on and off without a restart.

## Later the same day

The owner answered the first choice: pairing by itself when a keyboard or a
mouse is missing, with a window for the rest. The switch and pairing with
no code are still his to confirm.

## The first run on the board (batch 19, part A)

Commit 72c018e, no device tried yet (that part waits for the owner with a
keyboard or mouse). It started in 73 ms with no restarts, heard seven
devices in the room, and both switches worked. No missed refills. The
benchmark had 37 pieces over budget, as main has.

What it costs, measured:

| | main | Bluetooth listening | `enabled = false` |
| --- | --- | --- | --- |
| Free internal RAM | 189,147 | 130,971 (58 KB less) | 21 KB less than main |
| Largest free internal block | 114,688 | 57,344 | |
| Free external RAM | | 204 KB less | 170 KB less |
| After the benchmark: free internal, lowest, largest block | 148,331, , 73,728 | 90,179, 49,452, 31,744 | |

- The backlog's estimate was 50 to 100 KB of internal RAM: it is 58.
- **The switch gives back 37 of the 58 KB, not all.** The link map says
  why: the Bluetooth controller's library keeps 12.4 KB of its code in
  internal RAM whether it runs or not (interrupt code; ESP-IDF offers no
  setting for it on the ESP32-S3), and it, the radio's and the
  coexistence libraries add a few KB of static data. Only a firmware
  built without Bluetooth gets those back.
- The external RAM is the firmware itself: its code runs from external RAM
  (`CONFIG_SPIRAM_XIP_FROM_PSRAM`), so 200 KB more program is 200 KB less
  external RAM, on or off.
- The stack of NimBLE's task had 1,300 bytes left of 4,096 before any
  device had connected. Too little: 6,144 now.
- A full update of the remote screen took 3.3 to 3.7 s against 2.0 s on
  main, with nothing paired, so with the radio listening 30 ms in every
  100 all the time. Listening is now of two kinds: thinly in the
  background (20 ms in every 200), and as before only while a list is on
  show or `dbg.py bt scan` runs. With a keyboard and a mouse both
  connected it does not listen at all.
- Trimmed from the controller, none of it used by a host of keyboards:
  room for two of its six connections or advertisers (828 bytes each), the
  list of devices already reported, advertising, the radio test mode; and
  the names of NimBLE's errors as text. The firmware is 9.8 KB smaller for
  it (1,933,712 bytes on the same base). How much internal RAM it gives
  back is for the next turn to measure; I expect a few KB, not tens.
- The laptop's build of 72c018e with main merged was 1,975,648 bytes,
  97.2% of the slot; the cloud's build of the same commit was 1,943,520
  (main had grown in between, and the two builds differ). After the trim
  and with main of 01:10 merged the cloud's build is 1,937,600 bytes,
  compiled and linked, not run.

## The second run (batch 20, part A again)

Commit e63159e over main, still with no device: the owner has not yet said
whether he has a Bluetooth LE keyboard or mouse, so part B waits.

| | main | listening (batch 19) | listening (now) | `enabled = false` |
| --- | --- | --- | --- | --- |
| Free internal RAM | 189,095 | 130,971 | 133,683 | 169,919 |
| Largest free internal block | 114,688 | 57,344 | 59,392 | 94,208 |
| Unused stack of `nimble_host` | | 1,300 | 3,940 | |
| Unused stack of `btController` | | 2,096 | 2,096 | |

- The trim of the controller gave back 2.7 KB of internal RAM, as little
  as expected: on costs 55 KB now, off 19 KB.
- The stack of NimBLE's task is no longer a worry before a device is
  tried; real use is still to measure.
- The remote screen: the Wi-Fi was slow that day, so the figures are not
  those of batch 19. A full update took 11 s on main, 15 to 20 s with
  Bluetooth listening in the background, 12 to 14 s with pairing by itself
  off (so not listening, but the radio on), 6 to 14 s with `enabled =
  false`. Still about 1.6 times slower while listening, though listening
  is a fifth of what it was; on a day like that the figures do not say
  whether the thinner listening helped. To measure again on a normal day,
  and with a keyboard and a mouse both connected, when it does not listen
  at all.
- No missed refills. `dbg.py bt scan` heard two devices.
- The app slots are 3 MB since PR 79 was merged in the same batch, so the
  firmware (1,968,768 bytes on the laptop) fills about 63% of its slot
  and the worry of the first run is gone.
