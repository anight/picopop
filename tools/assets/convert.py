#! /usr/bin/env python3
"""
Turn the extracted DAT resources into C that a Pico can use directly.

Run extract.sh first; this reads what it produced.

Two things come out:

  * every non-image resource, embedded verbatim as it is on disk in the DAT
    (levels, sounds, MIDI, the sprite-set headers) -- unchanged from before;

  * every sprite, converted from its 4bpp-with-a-private-16-colour-palette form
    into an 8bpp image indexed into ONE global 256-colour palette, together with
    a `const SDL_Surface` describing it. Pixels and surfaces both end up in
    flash, so the game blits straight out of XIP: no decode step, no copy into
    RAM, no allocation.

The global palette is not invented here. The DOS original ran in VGA mode 13h
with a single 256-entry hardware palette, and each sprite set's 16 colours were
loaded into one *row* of 16 within it. The game still says which row: every
call site passes a `palette_bits` mask, e.g.

    seg000.c:2135  load_chtab_from_file(id_chtab_2_kid, 400, "KID.DAT", 1<<7)

meaning the Kid's palette occupies row 7, i.e. global indices 112..127. So a
Kid sprite pixel with value v becomes global index 112 + v, and colour-key
transparency (value 0) becomes index 112.

Rows 5, 6, 8 and 10 hold more than one set -- dungeon vs palace tiles, the five
guard types, the two cutscene sets. Those rows are rewritten when the set is
loaded, which is why the palette lives in the CLUT rather than in the pixels:
one `SDL_SetPaletteColors` call swaps a set's colours without touching a single
pixel. It is also what makes the game's fades free.

Output lands in resources/ as resources.{h,c,inc}, to be copied into
SDLPoP/src/.
"""

from glob import glob
import collections
import os
import struct
import subprocess
import sys

OUT_DIR = "resources"

# ---------------------------------------------------------------------------
# Which row of 16 each sprite set's palette occupies.
#
# Taken from the game, not chosen here. Every entry cites the line that decides
# it, so this table can be re-checked against the source rather than trusted.
# ---------------------------------------------------------------------------
PALETTE_ROWS = {
    # (DAT file, shpl resource id): (row, what it is, where the game says so)
    ("PRINCE.DAT",   700): (2,  "sword",                 "seg000.c:171  load_sprites_from_file(700, 1<<2)"),
    ("PRINCE.DAT",   150): (3,  "flame/sword/potion",    "seg000.c:173  load_sprites_from_file(150, 1<<3)"),
    ("VDUNGEON.DAT", 200): (5,  "environment (dungeon)", "seg000.c:1111 load_chtab_from_file(..., 200, ..., 1<<5)"),
    ("VPALACE.DAT",  200): (5,  "environment (palace)",  "seg000.c:1111 load_chtab_from_file(..., 200, ..., 1<<5)"),
    ("VDUNGEON.DAT", 360): (6,  "walls (dungeon)",       "seg000.c:1124 load_chtab_from_file(..., 360, ..., 1<<6)"),
    ("VPALACE.DAT",  360): (6,  "walls (palace)",        "seg000.c:1124 load_chtab_from_file(..., 360, ..., 1<<6)"),
    ("KID.DAT",      400): (7,  "kid",                   "seg000.c:2135 load_chtab_from_file(..., 400, 'KID.DAT', 1<<7)"),
    # GUARD.DAT holds images 751..784 but no palette of its own. For a normal
    # guard the game opens GUARD1.DAT or GUARD2.DAT first (seg000.c:1116) so
    # that resource 750 resolves to one of their palettes, then loads the set
    # from GUARD.DAT - the DAT chain supplies the images. Both variants sit in
    # row 8, so the pixels are identical and are emitted once.
    ("GUARD1.DAT",   750): (8,  "guard (colour 1)",      "seg000.c:1116/1118 palette here, images from GUARD.DAT"),
    ("GUARD2.DAT",   750): (8,  "guard (colour 2)",      "seg000.c:1116/1118 palette here, images from GUARD.DAT"),
    ("FAT.DAT",      750): (8,  "fat guard",             "seg000.c:1118"),
    ("SKEL.DAT",     750): (8,  "skeleton",              "seg000.c:1118"),
    ("VIZIER.DAT",   750): (8,  "vizier",                "seg000.c:1118"),
    ("SHADOW.DAT",   750): (8,  "shadow",                "seg000.c:1118"),
    ("PV.DAT",       800): (9,  "princess in story",     "seg001.c:674  load_chtab_from_file(..., 800, 'PV.DAT', 1<<9)"),
    ("PV.DAT",       850): (10, "cutscene set A",        "seg001.c:675  50*which_imgs + 850, 1<<10"),
    ("PV.DAT",       900): (10, "cutscene set B",        "seg001.c:675  50*which_imgs + 850, 1<<10"),
    ("TITLE.DAT",     40): (11, "title (main)",          "seg000.c:2302 load_sprites_from_file(40, 1<<11)"),
    ("TITLE.DAT",     50): (12, "title (presents)",      "seg000.c:2303 load_sprites_from_file(50, 1<<12)"),
    ("PV.DAT",       950): (13, "princess room",         "seg001.c:663  load_chtab_from_file(..., 950, 'PV.DAT', 1<<13)"),
    ("PV.DAT",       980): (14, "princess bed",          "seg001.c:664  load_chtab_from_file(..., 980, 'PV.DAT', 1<<14)"),
}

