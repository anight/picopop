# picopop

Prince of Persia on a Raspberry Pi Pico, with an ST7789 panel, an I2S DAC, a
Bluetooth keyboard and an analog stick.

The game runs on the board with sound, a pad and a Bluetooth keyboard. All four
Pico boards build; the Pico 2 W is the one it is developed and run on.
[`TODO.md`](TODO.md) records what is outstanding, and picosdl keeps
[its own list](picosdl/TODO.md) for the SDL layer.

---

## Layout

```
src/        the firmware build - CMakeLists.txt, main.c, and the hardware demo
picosdl/    submodule: a subset of SDL2 for the RP2040 and RP2350. Knows nothing
            about this or any other game, and is meant to be reused
SDLPoP/     submodule: the game, on the picopop branch of the fork
tools/      the desktop build, the tests and the asset pipeline - see its README
generated/  the converted resources, built from your DAT files (never committed)
```

The division that matters is between `picosdl` and everything else. picosdl is a
library: it provides surfaces, blitters, a palette that *is* the display CLUT, an
event queue, an audio mixer and the drivers beneath them, and it has no dependency
on Prince of Persia. Everything that does depend on the game — the game itself,
its converted resources, the demo that plays its tunes, the tests that read its
data — is on this side of that line.

---

## You need your own copy of the game

The firmware is built from Prince of Persia's original data files. They are not
distributed with this project and are not in its git history, neither the DAT
files themselves nor anything derived from them.

Copy these from your own copy of the game into `SDLPoP/data/`:

```
FAT.DAT  KID.DAT  LEVELS.DAT  PRINCE.DAT  PV.DAT  SHADOW.DAT
SKEL.DAT TITLE.DAT VDUNGEON.DAT VIZIER.DAT VPALACE.DAT
```

Any DOS release of 1.0, 1.1, 1.3 or 1.4 will do. The remaining DAT files the
build needs — the sound and guard-palette ones — ship with SDLPoP and are already
present after a submodule checkout.

If a file is missing, the build stops before compiling anything and names it.

The game's data files are yours and are not covered by this project's licence.
Nothing in `generated/` is committed and `SDLPoP/data/*.DAT` is ignored.

---

## Building

```bash
git clone --recurse-submodules git@github.com:anight/picopop.git
cd picopop
# ...put the DAT files in SDLPoP/data first...
cmake -S src -B build
cmake --build build
```

If you cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

The recursion matters: picosdl has two submodules of its own, the ST7789 display
driver and the I2S output driver.

The build produces one firmware as two images — `build/picopop.uf2` and
`build/picopop.elf` — and which you use depends on whether you have a second Pico
to debug with. `picosdl-demo` is produced alongside, in both forms. It brings up
the panel, the DAC, the radio and the stick without running the game, and is the
bisection tool of choice when something is wrong at that level: it has a steady
test tone, a memory report and a mixer-load figure.

### Flashing over USB

Hold BOOTSEL down, plug the board into USB, then release it. The board appears as
a USB mass storage device instead of running anything; copy the `.uf2` onto it and
it reboots into the firmware as soon as the copy completes.

```bash
cp build/picopop.uf2 /media/$USER/RPI-RP2/
```

`RPI-RP2` is what an RP2040 names the drive; an RP2350 uses a name of its own, so
go by whatever actually mounted.

This needs no hardware beyond a cable, and is the route for running the firmware
rather than working on it.

### Flashing over SWD

```bash
picosdl/picodev.sh flash build/picopop.elf
```

This requires a probe on the board's SWD pins. Either works:

- A [Raspberry Pi Debug Probe](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html).
  That page covers wiring, OpenOCD, and the serial connection.
