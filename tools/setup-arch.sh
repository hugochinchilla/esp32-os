#!/usr/bin/env bash
# Sets up an Arch-based system (Arch, Omarchy) to build and flash this project:
# build tools, ESP-IDF v5.5 for the ESP32-S3, and serial port access without sudo.
# Safe to run more than once.
set -euo pipefail

IDF_DIR="$HOME/esp/esp-idf"

echo "==> Installing build tools (asks for your password)"
sudo pacman -S --needed git wget flex bison gperf python python-pip cmake ninja ccache dfu-util libusb gcc make

echo "==> Allowing $USER to use serial ports without sudo"
# On Arch, /dev/ttyUSB* and /dev/ttyACM* belong to the group uucp.
sudo usermod -aG uucp "$USER"

echo "==> Downloading ESP-IDF v5.5 to $IDF_DIR"
mkdir -p "$HOME/esp"
if [ ! -d "$IDF_DIR" ]; then
    git clone --depth 1 -b release/v5.5 --recursive --shallow-submodules \
        https://github.com/espressif/esp-idf.git "$IDF_DIR"
fi

echo "==> Installing the ESP32-S3 compiler and tools"
"$IDF_DIR/install.sh" esp32s3

echo "==> Adding the get_idf shortcut to ~/.bashrc"
if ! grep -q "alias get_idf=" "$HOME/.bashrc" 2>/dev/null; then
    echo "alias get_idf='. \$HOME/esp/esp-idf/export.sh'" >> "$HOME/.bashrc"
fi

cat <<'DONE'

Done. Two things before the first build:
  1. Log out and back in (or reboot) so the serial port permission applies.
  2. In each new terminal, run:  get_idf

Then, from the repository:
  cd firmware/vga-test
  idf.py -p /dev/ttyACM0 flash monitor     # the port may be /dev/ttyUSB0
Leave the monitor with Ctrl+]
DONE
