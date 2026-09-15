# picopop

Prince of Persia on a Raspberry Pi Pico 2 W, with an ST7789 panel, an I2S DAC,
a Bluetooth keyboard and an analog stick.

`PLAN.md` is the working document: what is done, what is not, and the
measurements behind the design decisions.

## Layout

```
src/        the firmware build - CMakeLists.txt, main.c, and the hardware demo
picosdl/    submodule: a small SDL2 subset for the RP2350. Knows nothing about
            this or any other game, and is meant to be reused
SDLPoP/     submodule: the game, on the picopop branch of the fork
PR/         submodule: Princed Resources, the tool that reads the DAT files
tools/      the desktop build, the tests and the asset pipeline - see its README
generated/  the converted resources, built from your DAT files (never committed)
```

The split that matters is between `picosdl` and everything else. picosdl is a
library: it provides a framebuffer, blitters, a palette that *is* the display
CLUT, an event queue, an audio mixer and the drivers underneath them. It has no
dependency on Prince of Persia. Everything that does — the game, its converted
resources, the demo that plays its tunes, the tests that read its data — lives
on this side of that line.

## You need your own copy of the game

The firmware is built from Prince of Persia's original data files. They are
not distributed with this project and are not in its git history — neither the
DAT files themselves nor anything derived from them.

Copy these from your own copy of the game into `SDLPoP/data/`:

```
FAT.DAT  KID.DAT  LEVELS.DAT  PRINCE.DAT  PV.DAT  SHADOW.DAT
SKEL.DAT TITLE.DAT VDUNGEON.DAT VIZIER.DAT VPALACE.DAT
```

Any DOS release of 1.0, 1.1, 1.3 or 1.4 will do. The remaining DAT files that
the build needs — the sound and guard-palette ones — ship with SDLPoP itself
and are already there after a submodule checkout.

The build converts them into `generated/resources.{c,h,inc}`: every sprite
8bpp indexed into one global 256-colour palette with its set's row already
baked into the pixels, so the firmware blits straight out of flash with no
decode step and no allocation. Nothing in `generated/` is committed, and
`SDLPoP/data/*.DAT` is ignored.

If a file is missing the build stops before compiling anything and tells you
which.

## Building

```bash
git clone --recurse-submodules git@github.com:anight/picopop.git
cd picopop
# ...put the DAT files in SDLPoP/data first...
cmake -S src -B build
cmake --build build
```

That produces `build/picopop.uf2`. To flash it:

```bash
picosdl/picodev.sh flash build/picopop.elf
```

If you already cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

The recursion matters: picosdl has a submodule of its own, the ST7789 driver.

`build/picosdl-demo.uf2` is also produced. It brings up the panel, the DAC, the
radio and the stick without running the game, and stays useful as a bisection
tool — it has a steady test tone, a memory report and a mixer-load figure.

### Options

| option | default | meaning |
|---|---|---|
| `PICO_BOARD` | `pico2_w` | `pico_w` still builds, but is short of RAM and flash |
| `PICOPOP_OPL` | `dbopl` | `nuked` is the more faithful OPL3 and far more expensive |

## Working on it

Almost everything is quicker to find on the desktop than on the board, and some
of it is only findable there. See `tools/README.md`.

```bash
make -C tools                # the game, built for the host
make -C tools/tests all      # the game-data tests
make -C picosdl/test         # picosdl's own tests, no game involved
```