- A spare Pico running the [debugprobe firmware](https://github.com/raspberrypi/debugprobe).
  Wiring for that is Appendix A of
  [Getting started with Raspberry Pi Pico-series](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf)
  — A.2 for the setup, A.3 for the pin-by-pin wiring.

The script determines which part is attached for itself.

It is worth setting up if you are iterating. It programs without anyone reaching
for a button, silences the audio first — a halted board does not stop making
noise, because the audio DMA chain re-triggers itself — and `flash-and-logs`
attaches the console reader *before* programming, so the start-up banner is still
there when you begin watching. `picosdl/README.md` documents the rest.

### Boards

`PICO_BOARD` is the only build option, and defaults to `pico2_w`. All four boards
build with no argument beyond the name:

| `PICO_BOARD` | part | flash | Bluetooth | `.text` | `.bss` |
|---|---|---|---|---|---|
| `pico` | RP2040 | 2 MB | no radio, defaults off | 1,602,780 | 183,180 |
| `pico_w` | RP2040 | 2 MB | on | 2,044,588 | 205,120 |
| `pico2` | RP2350 | 4 MB | no radio, defaults off | 1,594,208 | 182,560 |
| **`pico2_w`** | RP2350 | 4 MB | on | 2,025,192 | 204,772 |

`pico_w` is the constrained one: 2,044,588 bytes leaves 51.3 KB of a 2 MB flash,
and it is Bluetooth that fills it — BTstack plus the CYW43 firmware blob. Its RAM
is comfortable by comparison, `.bss` leaving 55.7 KB of the 256 KB main region
for stack and heap.

The RP2040 boards are also the ones with no FPU, which costs nothing here because
the firmware links no maths library on any board — see below.

`pico2_w` is the default and the only board this is developed and run on. The
other three are supported by construction rather than exercised: they build clean
and the arithmetic works out, but the panel, the DAC and the radio have not been
brought up together on one. picosdl says the same of its own three.

---

## Making it fit

An RP2350 has 520 KB of SRAM and the RP2040 half that, against a game written for
a PC with a disk. Four decisions account for most of the difference, and each is
enforced or measured rather than assumed.

### Assets are resolved at build time

The DAT files are converted into `generated/resources.{c,h,inc}` before anything
is compiled: every sprite 8bpp indexed into one global 256-colour palette, with
its set's row already baked into the pixels. The firmware blits straight out of
flash with no decode step and no allocation.

The MIDI goes the same way. All 22 tunes are parsed at build time into flash
tables, so the firmware carries no MIDI parser and none of the 80 KB event pool
one would need. `make -C tools/tests midi-tables` renders every tune from the
tables and from the parser and compares the output sample for sample.

### No maths library

The image links no `sin`, `cos`, `pow`, `log`, `exp` or `atan2`, in any suffix.
This is enforced rather than asserted: `tools/check-no-libm.sh` runs after every
link and fails the build if one reappears.

An FPU is not the point. The M33 has one, so `float` add, multiply, divide and
`sqrt` are single instructions and are fine — `sqrt` is deliberately not on the
banned list. But an FPU provides none of the transcendentals, which remain library
routines of hundreds of cycles and several KB of flash each. Two riders: `double`
is soft-float on a single-precision FPU, so a stray `double` constant in a `float`
expression costs what it always did; and the RP2350's RISC-V cores, if ever
selected, have no FPU at all.

Every use in the tree proved to be either a table that could be built at build
time or an integer decision in floating-point clothes:

| was | is |
|---|---|
| `DBOPL::InitTables` — `pow` and `sin` for four wave tables | generated by `tools/assets/dbopl_tables.c`, copied into RAM at boot |
| `midi_callback` — `powf` and `log2f` per note-on | a 766-byte `{block, F-number}` table, `tools/assets/midi_fnum_table.c` |
| `get_joystick_state` — `atan2` per poll | integer comparisons in `SDLPoP/src/joystick_sectors.h` |

Each replacement has a test that checks it against what it replaces, and each test
was itself checked to fail when the replacement is broken. See
`make -C tools/tests all`.

### One screen buffer

The game composes into an offscreen surface and reveals it. Here that surface is a
*view* of the single buffer the panel is sent — `picopop_screen_fb`, 64,000 bytes
and the largest object in `.bss` — rather than a second buffer of its own.

What makes this work is that the panel holds the visible image: indices are
expanded to RGB565 by the PIO at push time, so pixels already pushed are immune to
later drawing. The reveal points in `seg009.c` and `seg000.c` hold a present back
while the game composes and push at the moment it reveals, which preserves the
visibility semantics the game expects from two buffers.

The saving is 62.5 KB, and it is what brings `pico_w` — 264 KB of SRAM, with
Bluetooth in it — within reach.

### No heap

Nearly true. Every allocation the game and picosdl make is gone: sprites are in
flash, peels come from picosdl's LIFO arena, the digi path resamples out of flash,
and the MIDI is pre-parsed. What remains is two one-shot calls inside the SDK at
init.

Unlike the rule above, this one is not yet enforced at link time, which is the
open item in [`TODO.md`](TODO.md).

---

## Working on it

Almost everything is faster to find on the desktop than on the board, and some of
it is only findable there — the host build runs the same game code against
picosdl's host backend, writing frames as PNGs and the mixer output as a WAV.
`tools/README.md` covers it.

```bash
make -C tools                # the game, built for the host
make -C tools/tests all      # the game-data tests
make -C picosdl/test         # picosdl's own tests, no game involved
```

---

## Licence

**GPL-3.0-or-later**, and not by preference — see [`LICENSE`](LICENSE).

The firmware is a derivative work of [SDLPoP](https://github.com/NagyD/SDLPoP),
which is GPL-3.0-or-later, and this port does not merely link it: `seg009.c`,
`seg000.c` and `midi.c` are substantially rewritten. Anything built from that
carries the same terms.

The rest of what enters the image is compatible, which is worth recording because
a single incompatible component would have been a genuine problem:

| component | licence |
|---|---|
| SDLPoP | GPL-3.0-**or-later** — sets the floor |
| DBOPL, from DOSBox-X | GPL-2.0-**or-later** |
| Nuked OPL3 (in `SDLPoP/`, built only by the tests) | GPL-2.0-**or-later** |
| picosdl | BSD-2-Clause |
| pio-st7789, pio-i2s, Pico SDK | BSD-2 / BSD-3-Clause |
| the status-band font | public domain (X11 misc-fixed) |

The three GPL-2.0 components are all *or-later*, so they may be used under v3. A
GPL-2.0-**only** component could not have been, and that is the usual way a
project of this shape becomes undistributable.

**picosdl is BSD-2-Clause, deliberately.** It contains no SDLPoP code — it is an
independent implementation of the slice of SDL2 this game uses — and its own
dependencies are permissive. Placing the GPL on it would defeat the purpose of
keeping it a standalone library: someone writing closed-source firmware for a
Pico 2 W should be able to use it. Permissive code combines into a GPLv3 work
without friction, so this costs the firmware nothing.

**The game's data files are not covered by any of this.** They are not
distributed here and not in this repository's history; you supply your own, as
described above.
