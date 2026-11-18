---
title: "Ideas kept for later"
date: 2026-11-16
by: owner
status: idea
area: project
summary: "Not scheduled: dial-up and a software modem over serial, network over USB, sound, Rust, Bluetooth, a virus"
---

# Ideas kept for later

Not scheduled. To be revisited once a basic OS boots.

| Idea | How it would work | Cost |
| --- | --- | --- |
| Dial-up over serial | PPP from the OS to a PPP server (`pppd`) on the connected computer, which shares its internet connection. About 90 KB/s at 921,600 baud; could be throttled to 56k and given a Dial-Up Networking window that "dials" | No hardware, no pins. Needs a one-time setup on the computer; awkward on Windows |
| Dial-up and debugging on the same serial port | The debug protocol is already framed packets, so network traffic travels as one more packet type. The PC tool splits the two and hands the network side to `pppd` through a virtual serial port | No hardware. More work in the PC tool |
| Software modem over serial | A real modem implemented in software at both ends. Instead of a phone line, the two sides exchange digital audio samples over the serial link (8,000 samples a second, as the digital phone network did), and the board plays them through the audio output, so the dial-up sounds are genuine. Realistic to implement: dial tone, touch-tone dialling, ringing, answer tone, and data at 300 to 2,400 bit/s (V.21, V.22bis). The well-known 33.6k and 56k negotiation (V.34, V.90) is far too complex to implement fully; it would be imitated, with the data then carried as plain serial | No hardware. Needs the sound output first, and a "phone exchange" program on the computer. Real modem speeds are very slow |
| Network over USB | The second USB-C socket presents the board to the computer as a network card. Much faster than serial | Uses the port reserved for the keyboard and mouse, so not both at once |
| Second serial port | A UART on two spare pins for dial-up, separate from the debug link | Needs a USB-serial adapter |
| Sound | Audio out on GPIO13 through a 1 kΩ resistor and a 100 nF capacitor to a 3.5 mm jack for amplified speakers, plus a piezo beeper on GPIO14. Both have their place on the adapter board. Wanted for a boot chime and, above all, dial-up modem sounds. The chip generates the signal in hardware, separate from the video output. No firmware for it yet | Parts already at home. Sounds must be original or synthesized: no Microsoft chime, no copied recordings |
| Port to Rust | Rewrite the OS in Rust as a later adventure, either on ESP-IDF or without it | A full rewrite. Check the state of Rust's display and USB host support for this chip first |
| Wired Ethernet | SPI module with an RJ45 socket, 5 pins | Rejected for now: a module would have to be bought |
| Battery-backed clock | I2C clock chip with a coin cell on GPIO1 and GPIO2 | Only if parts are already at home |
| Bluetooth keyboard and mouse | The chip's Bluetooth LE radio acts as a HID host, so wireless keyboards and mice that speak Bluetooth LE feed the same input path as USB and the debug link. Needs a pairing window. Out of place for the era, wanted anyway (2026-11-16) | No hardware. Older Bluetooth Classic keyboards and mice will not work: the ESP32-S3 has Bluetooth LE only. Shares the radio with Wi-Fi and takes flash and RAM for the Bluetooth stack |
| Bluetooth audio | Not possible on this chip: the ESP32-S3 has no Bluetooth Classic (so no A2DP) and no LE Audio | Ruled out |
| A virus, for fun | The owner would like to write one for Chinchilla later, to see how they work: with no memory protection every program can reach every other, as on the home computers of the time (2026-11-16) | Needs programs that can be loaded from disk to be interesting |
