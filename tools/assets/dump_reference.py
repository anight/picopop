#! /usr/bin/env python3
"""
Dump what every sprite is *supposed* to look like, for verify_resources.c to
check the generated C against.

The reference is built from the original BMPs the extractor produced, coloured
with their sprite set's own 16-colour palette out of the shpl - i.e. from the
data, by a completely different path than convert.py takes. If the generated
8bpp-plus-global-palette form reproduces this pixel for pixel, then the row
offsets, the palette rows, the bit unpacking and the row order are all right.

Note the 1bpp images are coloured from the shpl here regardless of what pr put
in their BMP header, because the header is pr's presentation choice and the
indices are what the game actually uses.

Writes reference.bin:

    u32 count
    repeated: u32 shpl_id, u32 image_index, u32 w, u32 h, char datfile[16],
              then w*h RGB triples

An image_index of 0xFFFFFFFF means the record is an optional-graphics image
(convert.py's OPTGRAF_BASE): shpl_id is then its own resource id rather than a
set index, because those images are not reachable through any set's image list.
The verifier looks them up by resource id instead.
"""

import os
import struct
import sys
from glob import glob

from convert import (IMAGES_FROM, OPTGRAF_BASE, OPTGRAF_PALETTE_SET,
                     PALETTE_ROWS, read_bmp, read_shpl)

OUT = "resources/reference.bin"


def main():
    os.makedirs("resources", exist_ok=True)
    records = []

    for (datfile, shpl_id) in sorted(PALETTE_ROWS):
        if not os.path.isdir(datfile):
            continue
        n_images, palette = read_shpl(datfile, shpl_id)
        rgb = [(r * 4, g * 4, b * 4) for r, g, b in palette]

        # The guard sets' shpl palettes are entirely black - the guards get
        # their colours from PRINCE.DAT resource 10 at runtime instead. Colour
        # them with the first of those variants, or the reference would be a
        # black rectangle and the comparison would prove nothing about the
        # indices. verify_resources.c substitutes the same one.
        if all(c == (0, 0, 0) for c in rgb):
            raw = open("PRINCE.DAT.raw/res00010.bin", "rb").read()
            rgb = [(raw[i * 3] * 4, raw[i * 3 + 1] * 4, raw[i * 3 + 2] * 4)
                   for i in range(16)]

        img_dat = IMAGES_FROM.get((datfile, shpl_id), datfile)

        for i in range(1, n_images + 1):
            path = f"{img_dat}/res{shpl_id + i:05d}.bin"
            if not os.path.exists(path):
                continue
            if open(path, "rb").read(2) != b"BM":
                continue
            w, h, rows, _ = read_bmp(path)
            pixels = bytearray()
            for row in rows:
                for v in row:
                    pixels += bytes(rgb[v])
            records.append((shpl_id, i - 1, w, h, datfile, bytes(pixels)))

    # -- the optional graphics --------------------------------------------
    #
    # Not part of any set's image list, so they are recorded by resource id.
    # They take the environment set's palette, which is what the game colours
    # them with (see convert.py's OPTGRAF_BASE).

    optgraf = 0
    for datfile in sorted({d for d, _ in PALETTE_ROWS}):
        if not os.path.isdir(datfile):
            continue
        if (datfile, OPTGRAF_PALETTE_SET) not in PALETTE_ROWS:
            continue
        _, palette = read_shpl(datfile, OPTGRAF_PALETTE_SET)
        rgb = [(r * 4, g * 4, b * 4) for r, g, b in palette]
        for path in sorted(glob(f"{datfile}/res*.bin")):
            rid = int(os.path.basename(path)[3:8])
            if rid <= OPTGRAF_BASE:
                continue
            if open(path, "rb").read(2) != b"BM":
                continue
            w, h, rows, _ = read_bmp(path)
            pixels = bytearray()
            for row in rows:
                for v in row:
                    pixels += bytes(rgb[v])
            records.append((rid, 0xFFFFFFFF, w, h, datfile, bytes(pixels)))
            optgraf += 1

    with open(OUT, "wb") as f:
        f.write(struct.pack("<I", len(records)))
        for shpl_id, idx, w, h, datfile, pixels in records:
            name = datfile.encode()[:15].ljust(16, b"\0")
            f.write(struct.pack("<IIII16s", shpl_id, idx, w, h, name))
            f.write(pixels)

    print(f"{OUT}: {len(records)} sprites ({optgraf} optional graphics), "
          f"{os.path.getsize(OUT) / 1024 / 1024:.1f} MB", file=sys.stderr)


if __name__ == "__main__":
    main()
