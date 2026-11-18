#!/bin/sh
# Makes a program file (backlog item 27) from a compiled program: the
# header the program wrote with APP_INFO (its .appinfo section), then the
# program as the loader takes it. core/include/core/app/file.hpp describes
# the header.
#
#   tools/pack_app.sh <objcopy> <compiled program> <name.app>
set -e
OBJCOPY="$1"
IN="$2"
OUT="$3"
"$OBJCOPY" -O binary --only-section=.appinfo "$IN" "$OUT.head"
if [ "$(wc -c < "$OUT.head")" -ne 1332 ]; then
    rm -f "$OUT.head"
    echo "$IN: no APP_INFO in it, or not of this system's size" >&2
    exit 1
fi
cat "$OUT.head" "$IN" > "$OUT"
rm -f "$OUT.head"
