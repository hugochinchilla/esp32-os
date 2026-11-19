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
  two switches. (Built later, below.)
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

## The Bluetooth program

The owner chose pairing by itself "with a window for the rest", so the
window was built without waiting for a real device: nothing in it depends
on what part B will find.

Start > Settings > Bluetooth opens a program of its own (process
"Bluetooth", `core/src/shell/bluetooth.cpp`), as every settings tool: made
when it opens, nothing kept while closed, only OS calls (a new one,
`os::bluetooth()`, hands out the service). It shows two lines of state
(which of keyboard and mouse are connected), one list with the devices
connected, then those remembered and not connected, then those in range
that say they are a keyboard or a mouse, a Pair button for the last kind
and a Forget button for the others, and two tick boxes: pairing by itself,
and Bluetooth on from the next start. A pairing asked for there that fails
is told in a box. While the window is open the radio listens hard
(`set_listing`), so a device put in pairing mode shows at once; closed,
listening goes back to thin or off.

Options for the list: one list with the state in words (chosen: one thing
to pick from, and Pair or Forget follows what is picked); two lists, "my
devices" and "in range", as phones have (not chosen: twice the room in a
396 by 320 window for mostly one or two rows each). Turning Bluetooth off
at once from the box was not built: the stack cannot give its memory back
without a restart, so the box says "from the next start".

Two faults found by the picture test and fixed. The box "Could not pair"
came up right after Pair was pressed, because the manager only learnt of
the new connection at its next tick: `Manager::pair` now follows the radio
before it returns. And the box came up again when a keyboard paired
earlier went to sleep: the window now stops watching an attempt once the
device is remembered.

Tested on the PC: `render_bt_scenes` drives the window through the Start
menu, pairs with the button, lets a mouse pair by itself, drops a
keyboard, fails a pairing, forgets, flips both boxes and checks the file
each time; fifteen pictures, the window in every theme. The lifecycle
test opens and closes it as the other tools (twelve blocks open, none
closed). The firmware compiles and links in the cloud (1,943,264 bytes,
5.7 KB more than without the window); the window has not run on
the board.

## An icon in the tray

The owner asked, on seeing the window: "we need a tray icon for bluetooth
status, and clicking on it opens this dialog".

The taskbar's tray held one icon, the network's. It now has two places
(`desk::Taskbar::kMaxTray`), the network's next to the clock where it was
and Bluetooth's left of it; an empty place leaves no gap. The Bluetooth
service sets its icon whenever the manager says something changed, as the
network's does, and a click opens the Bluetooth program.

Three states: off (a red cross), on with nothing connected (grey waves),
a keyboard or mouse connected (blue waves). Not shown: an attempt under
way (it lasts seconds, and the window tells it), and which of keyboard
and mouse is there (the window's first line does).

The drawing: the Bluetooth rune is what everybody knows, but it is a
registered trademark and the rule here is original or freely licensed
art, so the icon is a drawing of its own, a keyboard under two radio
waves. The owner can ask for another. The same icon is the window's and
the Settings menu entry's.

Tested on the PC: the scenes check that the icon is there from the start,
show it off and beside the network's, and open the window by a click on
it; the fifteen pictures were drawn again with the icon in them and two
were added. The firmware compiles and links; not run on the board.

### The icon, drawn again

The owner, on seeing the keyboard under waves: try a reinterpretation of
the Bluetooth rune, close enough to the one everybody knows without being
a copy.

The mark is a bind rune: hagall and bjarkan, the initials of Harald
Bluetooth, drawn as one sign, white in a blue oval. What makes it the mark
is the binding (the two arms crossing to the left of the stem) and the
oval. The icon keeps what is nobody's: bjarkan alone, the letter B of the
old Norse alphabet, a stem with two pointed bows, white on a square blue
tile. It reads as Bluetooth at a glance and has neither the arms nor the
oval. Tried first without a tile, blue on the bar: it vanished on the blue
taskbar of the Sky theme, so the tile stays. States: a grey tile with a
red cross (off), the blue tile (on), a green light in its corner (a
keyboard or mouse connected). This is a drawing choice, not legal advice;
the project is not distributed.

Minutes later the owner went further: he had read on the Bluetooth site
that what is under trademark is the logo, the rune inside a blue oval, and
reasoned that the rune in blue with no oval is only two letters of an
alphabet drawn together. So the icon is now the bind rune itself, hagall
and bjarkan, blue, with a white edge one pixel wide in place of any tile
or oval (the edge is what keeps it visible on the blue bar of Sky and the
dark one of Midnight). The agent could not check how far the mark reaches
and recorded the choice as the owner's. Grey with a red cross when off, a
green light in the corner when a keyboard or mouse is connected. The
bjarkan-only tile lasted one build and was never pushed.

In the afternoon the owner asked for the indicator "a bit thinner" and a
try without the white outline. The rune went from two pixels thick to one.
Without the edge it was clean on the grey bar, lost on the blue bar of Sky
and weak on the dark ones; he was shown both rows in the four themes and
three ways out: no edge and white on dark or blue bars (chosen), keep the
edge, or no edge and always blue. The Bluetooth service picks the tray's
picture from the theme (a bar written in a light ink is a dark bar) and
again when the theme changes. The program's own icon keeps the edge: it
sits on the blue title bar and in menus.

## The first real mouse: the pointer jumped

The third board turn was the first with the owner's own Bluetooth keyboard
and mouse, and they paired and worked. His words on the mouse: it does not
feel fluid; it never stops, but it seems to jump large distances between
repaints. The board counted 150 to 160 movements a second, mostly 10 ms
apart, with gaps of 40 to 50 ms, about three frames.

The cause was in the adapter: the connection was asked for with NimBLE's
own pace (`ble_gap_connect` with no parameters), which lets a device
speak every 30 to 50 ms. A mouse makes 125 reports a second, so it handed
them over four or five at a time, and the adding-up on the way to the UI
task turned each handful into one long step. The connection is now asked
for at 7.5 to 11.25 ms, with no skipped turns. A device may still ask for
another pace afterwards, and is given it; the log now says the pace at
connection and at every change, so the board check can read what each
device really runs at.

Options: 7.5 to 11.25 ms for every device (chosen: one rule, and a
keyboard gains too); the fast pace for mice only and a slow one for
keyboards, to leave Wi-Fi more air (not chosen yet: to decide with the
remote screen's figures once both are connected). Not touched: listening
in the background, which also takes radio time while a remembered device
is away, and the Wi-Fi sharing; they are to look at if gaps remain.

The firmware compiles and links (1,948,320 bytes). Not run on the board:
the pace a real mouse accepts is exactly what the next turn measures.