# Row 0 is the base VGA palette, the standard EGA sixteen. Values are 0..63 as
# the hardware wants them; SDLPoP has the same table as VGA_PALETTE_DEFAULT in
# data.h:127. Row 1 is the font, which is drawn mono (the blitter substitutes a
# colour per call) and so needs no entry here. Rows 4 and 15 are unused.
VGA_PALETTE_DEFAULT = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0x2A), (0x00, 0x2A, 0x00), (0x00, 0x2A, 0x2A),
    (0x2A, 0x00, 0x00), (0x2A, 0x00, 0x2A), (0x2A, 0x15, 0x00), (0x2A, 0x2A, 0x2A),
    (0x15, 0x15, 0x15), (0x15, 0x15, 0x3F), (0x15, 0x3F, 0x15), (0x15, 0x3F, 0x3F),
    (0x3F, 0x15, 0x15), (0x3F, 0x15, 0x3F), (0x3F, 0x3F, 0x15), (0x3F, 0x3F, 0x3F),
]

# When the global palette is snapshotted for previews, these are the variants
# picked for the rows that have more than one occupant.
SNAPSHOT_SETS = [
    ("VDUNGEON.DAT", 200), ("VDUNGEON.DAT", 360), ("KID.DAT", 400),
    ("GUARD1.DAT", 750), ("PRINCE.DAT", 700), ("PRINCE.DAT", 150),
    ("PV.DAT", 800), ("PV.DAT", 850), ("TITLE.DAT", 40), ("TITLE.DAT", 50),
    ("PV.DAT", 950), ("PV.DAT", 980),
]

# Sprite sets whose palette lives in one DAT and whose images live in another,
# reached through the game's DAT chain rather than by being in the same file.
IMAGES_FROM = {
    ("GUARD1.DAT", 750): "GUARD.DAT",
    ("GUARD2.DAT", 750): "GUARD.DAT",
}

# ---------------------------------------------------------------------------
# The optional graphics.
#
# VDUNGEON.DAT and VPALACE.DAT carry extra tile variants above resource 1200
# that no shpl covers. They are still sprites, and the game still draws them:
#
#     seg000.c:1682 load_more_opt_graf()
#         area = resource 200's shpl;  area.palette.row_bits = 0x20
#         load_one_optgraf(chtab_addrs[id_chtab_6_environment], &area.palette,
#                          1200, min-1, max-1)
#
# It overlays them onto the environment chtab at level load, colouring them
# with resource 200's palette - row_bits 0x20 is row 5, the same row the
# environment set uses. So they take that set's row, and check_palette() below
# confirms each one really does carry that palette rather than another.
#
# These were previously left as raw bytes, which meant the overlay silently did
# nothing and levels drew with the base tile set only.
OPTGRAF_BASE = 1200
OPTGRAF_PALETTE_SET = 200


