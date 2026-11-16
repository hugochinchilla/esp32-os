# Where the internal RAM goes, and whether wired networking would be cheaper

## What was asked

The owner was surprised that the framebuffer lives in the slower external RAM
and that only about 140 KB of internal RAM is free, and asked (through the
coordinator) for a measured answer: where the internal SRAM goes, and whether
a wired link would cost less RAM than Wi-Fi. Report only: nothing applied.

## The totals

The ESP32-S3 has 512 KB of internal SRAM. With this firmware:

| | KB | How known |
| --- | ---: | --- |
| Static, in the D/IRAM region (code that must run from RAM, variables) | 230.6 of 341.8 | `idf.py size` |
| Static, IRAM-only region (vectors and code) | 16.0 of 16.0 | `idf.py size` |
| Heap total, internal (`heap_caps_get_total_size(MALLOC_CAP_INTERNAL)`) | 258.3 (264,483 bytes) | measured |
| of which DMA-capable | 250.3 | measured |

The rest of the 512 KB is ROM data and the cache. External RAM (PSRAM) is
8 MB, 7.0 MB of it in the heap.

**Why the framebuffer is in PSRAM:** 640 x 480 x 1 byte = 307,200 bytes,
more than the whole internal heap (264,483 bytes). It cannot fit. The cost:
every 320 us an interrupt copies 10 lines (6,400 bytes) from PSRAM into one of
two bounce buffers in internal RAM (12.8 KB together). On the board the
counter `scanout_us` read 10.19 s after 193.3 s of uptime: **5.3 % of one
core**.

## Static use, by owner (measured, `idf.py size`, `esp_idf_size --archives`)

| Group | .data | .bss | IRAM code | Total KB |
| --- | ---: | ---: | ---: | ---: |
| Our code (core + firmware adapters) | 4.1 | 112.9 | 0.7 | 117.7 |
| ESP-IDF core (hal, flash, heap, system, PSRAM, timers...) | 10.7 | 1.2 | 53.6 | 65.5 |
| Wi-Fi driver, PHY, WPA supplicant | 7.3 | 10.7 | 6.8 | 24.9 |
| FreeRTOS | 3.0 | 1.1 | 16.8 | 20.9 |
| lwIP and esp_netif | 0.0 | 3.7 | 0.0 | 3.7 |
| C library | 0.4 | 1.0 | 2.1 | 3.5 |
| mDNS | 0.0 | 2.3 | 0.0 | 2.4 |
| LCD driver (video) | 0.0 | 0.0 | 0.6 | 0.7 |
| LittleFS | 0.1 | 0.0 | 0.0 | 0.1 |
| All | 25.8 | 132.9 | 80.7 | 239.4 |

The surprise is the first line: **our own static variables are 113 KB of
internal RAM**, more than Wi-Fi, lwIP and mDNS together. They are C++ objects
that exist for the whole run: the File manager's three windows (11.3 KB,
each with its listing and history), the remote-screen server state (7.3 KB),
the paint band (7.5 KB), the desktop (5.1 KB), the debug protocol (4.1 KB),
the Defragmenter's window and its model (7.5 KB), the file dialog (3.8 KB),
the viewer window (3.7 KB), the starfield (3.6 KB), the desktop file list
(3.4 KB), the scanout timeline (3.1 KB), the network panel (3.0 KB), the
Cactus Patch and Culebra windows (4.5 KB), seven 2 KB rectangle lists, and
smaller ones. USB host is not built yet, so it costs nothing here.

## The heap at run time (measured on the board)

| State | Free KB | Largest block KB | Used KB |
| --- | ---: | ---: | ---: |
| Wi-Fi off (`enabled = false`) | 170.5 | 92.0 | 87.8 |
| Wi-Fi started, not connected | 105.0 | 31.0 | 153.3 |
| Connected, idle | 105.0 | 31.0 | 153.3 |
| Connected, a VNC viewer updating | 104.9 | 31.0 | 153.4 |
| Connected, the browser viewer open | 104.9 | 31.0 | 153.4 |

