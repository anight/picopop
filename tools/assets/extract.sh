#! /bin/bash
#
# Extract the resources from a set of Prince of Persia DAT files.
#
#   extract.sh <dat-dir> <out-dir>
#
# Each DAT is exported twice, and both are needed:
#
#   <NAME>.DAT       --plain:       resources under numeric names, with images
#                    decoded to BMP. This is the only source of decoded pixels,
#                    and it is also what convert.py enumerates to find them.
#
#   <NAME>.DAT.raw   --plain --raw: the resources exactly as the DAT holds
#                    them. This is what the firmware embeds and parses at
#                    runtime - the sprite-set headers, the levels, the MIDI,
#                    and the digi sounds, whose 8-bit samples the mixer
#                    resamples straight out of flash.
#
# Neither can be derived from the other. For a digi sound, --plain produces a
# RIFF/WAVE file with a 44-byte header, while --raw gives the original bytes
# that determine_wave_version() reads; for a sprite, --plain decodes the image
# and --raw leaves it compressed. Deriving one from the other would mean
# reimplementing PR's codec, which is the decode chain this port exists to
# delete from the firmware.
#
# A third export, --export without --plain, writes the same content as the
# first under human-readable names. Nothing reads it - checked against
# convert.py, dump_reference.py and preview.py - and dropping it leaves the
# generated C byte-identical while saving 5 MB and a third of the extraction.
#
# This does not write into the source tree: everything lands under <out-dir>.
#
set -e

DAT_DIR="${1:?usage: extract.sh <dat-dir> <out-dir>}"
OUT_DIR="${2:?usage: extract.sh <dat-dir> <out-dir>}"
PR="${PR_TOOL:?PR_TOOL must point at the built pr executable}"

# pr looks for resources.xml in the working directory unless told otherwise.
# Pointing at it explicitly is what lets the extraction run anywhere rather
# than only from PR/src/bin, which is where it used to have to live.
RESXML="${PR_RESOURCE_XML:-$(dirname "$PR")/resources.xml}"
[ -f "$RESXML" ] || { echo "extract.sh: no resources.xml at $RESXML" >&2; exit 1; }

dat_files="DIGISND1.DAT DIGISND2.DAT DIGISND3.DAT FAT.DAT GUARD1.DAT GUARD2.DAT GUARD.DAT KID.DAT LEVELS.DAT MIDISND1.DAT MIDISND2.DAT PRINCE.DAT PV.DAT SHADOW.DAT SKEL.DAT TITLE.DAT VDUNGEON.DAT VIZIER.DAT VPALACE.DAT"

DAT_DIR=$(cd "$DAT_DIR" && pwd)
mkdir -p "$OUT_DIR"
OUT_DIR=$(cd "$OUT_DIR" && pwd)
PR=$(cd "$(dirname "$PR")" && pwd)/$(basename "$PR")
RESXML=$(cd "$(dirname "$RESXML")" && pwd)/$(basename "$RESXML")

cd "$OUT_DIR"

#
# pr exits 1 even when it succeeds - its "Result: N files successfully
# exported (1)" prints the return code in those brackets, and that is a 1 on a
# clean run. So its status cannot be trusted, and under `set -e` it aborts the
# loop after the first DAT. Check the output instead: an export that produced
# no files is the real failure.
#
run_pr () {
	local out="$1"; shift
	"$PR" --resource="$RESXML" --export="$out" "$@" >/dev/null 2>&1 || true
	if [ -z "$(find "$out" -type f -print -quit 2>/dev/null)" ]; then
		echo "extract.sh: $PR produced nothing in $out" >&2
		echo "            (source: $1)" >&2
		exit 1
	fi
}

for file in $dat_files ; do
	mkdir -p "${file}" "${file}.raw"
	run_pr "${file}"      --plain "${DAT_DIR}/${file}"
	run_pr "${file}.raw"  --plain --raw "${DAT_DIR}/${file}"
done

echo "extract.sh: $(echo $dat_files | wc -w) DAT files extracted"
