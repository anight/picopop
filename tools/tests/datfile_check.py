#! /usr/bin/env python3
"""
Check tools/assets/datfile.py against PR, resource by resource and pixel by
pixel.

datfile.py reads the DAT files directly; PR reads the same files with a codec
written by someone else, from the format documentation rather than from
SDLPoP's source. Two independent readers agreeing on every byte of every
resource and every pixel of every image is what makes it safe to take PR out of
the build.

Two comparisons, and both have to pass:

    raw       every resource, as bytes. Tests the container: the index table,
              the per-resource checksum byte, and the sizes.
    decoded   every resource PR decoded to a BMP, as 8bpp indices. Tests the
              codec: five compression methods and the depth expansion.

PR is no longer part of this project: `convert.py` reads the DAT files itself,
so nothing in the build needs it. It remains the only reader independent of
`datfile.py`, which makes this the only real check on the decoder - so run it
after changing `datfile.py`, against a PR checked out for the purpose:

    git clone https://github.com/NagyD/PR /tmp/pr && make -C /tmp/pr/src
    mkdir /tmp/extract && cd /tmp/extract
    for d in /path/to/dats/*.DAT; do
        n=$(basename "$d")
        mkdir -p "$n" "$n.raw"
        /tmp/pr/src/bin/pr --resource=/tmp/pr/src/bin/resources.xml \
                           --export="$n" --plain "$d"
        /tmp/pr/src/bin/pr --resource=/tmp/pr/src/bin/resources.xml \
                           --export="$n.raw" --plain --raw "$d"
    done

    make -C tools/tests datfile PR_EXTRACT=/tmp/extract
"""

import os
import struct
import sys
from glob import glob

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.normpath(os.path.join(HERE, "..", "assets"))
sys.path.insert(0, ASSETS)

from datfile import Dat, decode_image, image_header      # noqa: E402


