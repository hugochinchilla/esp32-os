#!/bin/sh
# Builds the BIOS (firmware/bios) and writes it to its partition, 0x10000.
# The bootloader, the partition table and the OS are flashed from
# firmware/os with "idf.py flash" as before; this adds the BIOS.
#
#   tools/flash-bios.sh [port]        (default /dev/ttyACM0)
#
# Do not run "idf.py flash" inside firmware/bios: it would write the BIOS
# where the OS lives and Espressif's bootloader over ours.
set -e
PORT="${1:-/dev/ttyACM0}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE/firmware/bios"
idf.py build
python3 -m esptool --chip esp32s3 -p "$PORT" -b 460800 --before default_reset --after hard_reset \
    write_flash 0x10000 build/chinchilla-bios.bin