"Lowest ever" reads about 28 KB in every state, Wi-Fi or not, because the
start-up memory test borrows the free internal RAM for a moment; it says
nothing about load. Viewers cost almost nothing in internal RAM (their
buffers and lwIP's packets go to PSRAM: `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`).
Wi-Fi costs **65.5 KB of heap the moment the radio starts**, connected or not.

What the 87.8 KB used without Wi-Fi holds (sizes from the configuration and
source; FreeRTOS reports only each stack's unused part):

| Allocation | KB | Notes |
| --- | ---: | --- |
| UI task stack | 16.0 | at most 8.6 KB used today (7.6 KB never touched) |
| Video bounce buffers | 12.5 | two of 10 lines, DMA-capable |
| Debug-link UART receive buffer | 4.0 | |
| esp_timer task stack | 3.5 | 3.3 KB never touched |
| Scanout timeline task stack | 3.0 | ends 20 s after start |
| Timer service task stack | 2.0 | |
| Two idle task stacks | 3.0 | |
| Two IPC task stacks | 2.5 | |
| The rest | about 41 | task blocks, queues, LittleFS, NVS, driver state, the LCD's descriptors, heap overhead; not split further (no heap tracing build was made) |

What Wi-Fi's 65.5 KB holds (sizes from the configuration; the total is
measured, the split is an estimate):

| Allocation | KB |
| --- | ---: |
| 16 static transmit buffers (about 1.6 KB each) | 25.6 |
| 8 static receive buffers | 12.8 |
| Wi-Fi task stack | 6.5 |
| mDNS task stack | 4.0 |
| lwIP task stack | 3.0 |
| Event task stack | 2.3 |
| Driver, PHY calibration, management buffers, lwIP, netif, mDNS, servers | about 11 |

Task stacks with Wi-Fi connected (unused bytes measured by FreeRTOS; sizes,
priorities and cores from the configuration and source):

| Task | Stack | Never used | Priority | Core |
| --- | ---: | ---: | ---: | --- |
| ui | 16,384 | 11,044 now, 7,780 lowest seen | 5 | either |
| wifi | 6,656 | 3,644 | 23 | 0 |
| mdns | 4,096 | 2,392 | 1 | 0 |
| esp_timer | 3,584 | 3,156 | 22 | 0 |
| tiT (lwIP) | 3,072 | 1,772 | 18 | 0 |
| sys_evt | 2,304 | 768 | 20 | either |
| Tmr Svc | 2,048 | 1,420 | 1 | either |
| IDLE0, IDLE1 | 1,536 each | 780, 772 | 0 | 0, 1 |
| ipc0, ipc1 | 1,280 each | 580, 588 | 24 | 0, 1 |
| video (starts the scanout) | 4,096 | ends | 10 | 1 |
| scanlog | 3,072 | ends after 20 s | 1 | either |

The other thread's FreeRTOS statistics (PR 13) will give exact per-task
numbers once it is on the board.

## What dropping Wi-Fi saves

- Measured: switching the radio off (`enabled = false`) frees **65.5 KB** of
  heap and gives a 92 KB largest block instead of 31 KB.
- From the size report: a build without the Wi-Fi driver would also free
  **24.9 KB** of static RAM (not built).
- lwIP, its task, the event task, mDNS and the VNC and web servers stay with
  any other network link, about 12 KB of the above. So Wi-Fi itself costs
  about **78 KB** of internal RAM.

## Wired alternatives (estimates unless marked)

None of these was built today: these are estimates from the configuration,
Espressif's documentation and the protocols' sizes, and say so.

| Link | Internal RAM, est. | Throughput, est. | Conflicts |
| --- | ---: | --- | --- |
| Wi-Fi, as now | 78 (measured, plus the static size) | about 270 KB/s measured at 2 dBm | the scanlines at full power |
| PPP over the serial link (lwIP PPPoS, esp_netif PPP; `pppd` on the laptop) | 4 to 8 | about 85 KB/s at 921,600 baud (92 KB/s raw, less framing): a ZRLE screen (4.5 KB) in 0.1 s, a raw one in 3.6 s | the wire is the debug link, and the console will share it too: PPP frames would have to be told apart from debug frames, or the debug protocol would have to move inside PPP (TCP); a mistake cuts the link used to diagnose it. `pppd` needs root on the laptop |
| SLIP over the serial link (lwIP slipif; `slattach` on the laptop) | 2 to 4 | about 85 KB/s | as PPP, with no negotiation or error check; simpler |
| USB networking on the second USB-C socket (TinyUSB, CDC-NCM) | 15 to 25 (stack, task, two 3.2 KB transfer blocks each way) | 0.5 to 1 MB/s (USB full speed) | that socket is planned as USB host for the keyboard and mouse; the S3 has one USB OTG port, so not both at once |
| Ethernet over SPI | | | rejected: a module would have to be bought |
| Bluetooth LE (NimBLE), for input, not networking | 40 to 70 (controller plus host) | | competes for the same RAM; with Wi-Fi both at once, more for coexistence |

## Cheap wins still available (none applied)

| Change | Saves, KB | Downside |
| --- | ---: | --- |
| Our static objects into PSRAM (`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`, all of libcore's .bss and the firmware's big statics placed there) | up to about 105 (113 less the paint band, kept internal) | those objects are a little slower to touch; nothing the video interrupt reads may go there (none of them is). Safe during flash writes because the code already runs from PSRAM (XIP) |
| Wi-Fi off by default, a switch to turn it on | 65.5 (measured) | no remote screen, no network clock, no `chinchilla.local` until switched on |
| Wi-Fi transmit buffers dynamic instead of 16 static ones (they then go to PSRAM) | about 25 | slightly more latency; to be measured |
| UI task stack 16 KB to 12 KB | 4 | 3.4 KB margin over the deepest use seen so far |
| UI task stack in PSRAM (`CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` is already on) | 16 | slower calls; must be checked against flash writes from that task, though XIP from PSRAM keeps PSRAM readable during them |
| Static receive buffers 8 to 4 | about 6.4 | lower throughput under load |
| mDNS only on demand or off | about 6.4 (task and statics) | no `chinchilla.local` |
| VNC and web server only on demand | under 0.2 (measured) | not worth it for RAM |
| Code out of internal RAM (FreeRTOS, heap, ring buffer functions into flash: `*_PLACE_FUNCTIONS_INTO_FLASH`) | up to about 28 | slower scheduler and allocator; with XIP from PSRAM they stay usable during flash writes |
| Drop the scanout timeline (diagnostic) | 3.1 | no start-up timeline in `dbg.py memory` |
| Lower log level | under 0.5 | the strings are in flash already, so almost nothing |

## Found on the way

At every start the scanout counters record one missed refill at about
10.4 s, the worst 0.9 to 1.0 ms late: that is when the desktop first
appears. One line of one frame comes out wrong. It needs looking into.

## How this was measured

`idf.py size`, `esp_idf_size --archives`, `xtensa-esp32s3-elf-nm` for the
biggest symbols; on the board `tools/dbg.py memory` and `stats` after a
restart in each state (four restarts, all announced; a temporary
`network.ini` for the Wi-Fi-off run, removed afterwards), with a VNC client
and headless Chromium on the browser page holding the screen while sampling.
No heap tracing build was made, so the "rest" lines are differences, not
per-owner figures.
