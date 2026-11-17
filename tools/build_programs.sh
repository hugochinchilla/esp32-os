#!/bin/sh
# Builds the programs that are kept in files (programs/*, backlog item 27)
# for the board: each folder becomes build/programs/<name>.app, an ELF file
# the system loads from a drive. Needs the ESP32-S3 compiler on the PATH
# (ESP-IDF's export.sh, or `. tools/setup-cloud-idf.sh env`).
#
#   tools/build_programs.sh                    # build
#   tools/build_programs.sh <firmware build>   # and check against the firmware
#
# With the firmware's build folder it also checks that everything each
# program asks the system for by name is in the firmware's table of exports
# (tools/gen_exports.py): a program that asks for more would be refused when
# it is loaded.
#
# Copy a program to the board with tools/dbg.py put, then open it from the
# File manager or type its path in Run.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${CXX:-xtensa-esp32s3-elf-g++}"
NM="${NM:-xtensa-esp32s3-elf-nm}"
STRIP="${STRIP:-xtensa-esp32s3-elf-strip}"
OBJCOPY="${OBJCOPY:-xtensa-esp32s3-elf-objcopy}"
OUT="$HERE/build/programs"
mkdir -p "$OUT"
built=""
for dir in "$HERE"/programs/*/; do
    name="$(basename "$dir")"
    # The flags Espressif's loader asks for (elf_loader.cmake, project_elf):
    # position independent, nothing of the libraries linked in, the way in
    # named app_main; and the core's own (no exceptions, no RTTI).
    "$CXX" -std=gnu++17 -Os -Wall -Wextra -Werror -mlongcalls -fno-exceptions -fno-rtti \
        -fPIC -shared -nostartfiles -nostdlib -e app_main \
        -fdata-sections -ffunction-sections -Wl,--gc-sections -fvisibility=hidden \
        -Wl,-T,"$HERE/tools/program.ld" \
        -I"$HERE/core/include" "$dir"*.cpp -o "$OUT/$name.elf"
    "$STRIP" --strip-unneeded --remove-section=.comment --remove-section=.xt.lit \
        --remove-section=.xt.prop --remove-section=.xtensa.info "$OUT/$name.elf"
    # The file: the program's header, then the code.
    "$HERE/tools/pack_app.sh" "$OBJCOPY" "$OUT/$name.elf" "$OUT/$name.app"
    # What tools/program.ld is for: no read-only data in the last word of
    # the code.
    python3 "$HERE/tools/check_program_elf.py" "${READELF:-xtensa-esp32s3-elf-readelf}" "$OUT/$name.elf"
    echo "$OUT/$name.app: $(wc -c < "$OUT/$name.app") bytes"
    built="$built $OUT/$name.elf"
done
if [ -n "$1" ]; then
    python3 "$HERE/tools/gen_exports.py" --nm "$NM" --check $built -- "$1/esp-idf/core/libcore.a"
fi