# ---------------------------------------------------------------------------
# reading what extract.sh produced
# ---------------------------------------------------------------------------

def res_id(path):
    return int(os.path.basename(path)[3:-4])


def dat_of(path):
    return os.path.dirname(path)


def c_name(datfile, rid):
    return f"{datfile.lower().replace('.', '_')}_res{rid:05d}"


def read_shpl(datfile, rid):
    """A sprite-set header: n_images, then a 16-colour palette in 6-bit RGB."""
    raw = open(f"{datfile}.raw/res{rid:05d}.bin", "rb").read()
    assert len(raw) == 100, f"{datfile} res{rid}: shpl is {len(raw)} bytes, expected 100"
    n_images = raw[0]
    # skip row_bits (2) and n_colors (1); the game overrides row_bits anyway
    palette = [(raw[4 + i * 3], raw[5 + i * 3], raw[6 + i * 3]) for i in range(16)]
    return n_images, palette


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


# ---------------------------------------------------------------------------
# emitting C
# ---------------------------------------------------------------------------

def check_palette(path, owner, s, bmp_pal, stats):
    """Confirm a BMP's indices really do index its sprite set's palette.

    That assumption is what the whole row-offset scheme rests on: if pr had
    renumbered anything, baking row*16 into the pixels would silently produce
    the wrong colours.

    Three cases:

      * 4bpp images from the set's own DAT carry the set's palette verbatim, so
        compare it outright;
      * images that live in a different DAT than their palette (GUARD.DAT's,
        whose palettes are in GUARD1/GUARD2) got pr's default sixteen, because
        pr had no shpl to colour them with - nothing to compare against;
      * depth-1 images are inconsistent. Their pixel values are 0/1 into the
        set's palette like anything else, but pr writes either the set's
        colours 0 and 1 (13 of them) or plain black and white (23). That is a
        presentation choice in the exporter and says nothing about the indices.
        Accept either and reject anything else, so a real renumbering is still
        caught.
    """
    want = [(r * 4, g * 4, b * 4) for r, g, b in s["palette"]]

    if s["img_dat"] != owner[0]:
        stats["foreign"] += 1
    elif len(bmp_pal) == 16:
        assert bmp_pal == want, (
            f"{path}: BMP palette does not match {owner[0]} res{owner[1]}'s "
            f"shpl palette - the row offset would be wrong")
    else:
        assert bmp_pal in (want[:len(bmp_pal)], [(0, 0, 0), (255, 255, 255)]), (
            f"{path}: unexpected {len(bmp_pal)}-colour palette {bmp_pal}")
        stats["mono"] += 1


