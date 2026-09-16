#!/bin/sh
#
# Generate generated/midi_tables.inc: every MIDI tune in the game, parsed at build
# time.
#
#   gen-midi-tables.sh <generated-dir>
#
# Unlike gen-table.sh's generators this one is not pure arithmetic - it needs the
# converted game resources and SDLPoP's own parser, so it host-compiles several
# files rather than one. Reusing the real parse_midi() is the point: a second
# implementation of the MIDI parser could disagree with the firmware's, and this
# one cannot.
#
# Incremental: does nothing if the .inc is newer than everything it is built from.

set -eu

ROOT=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
OUT=${1:?usage: gen-midi-tables.sh <generated-dir>}
INC="$OUT/midi_tables.inc"

GEN="$ROOT/tools/assets/midi_tables.c"
POP="$ROOT/SDLPoP/src"
PSDL="$ROOT/picosdl"
GLUE="$ROOT/tools/tests"
RES="$OUT/resources"

up_to_date=true
[ -f "$INC" ] || up_to_date=false
for dep in "$GEN" "$0" "$POP/midi.c" "$RES/resources.c"; do
    [ -f "$dep" ] || { echo "gen-midi-tables: missing $dep" >&2; exit 1; }
    [ "$INC" -nt "$dep" ] || up_to_date=false
done
$up_to_date && exit 0

: "${CC_HOST:=cc}"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# PSDL_HAVE_AUDIO so midi.c sees the same audio configuration the firmware does.
#
# opl3.c is linked only to satisfy midi.c's references to the emulator; this
# program parses and never synthesises. Nuked rather than DBOPL because it is C
# and needs no C++ link step, and which emulator it is cannot affect the output.
"$CC_HOST" -std=c11 -O1 -w -DPSDL_HAVE_AUDIO=1 -DPICOPOP_USE_DBOPL=0 \
    -DPICOPOP_MIDI_TABLES_GENERATING=1 \
    -I "$PSDL/include" -I "$PSDL/src" -I "$POP" -I "$GLUE" -I "$RES" \
    -o "$TMP/gen" \
    "$GEN" "$POP/midi.c" "$POP/opl3.c" "$GLUE/game_glue.c" \
    "$PSDL/test/host_backend.c" \
    "$PSDL"/src/*.c "$RES"/*.c \
    -lm

"$TMP/gen" > "$INC.tmp"
mv "$INC.tmp" "$INC"
