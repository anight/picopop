#! /usr/bin/env python3
"""
Render the converted sprites and the global palette as PNGs, so the conversion
can be looked at rather than only asserted about.

verify_resources.c already proves the generated C reproduces the original
sprites pixel for pixel. This is for the other question - whether the palette
layout is sane - which is much easier to answer by eye: one sheet per sprite
set, drawn through the global palette with that set's row loaded, plus a chart
of the 256 entries showing which rows are occupied and by what.

    python3 preview.py     ->  resources/preview/*.png
"""

import os

from PIL import Image, ImageDraw

from convert import (IMAGES_FROM, PALETTE_ROWS, SNAPSHOT_SETS,
                     VGA_PALETTE_DEFAULT, read_bmp, read_shpl)

OUT = "resources/preview"
CELL = 20        # palette chart cell size
PAD = 4          # gap between sprites on a contact sheet


def guard_palette(index=0):
    raw = open("PRINCE.DAT.raw/res00010.bin", "rb").read()
    base = index * 48
    return [(raw[base + i * 3] * 4, raw[base + i * 3 + 1] * 4,
             raw[base + i * 3 + 2] * 4) for i in range(16)]


def set_palette(datfile, shpl_id):
    _, palette = read_shpl(datfile, shpl_id)
    rgb = [(r * 4, g * 4, b * 4) for r, g, b in palette]
    # Guards are coloured at runtime from PRINCE.DAT res10, not by their shpl.
    if all(c == (0, 0, 0) for c in rgb):
        rgb = guard_palette(0)
    return rgb


def build_global_palette():
    """The 256 entries with one variant chosen per shared row."""
    pal = [(0, 0, 0)] * 256
    labels = {}
    for i, (r, g, b) in enumerate(VGA_PALETTE_DEFAULT):
        pal[i] = (r * 4, g * 4, b * 4)
    labels[0] = "base VGA"
    for key in SNAPSHOT_SETS:
        if key not in PALETTE_ROWS or not os.path.isdir(key[0]):
            continue
        row, what, _ = PALETTE_ROWS[key]
        for i, c in enumerate(set_palette(*key)):
            pal[row * 16 + i] = c
        labels[row] = what
    labels.setdefault(1, "font (drawn mono)")
    return pal, labels


def draw_palette_chart(pal, labels):
    w, h = CELL * 16 + 200, CELL * 16 + 20
    img = Image.new("RGB", (w, h), (24, 24, 28))
    d = ImageDraw.Draw(img)
    for row in range(16):
        y = 10 + row * CELL
        for col in range(16):
            x = 10 + col * CELL
            d.rectangle([x, y, x + CELL - 2, y + CELL - 2], fill=pal[row * 16 + col])
        text = f"{row * 16:3d}  {labels.get(row, '(unused)')}"
        d.text((CELL * 16 + 18, y + 5), text, fill=(210, 210, 215))
    img.save(f"{OUT}/palette.png")
    return img.size


def contact_sheet(datfile, shpl_id, pal):
    row, what, _ = PALETTE_ROWS[(datfile, shpl_id)]
    img_dat = IMAGES_FROM.get((datfile, shpl_id), datfile)
    n_images, _ = read_shpl(datfile, shpl_id)

    sprites = []
    for i in range(1, n_images + 1):
        path = f"{img_dat}/res{shpl_id + i:05d}.bin"
        if not os.path.exists(path) or open(path, "rb").read(2) != b"BM":
            continue
        w, h, rows, _ = read_bmp(path)
        sprites.append((w, h, rows))
    if not sprites:
        return None

    cols = 16
    cw = max(s[0] for s in sprites) + PAD
    ch = max(s[1] for s in sprites) + PAD
    rows_n = (len(sprites) + cols - 1) // cols

    img = Image.new("RGB", (cols * cw, rows_n * ch + 16), (28, 28, 34))
    d = ImageDraw.Draw(img)
    d.text((4, 3), f"{datfile} res{shpl_id} - {what} - palette row {row} "
                   f"(indices {row*16}..{row*16+15}) - {len(sprites)} sprites",
           fill=(210, 210, 215))

    for n, (w, h, pixels) in enumerate(sprites):
        ox = (n % cols) * cw + PAD // 2
        oy = (n // cols) * ch + 16 + PAD // 2
        for y in range(h):
            for x in range(w):
                v = pixels[y][x]
                if v == 0:
                    continue          # transparent
                img.putpixel((ox + x, oy + y), pal[row * 16 + v])

    name = f"{OUT}/{datfile.replace('.DAT','').lower()}_{shpl_id}.png"
    img.save(name)
    return name


def main():
    os.makedirs(OUT, exist_ok=True)
    pal, labels = build_global_palette()

    size = draw_palette_chart(pal, labels)
    print(f"{OUT}/palette.png  {size[0]}x{size[1]}")

    for (datfile, shpl_id) in sorted(PALETTE_ROWS):
        if not os.path.isdir(datfile):
            continue
        name = contact_sheet(datfile, shpl_id, pal)
        if name:
            print(f"{name}")


if __name__ == "__main__":
    main()
