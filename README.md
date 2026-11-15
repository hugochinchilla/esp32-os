# esp32-os

A 90s-style desktop operating environment (think Windows 95/98) for the
ESP32-S3, with VGA output from GPIO pins, Wi-Fi, and USB keyboard and mouse.

Status: **design settled enough to start, hardware bring-up in progress.**
There is no OS yet. The first code is a VGA test program that checks the
resistor DAC wiring.

## What it will be

- 640x480 at 60 Hz on a VGA monitor, 256 colours, with a mouse cursor that
  always moves at 60 Hz.
- A desktop with taskbar, Start menu, icons and overlapping windows.
- A file system with a file manager, a Notepad-style text editor, and a viewer
  for BMP, PNG and JPEG images.
- Wi-Fi, with the clock set from NTP, or from an optional battery-backed clock
  chip.
- A serial debug protocol (screen capture, injected keyboard and mouse events)
  so the OS can be tested by an agent without a human.
- Remote keyboard, mouse and screen over the network until USB input hardware
  exists.

## Hardware

- ESP32-S3 dev board with 16 MB flash and 8 MB octal PSRAM (ESP32-S3-N16R8,
  DevKitC-1 pin layout, two USB-C sockets).
- A VGA adapter built on a 5x7 cm perfboard that the dev board plugs into:
  9 colour resistors and 2 sync resistors, all 1/4 W.

![VGA adapter board layout](docs/hardware/vga-adapter-256.png)

See [docs/hardware/README.md](docs/hardware/README.md) for the pin map, the
resistor values and build notes.

## Repository layout

| Path | Contents |
| --- | --- |
| `docs/DECISIONS.md` | What has been decided, what was rejected, and what is still open |
| `docs/hardware/` | VGA adapter pin map, resistor values and board diagram |
| `firmware/vga-test/` | ESP-IDF test program for the VGA wiring |
| `ci/` | GitHub Actions workflow to copy into `.github/workflows/` |
| `tools/` | Setup script for Arch-based systems, and the script that regenerates the board diagram |

## Building the VGA test

Needs [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
v5.5. On Arch-based systems (including Omarchy), `tools/setup-arch.sh` installs
it together with the build tools and gives your user access to serial ports
without sudo. Log out and back in afterwards, then run `get_idf` in each new
terminal.

```sh
cd firmware/vga-test
idf.py set-target esp32s3
idf.py -p <serial port> flash monitor
```

Plug the computer into the USB-C socket marked UART (or COM). Details and the
keys that switch modes are in
[firmware/vga-test/README.md](firmware/vga-test/README.md).
