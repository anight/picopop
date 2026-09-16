#!/bin/sh
#
# Compile and run a table generator with the host compiler.
#
#   gen-table.sh <generator.c> <outdir> <output-name.inc>
#
# Separate from build-resources.sh because these have nothing to do with the
# game's DAT files: the output is pure arithmetic and is the same for everybody.
# It still lands in generated/ rather than being committed, so that it cannot
# drift from the expressions it was copied from without the build noticing.
#
# Incremental: does nothing if the .inc is newer than its generator.

set -eu

GEN=${1:?usage: gen-table.sh <generator.c> <outdir> <output.inc>}
OUT=${2:?usage: gen-table.sh <generator.c> <outdir> <output.inc>}
NAME=${3:?usage: gen-table.sh <generator.c> <outdir> <output.inc>}
INC="$OUT/$NAME"

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
