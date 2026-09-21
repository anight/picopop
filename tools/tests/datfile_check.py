#! /usr/bin/env python3
"""
Check tools/assets/datfile.py against SDLPoP's own reader.

datfile.py is a port of the reader in SDLPoP/src/seg009.c. A port is worth only
as much as the check on it, so dat_decode_ref.c links the real functions out of
that file - decompr_img(), its four decompressors, calc_stride() and
conv_to_8bpp() - walks the same DAT itself, and prints what it gets. The two
programs share no code.

Three comparisons, and all of them have to pass:

    resources   the ids the index table yields. Tests the container.
    raw         every resource as bytes. Tests the checksum byte and the offsets.
    decoded     every image as 8bpp indices. Tests the codec: five compression
                methods and the bit-depth expansion.

Which resources are images is decided here and passed to the C, rather than
decided twice. That is forced: those decompressors have no bounds of their own -
the game only ever calls them on resources it already knows are images - and fed
anything else they write outside the destination. So the classifier is not what
this checks; the container walk and the codec are.

Run it through the Makefile, which builds the reference tool:

    make -C tools/tests datfile
"""

import os
import struct
import subprocess
import sys
from glob import glob

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.normpath(os.path.join(HERE, "..", "assets"))
sys.path.insert(0, ASSETS)

from datfile import Dat, decode_image, image_header, looks_like_image  # noqa: E402

CMETH = {0: "raw", 1: "RLE left-right", 2: "RLE up-down",
         3: "LZG left-right", 4: "LZG up-down"}


def read_reference(tool, path, decode_ids):
    """Run the C reader over one DAT: {id: (raw, width, height, pixels)}."""
    cmd = [tool, path] + [str(i) for i in decode_ids]
    out = subprocess.run(cmd, stdout=subprocess.PIPE, check=True).stdout

    if out[:4] != b"DATD":
        raise SystemExit(f"{tool}: unexpected output for {path}")
    pos = 4
    count, = struct.unpack_from("<I", out, pos)
    pos += 4

    res = {}
    for _ in range(count):
        rid, rawlen = struct.unpack_from("<II", out, pos)
        pos += 8
        raw = out[pos:pos + rawlen]
        pos += rawlen
        width, height, pixlen = struct.unpack_from("<III", out, pos)
        pos += 12
        pixels = out[pos:pos + pixlen]
        pos += pixlen
        res[rid] = (raw, width, height, pixels)

    if pos != len(out):
        raise SystemExit(f"{tool}: {len(out) - pos} trailing bytes for {path}")
    return res


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <reference-tool> <dat-dir>", file=sys.stderr)
        return 2
    tool, dat_dir = sys.argv[1], sys.argv[2]

    paths = sorted(glob(os.path.join(dat_dir, "*.DAT")))
    if not paths:
        print(f"datfile_check: no DAT files in {dat_dir}", file=sys.stderr)
        return 1

    n_res = raw_ok = img_ok = 0
    coverage = {}
    failures = []

    for path in paths:
        name = os.path.basename(path)
        ours = Dat(path)
        # Which resources are images is this side's judgement, and has to be:
        # the C decompressors have no bounds of their own and walk off the end
        # of the buffer when handed something that is not an image.
        images = [rid for rid in ours.ids if looks_like_image(ours.raw(rid))]
        theirs = read_reference(tool, path, images)

        if set(ours.ids) != set(theirs):
            only_us = sorted(set(ours.ids) - set(theirs))
            only_them = sorted(set(theirs) - set(ours.ids))
            failures.append(
                f"{name}: resource ids differ - ours only {only_us[:8]}, "
                f"reference only {only_them[:8]}")
            continue

        for rid in ours.ids:
            n_res += 1
            our_raw = ours.raw(rid)
            their_raw, width, height, pixels = theirs[rid]

            if our_raw == their_raw:
                raw_ok += 1
            else:
                failures.append(
                    f"{name} res{rid}: raw differs - ours {len(our_raw)} bytes, "
                    f"reference {len(their_raw)}")
                continue

            if not looks_like_image(our_raw):
                continue
            if not pixels:
                failures.append(
                    f"{name} res{rid}: we call it an image, the reference did "
                    f"not decode it")
                continue

            mw, mh, indices = decode_image(our_raw)
            _, _, depth, cmeth, _ = image_header(our_raw)
            coverage[(cmeth, depth)] = coverage.get((cmeth, depth), 0) + 1

            if (mw, mh) != (width, height):
                failures.append(
                    f"{name} res{rid}: size {mw}x{mh}, reference {width}x{height}")
            elif bytes(indices) != pixels:
                n = sum(1 for a, b in zip(indices, pixels) if a != b)
                failures.append(
                    f"{name} res{rid}: {n} of {mw * mh} pixels differ "
                    f"({CMETH.get(cmeth, cmeth)}, {depth}bpp)")
            else:
                img_ok += 1

    print(f"{raw_ok} of {n_res} resources read identically")
    print(f"{img_ok} images decoded identically")
    print("compression methods exercised:")
    for cmeth in sorted({c for c, _ in coverage}):
        at = ", ".join(f"{n} at {d}bpp"
                       for (c, d), n in sorted(coverage.items()) if c == cmeth)
        print(f"  {cmeth} {CMETH.get(cmeth, '?'):<16} {at}")

    missing = set(CMETH) - {c for c, _ in coverage}
    if missing:
        # A method the data never reaches is a method this says nothing about.
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

    print("\nOK: datfile.py and SDLPoP's reader agree on every resource "
          "and every pixel")
    return 0


if __name__ == "__main__":
    sys.exit(main())
