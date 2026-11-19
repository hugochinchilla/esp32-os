#!/bin/sh
# Builds the programs that are kept in files (programs/*, backlog item 27)
# for the board: each folder becomes build/programs/<name>.app, an ELF file
# the system loads from a drive. Needs the ESP32-S3 compiler on the PATH
# (ESP-IDF's export.sh, or `. tools/setup-cloud-idf.sh env`).
#
#   tools/build_programs.sh                    # build
#   tools/build_programs.sh <firmware build>   # and check against the firmware
#   EXTRA_PROGRAMS="tools/bench/chess_bench/" tools/build_programs.sh ...
#                                              # and programs kept elsewhere
#                                              # (folders, from the repository)
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
CC="${CC:-xtensa-esp32s3-elf-gcc}"
NM="${NM:-xtensa-esp32s3-elf-nm}"
STRIP="${STRIP:-xtensa-esp32s3-elf-strip}"
OBJCOPY="${OBJCOPY:-xtensa-esp32s3-elf-objcopy}"
OUT="${PROGRAMS_OUT:-$HERE/build/programs}"
mkdir -p "$OUT"
built=""
extra=""
for dir in $EXTRA_PROGRAMS; do
    extra="$extra $HERE/${dir%/}/"
done
for dir in "$HERE"/programs/*/ $extra; do
    name="$(basename "$dir")"
    # A program with C in it (Doom, backlog item 81): its own *.c and the
    # files its engine.txt names, from the repository's root. They are not
    # ours to make free of warnings, they are given the folder's port.h in
    # front of everything, in place of the C library, and they are compiled
    # several at a time, there being many.
    objects=""
    include=""
    if [ -f "${dir}engine.txt" ]; then
        mkdir -p "$OUT/$name.obj"
        include="-I$dir"
        for folder in $(sed 's|/[^/]*$||' "${dir}engine.txt" | sort -u); do
            include="$include -I$HERE/$folder"
        done
        (cd "$HERE" && { ls "${dir}"*.c; sed "s|^|$HERE/|" "${dir}engine.txt"; }) | xargs -P 8 -I{} sh -c \
            '"$0" -std=gnu99 -O2 -w -mlongcalls -fPIC -fdata-sections -ffunction-sections -fvisibility=hidden \
                 $1 -include "$2port.h" -c "$4" -o "$3/$(basename "$4" .c).o" || exit 255' \
            "$CC" "$include" "$dir" "$OUT/$name.obj" {}
        objects="$(ls "$OUT/$name.obj"/*.o)"
    fi
    # The flags Espressif's loader asks for (elf_loader.cmake, project_elf):
    # position independent, nothing of the libraries linked in, the way in
    # named app_main; and the core's own (no exceptions, no RTTI).
    "$CXX" -std=gnu++17 -Os -Wall -Wextra -Werror -mlongcalls -fno-exceptions -fno-rtti \
        -fPIC -shared -nostartfiles -nostdlib -e app_main \
        -fdata-sections -ffunction-sections -Wl,--gc-sections -fvisibility=hidden \
        -Wl,-T,"$HERE/tools/program.ld" \
        -I"$HERE/core/include" -I"$HERE/core/src/shell" $include "$dir"*.cpp $objects -o "$OUT/$name.elf"
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
