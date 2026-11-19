# Larger firmware partitions, and what fills them

2026-11-19. Backlog item 71. Decision: `docs/decisions/app-slots-3-mb.md`.

## What was asked

The owner: "Parece que nos estamos quedando sin espacio para el firmware.
Amplia las particiones de firmware cuando sea necesario. Dame un reporte de
que piezas gastan que".

## What was found

The flash is 16 MB. The table had the BIOS in the first megabyte of the app
area (960 KB at 0x10000), two update slots of 0x1f0000 bytes (2,031,616,
"1.94 MB") at 0x100000 and 0x2f0000, `otadata` at 0x4e0000 and C: (LittleFS,
11 MB) from 0x500000.

Built in the cloud with ESP-IDF v5.5 (`tools/setup-cloud-idf.sh`):

| Firmware | `esp32-os.bin` | of the old slot | of a 3 MB slot |
| --- | --- | --- | --- |
| `main` (1d035ec) | 1,763,776 | 86.8 % | 56.1 % |
| Bluetooth (PR 77, c168da9 with `main` merged in) | 1,947,360 | 95.9 % | 61.9 % |

What goes to the flash, by library (`python -m esp_idf_size --archives`,
total size less the `.bss` and no-init parts, which take no flash), grouped:

| Piece | `main` | share | Bluetooth | difference |
| --- | --- | --- | --- | --- |
| Chinchilla core (`libcore.a`) | 647,457 | 36.9 % | 655,781 | +8,324 |
| Wi-Fi (net80211, pp, wpa_supplicant, phy, coexistence) | 307,664 | 17.6 % | 314,020 | +6,356 |
| ESP-IDF system (FreeRTOS, drivers, HAL, heap, ...) | 214,590 | 12.2 % | 215,139 | +549 |
| TCP/IP (lwIP, esp_netif, mDNS, esp_event) | 145,678 | 8.3 % | 145,986 | +308 |
| Bluetooth (`libbtdm_app.a` 74,628 and `libbt.a` 71,058) | 0 | | 145,686 | +145,686 |
| Chinchilla adapters (`libmain.a`) | 127,331 | 7.3 % | 134,787 | +7,456 |
| Text strings | 119,017 | 6.8 % | 131,809 | +12,792 |
| C and C++ library | 95,152 | 5.4 % | 95,144 | -8 |
| mbedTLS | 66,638 | 3.8 % | 67,002 | +364 |
| LittleFS | 28,831 | 1.6 % | 28,863 | +32 |
| Total | 1,752,358 | | 1,934,217 | +181,859 |

The largest files of our own (`--files`): `sample_pictures.cpp` 159,819 (the
sample photos copied to a new C:), `bundle.c` 92,873 (the loadable programs
copied to C:), `ui.cpp` 41,210, `art.cpp` 39,958, `wm.cpp` 32,990, the
adapters' `main.cpp` 23,236, `html.cpp` 18,139, `browser.cpp` 17,885,
`os.cpp` 15,534, `file_manager.cpp` 13,335, `viewer_page.cpp` 11,681.
About 300 KB of our 775 KB is data, not code.

## The options

Put to the owner as a card; the slots stay two in each, and `nvs` does not
move in any of them.

| Slots | C: | With Bluetooth | |
| --- | --- | --- | --- |
| 3 MB | 9 MB | 62 % full | recommended, chosen |
| 4 MB | 7 MB | 49 % full | takes 4 MB from C:, where loadable programs go |
| 2.5 MB | 10 MB | 79 % full | the same change again soon |

## What was chosen

The owner chose 3 MB. The agent chose where `otadata` goes: at 0xf0000, in
the last 64 KB of the BIOS's megabyte (the BIOS is about 300 KB of it), so
that `ota_0` stays at 0x100000 and `ota_1` and C: start on whole megabytes.
The other place, after `ota_1`, would have left C: starting at 0x702000 or
wasted 56 KB.

## What was built

`firmware/os/partitions.csv`:

```
nvs,      data, nvs,      0x9000,   0x6000
phy_init, data, phy,      0xf000,   0x1000
bios,     app,  test,     0x10000,  0xe0000
otadata,  data, ota,      0xf0000,  0x2000
ota_0,    app,  ota_0,    0x100000, 0x300000
ota_1,    app,  ota_1,    0x400000, 0x300000
storage,  data, littlefs, 0x700000, 0x900000
```

No code changed: the BIOS, the bootloader and the OS find partitions by
type and name. The BIOS reads the table from the flash when it runs and
starts where it did, so it is not written again. The board-turn skill names
the new layout.

Tested: the PC tests pass (`tools/gate.sh`; four tests first failed for a
missing `pyserial` in the container and passed once it was installed). The
firmware compiles and links with the new table; a full flash would write
0x0, 0x8000, 0xf0000 and 0x100000. **Nothing has run on the board.** Not
tested anywhere: that the BIOS finds `otadata` at its new place, and an
update over the network into the 3 MB `ota_1`.

## What is left

The board turn: copy C:'s files to the laptop, write the table, the
bootloader, `otadata` and the OS by cable, erase the new C: region
(0x700000 to the end) so LittleFS formats it clean, put the files back, and
check an update over the network. `nvs` is never written.

If the firmware ever fills 3 MB: the sample photos and the loadable programs
(252 KB) could be put on C: when the board is flashed instead of riding in
the firmware; building for size instead of speed would cost drawing time and
has not been measured.
