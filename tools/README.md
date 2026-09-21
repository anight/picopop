# tools

Everything that runs on a desktop rather than on the board: the game built for
the host, the tests that check the game's data, and the pipeline that turns the
original DAT files into the C the firmware links.

None of it is needed to build the firmware. It exists because almost every bug
found so far was cheaper to find here than on hardware — and several of them
were only *findable* here.

```
tools/
  Makefile        the game, built for the desktop
  host_backend.c  picosdl's backend with the panel, DAC and radio replaced
  png_write.c     a dependency-free indexed-PNG writer
  tests/          tests that read Prince of Persia's own data
  assets/         the build-time asset pipeline
```

## The desktop build

```bash
make -C tools
mkdir -p tools/frames
make -C tools run
```

This is the same code the firmware runs — the blitters, the surface pool, the
LIFO arena, the event ring, the mixer — with only the hardware half swapped
out. A wrong pixel here is a wrong pixel on the board. What it does *not*
reproduce is timing, DMA concurrency, and anything involving the radio.

It deliberately has **no SDL2 dependency**. Frames come out as PNG files
instead of going to a window, which costs interactivity and buys two things:
it builds anywhere, and every frame is a file you can diff, measure or attach
to a bug report.

The build uses exactly the firmware's limits — same arena size, same surface
count, same two screen buffers — so the peaks it reports are the peaks on the
board.

### Environment variables

| variable | effect |
|---|---|
| `PICOPOP_FRAMEDIR` | write each presented frame here as a PNG |
| `PICOPOP_FRAME_EVERY` | dump every Nth present (default 1) |
| `PICOPOP_MAX_FRAMES` | stop writing after this many (default 200) |
| `PICOPOP_EXIT_AFTER` | exit cleanly after this many presents |
| `PICOPOP_FAST` | virtual clock: delays return at once, so a run finishes as fast as it can draw |
| `PICOPOP_WAV` | record the mixer output to a WAV — a mixer bug is inaudible in a screenshot |
| `PICOPOP_KEYS` | scripted input, `NAME@POLL` comma-separated |
| `PICOPOP_WATCHDOG` | seconds before dumping a backtrace and aborting |

`PICOPOP_KEYS` matters more than it looks: the game waits for a keypress in
several places (`show_splash()` spins until Shift), so without it a headless run
parks on the first one and every frame after that is identical.

```bash
# Drive into level 2 and record both picture and sound
mkdir -p tools/frames
cd tools && PICOPOP_FRAMEDIR=frames PICOPOP_FAST=1 PICOPOP_WAV=/tmp/pop.wav \
  PICOPOP_EXIT_AFTER=4000 PICOPOP_KEYS=lshift@40,return@900,right@1600 \
  ./picopop-host megahit 2
```

`megahit` enables cheats, which is what makes the numeric level argument work.

## tests/

```bash
make -C tools/tests all      # verify + digi + mirror
make -C tools/tests verify   # every sprite against an independent reference
make -C tools/tests digi     # the resampler against the expansion it replaced
make -C tools/tests mirror   # mirrored sprites are exact reversals
make -C tools/tests music    # render a tune to a WAV and report its level
make -C tools/tests budget   # the OPL emulator's cost per output sample
make -C tools/tests opl3     # the Nuked optimisations, checked bit-exactly
```

These live here, not in `picosdl/test`, because every one of them reads the
game. picosdl is a library meant to be reused, so it must not depend on any
particular game; its own self-contained tests are in `picosdl/test`.

Two of them are worth knowing about:

- **`verify`** checks all 853 sprites against a reference built from the
  original BMPs by a completely different code path. If the generated
  8bpp-plus-global-palette form reproduces it pixel for pixel, then the row
  offsets, the palette rows, the bit unpacking and the row order are all right.
- **`digi`** and **`mirror`** drive the *shipped* code — `digi_resample.h` and
  `sprite_mirror.h` are headers precisely so the tests cannot drift into
  testing a restatement of the algorithm instead of the algorithm.

## assets/

The build-time asset pipeline. It reads the extracted DAT resources and emits
`resources.{c,h,inc}` — every sprite converted to 8bpp indexed into one global
256-colour palette, with its palette row already baked into the pixels, so the
firmware blits straight out of flash with no decode step and no allocation.

```bash
tools/assets/build-resources.sh                 # the whole pipeline
tools/assets/build-resources.sh --reference     # also reference.bin, for tests
```

That is all the builds call, and it is incremental — a stamp records the DATs
and converter it last ran against, so a second run with nothing changed does
nothing. It reads `SDLPoP/data` and writes `generated/`; both are overridable:

```bash
tools/assets/build-resources.sh /path/to/dats /path/to/output
```

Underneath it runs `convert.py`, which reads the DAT files directly through
`datfile.py` and writes nothing into the source tree.

Nothing it produces is committed: the DAT files are the original game's and the
generated C is derived from them.

`datfile.py` is the container and the image codec, ported from the reader in
`SDLPoP/src/seg009.c` — a six-byte header and an index table, then five
compression methods and a bit-depth expansion. Both forms a resource can take
come from it: the bytes exactly as the DAT holds them, which is what the
firmware embeds and parses, and the decoded pixels, which are what the sprites
become.

It also decides what a resource *is*, which the format does not record.
`looks_like_shpl()` takes a sprite-set palette to be a hundred bytes whose VGA
levels are all within six bits; `looks_like_image()` takes an image to be a
header naming a defined compression method and a plausible size, which decodes
without running off either end. Both were checked against PR, an independent
reader, and agreed on all 819 images and 20 sprite sets in the game's data.

`preview.py` renders the converted sprites as a contact sheet, which is the
quickest way to see that a palette row is wrong.
