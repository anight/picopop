# picopop

Prince of Persia on a Raspberry Pi Pico, with an ST7789 panel, an I2S DAC, a
Bluetooth keyboard and an analog stick. All four Pico boards build - `pico`,
`pico_w`, `pico2` and `pico2_w` - and the Pico 2 W is the one it runs on daily.

It works: the game runs on the board with sound, a pad and a Bluetooth
keyboard. [`TODO.md`](TODO.md) is what is left, and picosdl keeps
[its own list](picosdl/TODO.md) for the SDL layer.

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

The recursion matters: picosdl has two submodules of its own, the ST7789
display driver and the I2S output driver.

`build/picosdl-demo.elf` is also produced. It brings up the panel, the DAC, the
radio and the stick without running the game, and stays useful as a bisection
tool — it has a steady test tone, a memory report and a mixer-load figure.

### Options

| option | default | meaning |
|---|---|---|
| `PICO_BOARD` | `pico2_w` | any of the four; see below |
| `PICOPOP_OPL` | `dbopl` | `nuked` is the more faithful OPL3 and far more expensive |

### Boards

All four build, with no argument beyond the board name:

| `PICO_BOARD` | part | flash | Bluetooth | `.text` | `.bss` |
|---|---|---|---|---|---|
| `pico` | RP2040 | 2 MB | no radio, defaults off | 1,602,308 | 183,132 |
| `pico_w` | RP2040 | 2 MB | on | 2,043,772 | 205,036 |
| `pico2` | RP2350 | 4 MB | no radio, defaults off | 1,593,816 | 182,496 |
| **`pico2_w`** | RP2350 | 4 MB | on | 2,024,320 | 204,656 |

`pico_w` is the one with no room to spare: 2,043,772 bytes of a 2 MB flash leaves
about 52 KB, and it is Bluetooth that fills it - BTstack plus the CYW43 firmware
blob. It did not fit at all until the game stopped keeping a second full-screen
buffer, which was 62.5 KB of a 264 KB part. The RP2040 rows are also the ones
with no FPU, which costs nothing here because the firmware links no maths library
either way - see below.

`pico2_w` is the default and the only one this is developed and run on. The other
three are supported by construction rather than exercised: they build clean and
the arithmetic works out, but the panel, the DAC and the radio have not been
brought up together on one. picosdl says the same of its own three.

## Two rules the firmware holds

**No maths library.** The image links no `sin`, `cos`, `pow`, `log`, `exp` or
`atan2`, in any suffix. This is enforced, not asserted: `tools/check-no-libm.sh`
runs after every link and fails the build if one comes back.

An FPU is not the point. The M33 does have one, so `float` add, multiply, divide
and `sqrt` are single instructions and are fine — `sqrt` is deliberately not on
the banned list. But an FPU gives you none of the transcendentals, which stay
library routines of hundreds of cycles and a few KB of flash each. Two riders:
`double` is soft-float on a single-precision FPU, so a stray `double` constant in
a `float` expression costs what it always did; and if the RP2350's RISC-V cores
were ever selected there would be no FPU at all.

Every use in the tree turned out to be a table that could be built at build time
or an integer decision wearing floating-point clothes:

| was | is |
|---|---|
| `DBOPL::InitTables` — `pow`, `sin` for four wave tables | generated by `tools/assets/dbopl_tables.c`, copied into RAM at boot |
| `midi_callback` — `powf`, `log2f` per note-on | a 766-byte `{block, F-number}` table, `tools/assets/midi_fnum_table.c` |
| `get_joystick_state` — `atan2` per poll | integer comparisons in `SDLPoP/src/joystick_sectors.h` |

Each has a test that checks the replacement against what it replaced, and each
was checked to fail when broken — see `make -C tools/tests all`.

**No heap.** Nearly true: every allocation the game and picosdl used to make is
gone, and the only reachable `malloc` callers left are two one-shot SDK calls at
init. Unlike the rule above this one is not yet enforced at link time, which is
the open item in [`TODO.md`](TODO.md).

## Licence

**GPL-3.0-or-later**, and that is not a preference — see [`LICENSE`](LICENSE).

The firmware is a derivative work of [SDLPoP](https://github.com/NagyD/SDLPoP),
which is GPL-3.0-or-later, and this port does not merely link it: `seg009.c`,
`seg000.c` and `midi.c` are substantially rewritten. Anything built from that has
to carry the same terms.

The rest of what goes into the image is compatible with it, which is worth
recording because one incompatible component would have been a real problem:

| component | licence |
|---|---|
| SDLPoP | GPL-3.0-**or-later** — sets the floor |
| DBOPL, from DOSBox-X | GPL-2.0-**or-later** |
| Nuked OPL3 (`PICOPOP_OPL=nuked`) | GPL-2.0-**or-later** |
| Princed Resources (`PR/`) | GPL-2.0-**or-later** |
| picosdl | BSD-2-Clause |
| pio-st7789, pio-i2s, Pico SDK | BSD-2 / BSD-3-Clause |
| the status-band font | public domain (X11 misc-fixed) |

The three GPL-2.0 components are all *or-later*, so they can be used under v3. A
GPL-2.0-**only** component could not have been, and that is the usual way a
project like this ends up undistributable.

**picosdl is BSD-2-Clause, deliberately.** It contains no SDLPoP code — it is an
independent implementation of the slice of SDL2 this game uses — and its own
dependencies are permissive. Putting the GPL on it would defeat the point of
keeping it a standalone library: someone writing closed-source firmware for a
Pico 2 W should be able to use it. Permissive code combines into a GPLv3 work
without friction, so this costs the firmware nothing.

**The game's data files are not covered by any of this.** They are not
distributed here and not in this repository's history; you supply your own, as
described above.

## Working on it

Almost everything is quicker to find on the desktop than on the board, and some
of it is only findable there. See `tools/README.md`.

```bash
make -C tools                # the game, built for the host
make -C tools/tests all      # the game-data tests
make -C picosdl/test         # picosdl's own tests, no game involved
```
