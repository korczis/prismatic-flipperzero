#!/usr/bin/env bash
# Builds build/sim/pocketsim: the portable app (app/ui, app/core) with the firmware's u8g2.
#   tests/sim/build.sh [extra CFLAGS...]   e.g. coverage flags from tests/native/run.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
"$root/tests/sim/fetch.sh"
cc="${CC:-cc}"
u8g2="$root/build/sim/u8g2"
obj="$root/build/sim/obj"
mkdir -p "$obj"
for f in "$u8g2"/*.c; do
  o="$obj/$(basename "$f" .c).o"
  [ -f "$o" ] && [ "$o" -nt "$f" ] && continue
  "$cc" -O1 -w -ffunction-sections -fdata-sections -c -I"$u8g2" "$f" -o "$o" &
done
wait
strip_flag="-Wl,--gc-sections"
[ "$(uname)" = Darwin ] && strip_flag="-Wl,-dead_strip"
"$cc" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -g "$@" \
  -I"$root/tests/sim/include" -I"$u8g2" -I"$root/app" \
  "$root/tests/sim/pocketsim.c" "$root/app/ui/page.c" "$root/app/ui/ui_app.c" "$root/app/ui/fonts.c" \
  "$root/app/core/pkp.c" "$root/app/core/pocket.c" "$root/app/core/cache.c" "$root/app/core/progress.c" "$root/app/core/session.c" "$root/tests/sim/canvas.c" \
  "$obj"/*.o "$strip_flag" -o "$root/build/sim/pocketsim"
