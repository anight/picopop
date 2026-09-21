#! /bin/bash
#
# Turn a set of Prince of Persia DAT files into the C the firmware links.
#
#   build-resources.sh [--reference] [<data-dir> [<work-dir>]]
#
# This is the whole asset pipeline in one place, and it is what the build calls.
# Nothing it produces is ever committed: the DAT files are the original game's
# and the generated C is derived from them, so both stay out of git and out of
# anyone's release. You supply the DATs; the build makes everything else.
#
#   <data-dir>  where the DAT files are          (default: SDLPoP/data)
#   <work-dir>  where to extract and generate    (default: generated/)
#   --reference also build reference.bin, which tools/tests/ needs
#
# The result is <work-dir>/resources/resources.{c,h,inc}.
#
# It is incremental: a stamp file records the DATs it last ran against, and
# re-running with nothing changed does nothing. Extraction and conversion
# together take a while and produce about 34 MB, so that matters.
#
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)

WANT_REFERENCE=0
if [ "$1" = "--reference" ]; then WANT_REFERENCE=1; shift; fi

DATA_DIR="${1:-$ROOT/SDLPoP/data}"
WORK_DIR="${2:-$ROOT/generated}"

# ---------------------------------------------------------------- the DATs

# What convert.py needs. Eight of these ship with SDLPoP itself; the rest come
# from an original copy of the game and are what you have to provide. The list
# comes from convert.py, which is the thing that reads them, so there is one
# copy of it rather than two that drift.
NEEDED=$(python3 "$HERE/convert.py" --list-dats)

missing=""
for f in $NEEDED; do
	[ -f "$DATA_DIR/$f" ] || missing="$missing $f"
done

if [ -n "$missing" ]; then
	cat >&2 <<EOF

picopop: the game's data files are missing.

  Looked in: $DATA_DIR
  Missing:  $(echo $missing)

The firmware is built from the original Prince of Persia data, which is not
distributed with this project and is not in its git history. Copy the DAT files
from your own copy of the game into:

    $DATA_DIR

Any release of PoP 1.0, 1.1, 1.3 or 1.4 for DOS will do. Then build again.

EOF
	exit 1
fi

# ------------------------------------------------------------- up to date?

mkdir -p "$WORK_DIR"
STAMP="$WORK_DIR/.resources.stamp"
FINGERPRINT=$( (cd "$DATA_DIR" && md5sum $NEEDED 2>/dev/null; \
                md5sum "$HERE/convert.py" "$HERE/datfile.py") | md5sum )

if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$FINGERPRINT" ] \
   && [ -f "$WORK_DIR/resources/resources.c" ]; then
	if [ "$WANT_REFERENCE" = 0 ] || [ -f "$WORK_DIR/resources/reference.bin" ]; then
		echo "picopop: resources are up to date"
		exit 0
	fi
fi

# ------------------------------------------------------------------- build

echo "picopop: converting $DATA_DIR -> $WORK_DIR"
( cd "$WORK_DIR" && python3 "$HERE/convert.py" "$DATA_DIR" )

if [ "$WANT_REFERENCE" = 1 ]; then
	echo "picopop: building the sprite reference"
	( cd "$WORK_DIR" && python3 "$HERE/dump_reference.py" "$DATA_DIR" )
fi

echo "$FINGERPRINT" > "$STAMP"
echo "picopop: resources are in $WORK_DIR/resources"