def emit_bytes(fh, name, data):
    fh.write(f"static const unsigned char {name}[] = {{\n")
    for i in range(0, len(data), 16):
        fh.write("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",\n")
    fh.write("};\n")
    fh.write(f"static const unsigned int {name}_len = {len(data)};\n")


def emit_surface(fh, name, width, height, colorkey):
    fh.write(
        f"static const SDL_Surface {name}_surface = {{\n"
        f"  .flags = PSDL_SURF_FLASH, .format = &psdl_pixel_format,\n"
        f"  .w = {width}, .h = {height}, .pitch = {width},\n"
        f"  .pixels = (void *)(uintptr_t){name},\n"
        f"  .clip_rect = {{ 0, 0, {width}, {height} }}, .refcount = 1,\n"
        f"  .colorkey = {colorkey}, .has_colorkey = SDL_TRUE,\n"
        f"  .alpha_mod = SDL_ALPHA_OPAQUE, .blend_mode = SDL_BLENDMODE_NONE,\n"
        f"}};\n")


# ---------------------------------------------------------------------------

def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    # -- work out which images belong to which sprite set ------------------
    #
    # A sprite set is a shpl resource at id N followed by n_images images at
    # N+1..N+n_images. Outside those runs there is one other group of real
    # sprites: the optional graphics above resource 1200, which no shpl covers
    # but load_more_opt_graf() does load - see OPTGRAF_BASE above.

    sets = {}            # (datfile, shpl_id) -> dict
    image_owner = {}     # (datfile, rid) -> (datfile, shpl_id)
    stats_optgraf = collections.Counter()

    for path in sorted(glob("*.DAT/*.bin")):
        if open(path, "rb").read(4) != b"JASC":
            continue
        datfile, rid = dat_of(path), res_id(path)
        raw = f"{datfile}.raw/res{rid:05d}.bin"
        if not os.path.exists(raw) or os.path.getsize(raw) != 100:
            continue

        key = (datfile, rid)
        if key not in PALETTE_ROWS:
            print(f"warning: {datfile} res{rid} is a sprite set with no known "
                  f"palette row; its images stay raw", file=sys.stderr)
            continue

        n_images, palette = read_shpl(datfile, rid)
        row, what, cite = PALETTE_ROWS[key]
        img_dat = IMAGES_FROM.get(key, datfile)
        sets[key] = dict(row=row, what=what, cite=cite, palette=palette,
                         n_images=n_images, images=[None] * n_images,
                         img_dat=img_dat)

        for i in range(1, n_images + 1):
            res = (img_dat, rid + i)
            prev = image_owner.get(res)
            if prev is not None:
                # Two sets sharing one set of images (the guard colour
                # variants). They must agree on the row, or one of them would
                # be reading its colours out of the other's sixteen.
                assert sets[prev]["row"] == row, (
                    f"{res} claimed by {prev} (row {sets[prev]['row']}) and "
                    f"{key} (row {row})")
            else:
                image_owner[res] = key

    # -- the optional graphics --------------------------------------------
    #
    # Assigned to their own DAT's environment set, whose palette row the game
    # colours them with. They are deliberately *not* added to that set's image
    # list: the game patches them in by index at level load rather than
    # reaching them through the set, and the set's list is indexed
    # shpl_id+1+i, which never reaches 1200.

    for path in sorted(glob("*.DAT/*.bin")):
        datfile, rid = dat_of(path), res_id(path)
        if rid <= OPTGRAF_BASE:
            continue
        if open(path, "rb").read(2) != b"BM":
            continue
        env = (datfile, OPTGRAF_PALETTE_SET)
        if env not in sets:
            print(f"warning: {datfile} res{rid} is an optional-graphics image "
                  f"but {datfile} has no res{OPTGRAF_PALETTE_SET} sprite set",
                  file=sys.stderr)
            continue
        assert (datfile, rid) not in image_owner, \
            f"{datfile} res{rid} claimed both by a shpl run and as optgraf"
        image_owner[(datfile, rid)] = env
        stats_optgraf[datfile] += 1

    # -- convert ----------------------------------------------------------

    inc = open(f"{OUT_DIR}/resources.inc", "w")
    inc.write("\n/* DO NOT EDIT! This file is generated by convert.py */\n\n")

    datfiles = collections.OrderedDict()
    stats = collections.Counter()
    palette_bytes = 0
    checked = 0

    surface_of = {}   # (datfile, rid) -> C name of the generated surface

    for path in sorted(glob("*.DAT/*.bin")):
        datfile, rid = dat_of(path), res_id(path)
        name = c_name(datfile, rid)
        entry = dict(rid=rid, name=name, kind="raw", surface=None)

        owner = image_owner.get((datfile, rid))
        is_bmp = open(path, "rb").read(2) == b"BM"

        if owner is not None and is_bmp:
            s = sets[owner]
            base = s["row"] * 16
            width, height, rows, bmp_pal = read_bmp(path)
            check_palette(path, owner, s, bmp_pal, stats)
            checked += 1

            pixels = bytes(base + v for row in rows for v in row)
            emit_bytes(inc, name, pixels)
            emit_surface(inc, name, width, height, base)
            inc.write("\n")

            entry.update(kind="img", surface=f"{name}_surface")
            surface_of[(datfile, rid)] = f"&{name}_surface"
            stats["images"] += 1
            stats["image_bytes"] += len(pixels)
        else:
            # Everything else goes in exactly as the DAT holds it.
            raw = open(f"{datfile}.raw/res{rid:05d}.bin", "rb").read()
            emit_bytes(inc, name, raw)
            inc.write("\n")
            stats["raw"] += 1
            stats["raw_bytes"] += len(raw)

        datfiles.setdefault(datfile, []).append(entry)

    # Fill in each set's image list by lookup rather than during conversion, so
    # that two sets sharing one set of images (the guard colour variants, whose
    # pixels both live in GUARD.DAT) each get a complete list.
    for (datfile, shpl_id), s in sets.items():
        for i in range(s["n_images"]):
            s["images"][i] = surface_of.get((s["img_dat"], shpl_id + 1 + i))

    inc.close()

    # -- nothing that is an image may be left as raw bytes -----------------
    #
    # This is the check that was missing. The optional graphics sat as raw
    # resources for as long as they did because nothing ever asked whether a
    # BMP had come out the other side as a surface; the game just drew nothing
    # and no one could see why. Fail the build instead.

    unconverted = []
    for path in sorted(glob("*.DAT/*.bin")):
        datfile, rid = dat_of(path), res_id(path)
        if open(path, "rb").read(2) != b"BM":
            continue
        if (datfile, rid) not in surface_of:
            unconverted.append(f"{datfile} res{rid}")
    assert not unconverted, (
        "these image resources were not converted to surfaces, so the game "
        "would find nothing to draw:\n  " + "\n  ".join(unconverted))

    # -- headers ----------------------------------------------------------

    with open(f"{OUT_DIR}/resources.h", "w") as h:
        h.write("""
/* DO NOT EDIT! This file is generated by convert.py */

#ifndef _RESOURCES_H_
#define _RESOURCES_H_

#include <stddef.h>

#include "SDL2/SDL.h"

/*
 * One resource out of a DAT file.
 *
 * `data`/`data_len` are the bytes as they appear in the DAT, for everything the
 * game still parses at runtime: levels, sounds, MIDI, sprite-set headers.
 *
 * `surface` is non-NULL for sprites, which are not parsed at runtime at all.
 * They were decoded at build time into 8bpp indexed into the global palette and
 * live in flash along with the SDL_Surface describing them, so the game blits
 * them straight out of XIP. For those, `data`/`data_len` describe the pixels.
 */
struct resource_s {
    int resource_id;
    const char *type;              /* "raw" or "img" */
    const unsigned char *data;
    unsigned data_len;
    const SDL_Surface *surface;    /* NULL unless type is "img" */
};

struct datfile_s {
    const char *filename;
    /* const-qualified *pointers*, not just pointees: without the inner const
     * GCC treats the compound literal below as writable and puts all nineteen
     * of these arrays in .data, i.e. 4.3 KB of RAM on a board that has 264. */
    const struct resource_s *const *resources;
    unsigned resources_count;
};

/*
 * A sprite set: what used to be a shpl resource plus the images following it.
 *
 * `palette_row` says which row of sixteen in the global 256-colour palette this
 * set's colours occupy, and every pixel in `images` already has row*16 added
 * to it. Loading the set means writing `palette` to the CLUT at row*16 - which
 * is all `SDL_SetPaletteColors` does - and using `images` directly.
 *
 * Rows 5, 6, 8 and 10 are shared by several sets (dungeon vs palace, the guard
 * types, the two cutscene sets); whichever was loaded last owns those sixteen
 * CLUT entries. `colorkey` is row*16, the transparent index for this set.
 *
 * An entry in `images` is NULL where the DAT has an empty image, which the
 * original sprite tables do use as padding.
 */
struct sprite_set_s {
    const char *datfile;
    int shpl_resource_id;
    unsigned char palette_row;
    unsigned char colorkey;
    unsigned char palette[16][3];  /* 8-bit RGB, scaled up from the DAT's 6-bit */
    unsigned n_images;
    const SDL_Surface *const *images;
    const char *description;
};

extern const struct datfile_s datfiles[];
extern const unsigned datfiles_count;

extern const struct sprite_set_s sprite_sets[];
extern const unsigned sprite_sets_count;

/* Row 0 of the global palette: the base VGA sixteen, 8-bit RGB. */
extern const unsigned char resources_base_palette[16][3];

/*
 * The guard colours, from PRINCE.DAT resource 10.
 *
 * Both GUARD1.DAT's and GUARD2.DAT's sprite-set palettes are entirely black:
 * the guards are not coloured by their shpl at all. The game picks one of these
 * variants per room (level.guards_color) and writes it over row 8. Without
 * this, every guard renders as a silhouette.
 */
extern const unsigned char resources_guard_palettes[][16][3];
extern const unsigned resources_guard_palette_count;

/* A whole 256-entry palette with one variant chosen for each shared row.
 * Useful for previews and as a sane starting CLUT before any set is loaded. */
extern const unsigned char resources_default_palette[256][3];

/* Look up a sprite set by the DAT it came from and its shpl resource id. */
const struct sprite_set_s *resources_find_sprite_set(const char *datfile, int shpl_id);

#endif /* _RESOURCES_H_ */
""")

    # -- the C file -------------------------------------------------------

    with open(f"{OUT_DIR}/resources.c", "w") as c:
        c.write("\n/* DO NOT EDIT! This file is generated by convert.py */\n\n")
        c.write('#include <string.h>\n\n#include "resources.h"\n#include "resources.inc"\n\n')

        # sprite sets
        for (datfile, shpl_id), s in sorted(sets.items()):
            arr = c_name(datfile, shpl_id) + "_images"
            c.write(f"static const SDL_Surface *const {arr}[] = {{\n")
            for i in range(0, len(s["images"]), 4):
                chunk = [x if x else "NULL" for x in s["images"][i:i + 4]]
                c.write("    " + ", ".join(chunk) + ",\n")
            c.write("};\n\n")

        c.write("const struct sprite_set_s sprite_sets[] = {\n")
        for (datfile, shpl_id), s in sorted(sets.items()):
            arr = c_name(datfile, shpl_id) + "_images"
            pal = ", ".join("{%d,%d,%d}" % (r * 4, g * 4, b * 4) for r, g, b in s["palette"])
            n_present = sum(1 for x in s["images"] if x)
            c.write(f"    /* {s['what']}: row {s['row']} = indices "
                    f"{s['row']*16}..{s['row']*16+15}\n"
                    f"       {s['cite']} */\n")
            c.write(f"    {{ .datfile = \"{datfile}\", .shpl_resource_id = {shpl_id},\n"
                    f"      .palette_row = {s['row']}, .colorkey = {s['row'] * 16},\n"
                    f"      .palette = {{ {pal} }},\n"
                    f"      .n_images = {s['n_images']}, .images = {arr},\n"
                    f"      .description = \"{s['what']}\" }},\n")
            stats["sets"] += 1
            stats["set_images_present"] += n_present
        c.write("};\n\n")
        c.write(f"const unsigned sprite_sets_count = {len(sets)};\n\n")

        # The guard sprite set's own palette is all zeros in both GUARD1.DAT
        # and GUARD2.DAT - the guards' colours do not come from a shpl at all.
        # The game loads PRINCE.DAT resource 10 (seg000.c:161) and picks one of
        # its seven 16-colour variants per room, writing it over row 8 with
        # set_chtab_palette (seg003.c:257). Emit them so that is possible.
        guard_pals = []
        if os.path.exists("PRINCE.DAT.raw/res00010.bin"):
            raw = open("PRINCE.DAT.raw/res00010.bin", "rb").read()
            for p in range(len(raw) // 48):
                guard_pals.append([(raw[p * 48 + i * 3] * 4,
                                    raw[p * 48 + i * 3 + 1] * 4,
                                    raw[p * 48 + i * 3 + 2] * 4) for i in range(16)])
        c.write(f"const unsigned resources_guard_palette_count = {len(guard_pals)};\n")
        c.write("const unsigned char resources_guard_palettes"
                f"[{max(len(guard_pals), 1)}][16][3] = {{\n")
        for gp in guard_pals:
            c.write("    { " + ", ".join("{%d,%d,%d}" % v for v in gp) + " },\n")
        c.write("};\n\n")

        # palettes
        c.write("const unsigned char resources_base_palette[16][3] = {\n    ")
        c.write(", ".join("{%d,%d,%d}" % (r * 4, g * 4, b * 4) for r, g, b in VGA_PALETTE_DEFAULT))
        c.write("\n};\n\n")

        snapshot = [(0, 0, 0)] * 256
        for i, (r, g, b) in enumerate(VGA_PALETTE_DEFAULT):
            snapshot[i] = (r * 4, g * 4, b * 4)
        for key in SNAPSHOT_SETS:
            if key not in sets:
                continue
            st = sets[key]
            for i, (r, g, b) in enumerate(st["palette"]):
                snapshot[st["row"] * 16 + i] = (r * 4, g * 4, b * 4)
        # Row 8 would be black from the guard shpl, so show guard colour 1.
        if guard_pals:
            for i, v in enumerate(guard_pals[0]):
                snapshot[8 * 16 + i] = v
        palette_bytes = 256 * 3

        c.write("const unsigned char resources_default_palette[256][3] = {\n")
        for i in range(0, 256, 8):
            c.write("    " + ", ".join("{%d,%d,%d}" % v for v in snapshot[i:i + 8]) + ",\n")
        c.write("};\n\n")

        # datfiles
        c.write("const struct datfile_s datfiles[] = {\n")
        for datfile, entries in datfiles.items():
            c.write(f"    {{ .filename = \"{datfile}\", .resources = "
                    f"(const struct resource_s *const[]) {{\n")
            body = []
            for e in entries:
                surf = f"&{e['surface']}" if e["surface"] else "NULL"
                body.append(
                    f"        &(const struct resource_s) {{ .resource_id = {e['rid']}, "
                    f".type = \"{e['kind']}\", .data = {e['name']}, "
                    f".data_len = {e['name']}_len, .surface = {surf} }}")
            c.write(",\n".join(body))
            c.write(f"\n    }}, .resources_count = {len(entries)} }},\n")
        c.write("};\n\n")
        c.write(f"const unsigned datfiles_count = {len(datfiles)};\n\n")

        c.write("""const struct sprite_set_s *resources_find_sprite_set(const char *datfile, int shpl_id)
{
    for (unsigned i = 0; i < sprite_sets_count; ++i) {
        if (sprite_sets[i].shpl_resource_id == shpl_id &&
            strcmp(sprite_sets[i].datfile, datfile) == 0) {
            return &sprite_sets[i];
        }
    }
    return NULL;
}
""")

    # -- report -----------------------------------------------------------

    print(f"sprite sets     : {stats['sets']}")
    print(f"sprites         : {stats['images']} "
          f"({stats['image_bytes'] / 1024:.1f} KB of 8bpp pixels)")
    print(f"raw resources   : {stats['raw']} "
          f"({stats['raw_bytes'] / 1024:.1f} KB)")
    print(f"palette tables  : {palette_bytes + len(sets) * 48} bytes")
    print(f"palette checks  : {checked} images verified against their shpl palette")
    if stats_optgraf:
        detail = ", ".join(f"{d} {n}" for d, n in sorted(stats_optgraf.items()))
        print(f"optional graphics: {sum(stats_optgraf.values())} images ({detail})")
    for f in ("resources.h", "resources.c", "resources.inc"):
        print(f"  {OUT_DIR}/{f}: {os.path.getsize(f'{OUT_DIR}/{f}') / 1024:.0f} KB")


if __name__ == "__main__":
    main()
