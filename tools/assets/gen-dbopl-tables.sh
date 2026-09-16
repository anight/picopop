#!/bin/sh
#
# Generate DBOPL's floating-point tables into <outdir>/dbopl_tables.inc.
#
# Separate from build-resources.sh because it has nothing to do with the game's
# DAT files: the output is pure arithmetic and is the same for everybody. It
# still lands in generated/ rather than being committed, so that it cannot drift
# from the expressions in dbopl.cpp without the build noticing.
#
# Incremental: does nothing if the .inc is newer than the generator.

set -eu

ROOT=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)
GEN="$ROOT/tools/assets/dbopl_tables.c"
OUT="${1:?usage: gen-dbopl-tables.sh <outdir>}"
INC="$OUT/dbopl_tables.inc"

if [ -f "$INC" ] && [ "$INC" -nt "$GEN" ]; then
    exit 0
fi

: "${CC_HOST:=cc}"

mkdir -p "$OUT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

"$CC_HOST" -O2 -std=c11 -Wall -Wextra -o "$TMP/gen" "$GEN" -lm
"$TMP/gen" > "$INC.tmp"
mv "$INC.tmp" "$INC"

echo "Generated $INC"