def read_bmp(path):
    """Unpack a 1bpp or 4bpp BMP into (width, height, [row][col] palette indices).

    BMP rows are stored bottom-up and padded to a 4-byte boundary; both matter,
    and getting either wrong produces an image that looks almost right.
    """
    d = open(path, "rb").read()
    assert d[:2] == b"BM", f"{path}: not a BMP"
    bits_offset = struct.unpack("<I", d[10:14])[0]
    hdr_size = struct.unpack("<I", d[14:18])[0]
    width, height = struct.unpack("<ii", d[18:26])
    bpp = struct.unpack("<H", d[28:30])[0]
    assert bpp in (1, 4), f"{path}: {bpp}bpp not handled"

    pal_off = 14 + hdr_size
    n_pal = 1 << bpp
    palette = [(d[pal_off + i * 4 + 2], d[pal_off + i * 4 + 1], d[pal_off + i * 4])
               for i in range(n_pal)]

    bottom_up = height > 0
    height = abs(height)
    row_bytes = ((width * bpp + 31) // 32) * 4

    rows = []
    for y in range(height):
        base = bits_offset + y * row_bytes
        row = []
        for x in range(width):
            if bpp == 4:
                b = d[base + x // 2]
                row.append((b >> 4) if x % 2 == 0 else (b & 0x0F))
            else:
                b = d[base + x // 8]
                row.append((b >> (7 - x % 8)) & 1)
        rows.append(row)
    if bottom_up:
        rows.reverse()
    return width, height, rows, palette



CMETH = {0: "raw", 1: "RLE left-right", 2: "RLE up-down",
         3: "LZG left-right", 4: "LZG up-down"}


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <dat-dir> <pr-extraction-dir>", file=sys.stderr)
        return 2
    dat_dir, pr_dir = sys.argv[1], sys.argv[2]

    dats = sorted(glob(os.path.join(dat_dir, "*.DAT")))
    if not dats:
        print(f"datfile_check: no DAT files in {dat_dir}", file=sys.stderr)
        return 1

    # A missing extraction is a failure, not a skip: a test that reports success
    # when it did not run is worse than one that is simply absent.
    have_pr = [d for d in dats
               if os.path.isdir(os.path.join(pr_dir, os.path.basename(d) + ".raw"))]
    if not have_pr:
        print(f"datfile_check: no PR extraction under {pr_dir}\n"
              f"  see the header of this file for how to produce one",
              file=sys.stderr)
        return 1

    raw_ok = raw_bad = 0
    img_ok = img_bad = 0
    coverage = {}
    failures = []

    for path in dats:
        name = os.path.basename(path)
        rawdir = os.path.join(pr_dir, name + ".raw")
        plaindir = os.path.join(pr_dir, name)
        if not os.path.isdir(rawdir):
            continue

        dat = Dat(path)

        for rid in dat.ids:
            ours = dat.raw(rid)

            # --- the container -------------------------------------------
            f = os.path.join(rawdir, f"res{rid:05d}.bin")
            if not os.path.exists(f):
                failures.append(f"{name} res{rid}: PR exported no raw resource")
                raw_bad += 1
            else:
                with open(f, "rb") as fh:
                    theirs = fh.read()
                if theirs == ours:
                    raw_ok += 1
                else:
                    raw_bad += 1
                    failures.append(
                        f"{name} res{rid}: raw differs - ours {len(ours)} bytes, "
                        f"PR {len(theirs)}")

            # --- the codec -----------------------------------------------
            f = os.path.join(plaindir, f"res{rid:05d}.bin")
            if not os.path.exists(f):
                continue
            with open(f, "rb") as fh:
                if fh.read(2) != b"BM":
                    continue

            width, height, rows, _palette = read_bmp(f)
            try:
                mw, mh, pixels = decode_image(ours)
                _, _, depth, cmeth, _ = image_header(ours)
            except Exception as exc:                      # noqa: BLE001
                img_bad += 1
                failures.append(f"{name} res{rid}: decode raised {exc!r}")
                continue

            coverage[(cmeth, depth)] = coverage.get((cmeth, depth), 0) + 1
            theirs = bytes(b for row in rows for b in row)

            if (mw, mh) != (width, height):
                img_bad += 1
                failures.append(f"{name} res{rid}: size {mw}x{mh}, PR {width}x{height}")
            elif bytes(pixels) != theirs:
                n = sum(1 for a, b in zip(pixels, theirs) if a != b)
                img_bad += 1
                failures.append(
                    f"{name} res{rid}: {n} of {mw * mh} pixels differ "
                    f"({CMETH.get(cmeth, cmeth)}, {depth}bpp)")
            else:
                img_ok += 1

    print(f"{raw_ok} resources read identically, {raw_bad} not")
    print(f"{img_ok} images decoded identically, {img_bad} not")

    print("compression methods exercised:")
    for cmeth in sorted({c for c, _ in coverage}):
        at = ", ".join(f"{n} at {d}bpp"
                       for (c, d), n in sorted(coverage.items()) if c == cmeth)
        print(f"  {cmeth} {CMETH.get(cmeth, '?'):<16} {at}")

    missing = set(CMETH) - {c for c, _ in coverage}
    if missing:
        # Every method the codec implements has to be reached by the data, or
        # agreement on the rest says nothing about the one that was not.
        print("FAIL: never exercised: "
              + ", ".join(f"{m} ({CMETH[m]})" for m in sorted(missing)),
              file=sys.stderr)
        return 1

    if failures:
        print(f"\nFAIL: {len(failures)} mismatch(es):", file=sys.stderr)
        for line in failures[:20]:
            print("  " + line, file=sys.stderr)
        if len(failures) > 20:
            print(f"  ... and {len(failures) - 20} more", file=sys.stderr)
        return 1

    print("\nOK: datfile.py and PR agree on every resource and every pixel")
    return 0


if __name__ == "__main__":
    sys.exit(main())
