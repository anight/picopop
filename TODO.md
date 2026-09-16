# TODO

What is left in the firmware, roughly in the order it is worth doing. picosdl
keeps its own list in [`picosdl/TODO.md`](picosdl/TODO.md) — anything that is
about the SDL layer rather than the game belongs there.

Everything here was checked against the linked image rather than against the
source, because several items that looked outstanding turned out to be finished
and one that looked finished turned out not to be. Where a claim has a number,
the command that produced it is given.

## The firmware still links a maths library

One site reaches it, and it is the only one. From the image:

```
arm-none-eabi-objdump -d build/picopop.elf | awk \
  '/^[0-9a-f]+ <.*>:/{fn=$2} /bl.*<__wrap_(pow|powf|sin|log2f|atan2)>/{print fn, $NF}' | sort -u
```

| caller | calls | when |
|---|---|---|
| `midi_callback` | `powf`, `log2f` | **per note-on**, turning a MIDI note into an OPL block/F-number |

Not hard. The conversion is indexed by `note - 81 + midi_semitones_higher`, a
small integer, so it is a const `{block, fnum}` lookup.

`get_joystick_state`'s `atan2` was the second and is **done**. It computed an
angle only to compare it against six fixed rays, and for that only the side of
the ray matters — so the tests are now integer, in
`SDLPoP/src/joystick_sectors.h`. Squaring turns each into `y*y` against `3*x*x`
for the 60-degree rays, where `tan^2 60` is exactly 3, so three of the four come
out exact rather than approximated. `make -C tools/tests joystick` checks them
against the atan2 form over both ADC grids in full and over every integer
straddling every ray.

`DBOPL::InitTables` was the third and is **done**: `tools/assets/dbopl_tables.c`
generates its `pow`/`sin` tables and `InitTables()` copies them in at boot.
`make -C tools/tests dbopl-tables` renders the same tune with the generated
tables and with the computed ones and requires the WAVs to match byte for byte,
so the copied loops cannot drift from the originals unnoticed.

That one did not go the way this entry predicted, which is worth keeping. It
claimed the change would reclaim 8,960 bytes of `.bss`. It does not: leaving the
tables in flash and reading them where they lie costs the mixer six points of
its block budget — 24-25% average became 30-31% — because the RP2350's XIP cache
is 8 KB and `WaveTable` alone is 8 KB, so a table indexed once per sample per
operator evicts everything else on its way past. Copying them into RAM at boot
gives the libm removal at no CPU cost and no RAM saving. **The `.bss` was the
wrong reason to do it; the maths library was the right one.**

**What the rule is, precisely.** The M33 has a single-precision FPU, so `float`
add, subtract, multiply, divide and `sqrt` are single instructions and are fine.
An FPU does not give you `sin`, `pow`, `log`, `exp` or `atan2` — those stay
library calls of hundreds of cycles each. Two riders: `double` is soft-float on
this FPU, so one stray `double` constant in a `float` expression costs what it
always did; and if the RP2350's RISC-V cores are ever selected there is no FPU
at all and the whole argument returns.

Two ways it creeps back once removed: newlib's `printf` family pulls float
conversion in for `%f`/`%g`, and `double`-typed constants in otherwise integer
expressions emit soft-float helpers. **Done when** the image links with no libm
object in the `.map` and the music still matches the reference render
(`make -C tools/tests music`).

## The heap is down to two SDK calls, and nothing stops a third

The goal is zero reachable `malloc`, and the game is there. Every allocation
SDLPoP and picosdl used to make is gone — sprites live in flash, peels come from
picosdl's LIFO arena, the digi path resamples out of flash, the MIDI parser no
longer reallocs per event. What is left is two one-shot calls inside the SDK:

```
arm-none-eabi-objdump -d build/picopop.elf | awk \
  '/^[0-9a-f]+ <.*>:/{fn=$2} /bl.*<__wrap_(malloc|calloc|realloc)>/{print fn}' | sort -u
  alarm_pool_create_on_timer_with_unused_hardware_alarm
  cyw43_btbus_init
```

`PICO_HEAP_SIZE=32768` (`src/CMakeLists.txt`) is reserved for those two.

So this is not a porting job any more — it is an enforcement job. Nothing in the
build fails if a heap call comes back. Wrap the family with panicking wrappers
and grep the `.map` in CI, with those two exempted by name.

**Why it is a rule and not a preference.** A `malloc` inside the I2S DMA
interrupt on core 1 returned NULL and the firmware played bootrom contents as
audio at full scale. That was not a hypothetical; it was the noise on first
power-up. Address 0 is bootrom on this part, so the failure mode has not changed
— and on a microcontroller with no MMU, every out-of-memory is a silent,
position-dependent failure hours into play. Static reservation puts the whole
budget in the `.map` at build time instead.

Watch the stack once the heap is gone: it becomes the remaining way to run out
of memory, and both core stacks should be set explicitly rather than defaulted.

## DBOPL or Nuked is worth reopening

The firmware runs **DBOPL**, DOSBox's OPL emulator. `PICOPOP_OPL=nuked` still
builds and switches to Nuked, which is the more exact model. Measured on the
title theme with callgrind:

| | instr / output sample | chip state | host time |
|---|---|---|---|
| Nuked 1.7.4 stock | 9203 | 20776 B | 5.5% |
| Nuked + silent-slot skip + unfolded table | 5583 | 20776 B | 3.3% |
| Nuked 1.8 upstream | — | — | 8.6% |
| **DBOPL (in use)** | **938** | **4380 B** | **0.6%** |

DBOPL was chosen under RAM and CPU pressure that no longer exists: 520 KB of
SRAM makes Nuked's 20 KB chip struct affordable, and the mixer sits at 24-25% of
its block budget with room above it. The difference is structural — Nuked runs
its chip model at 49716 Hz and resamples, DBOPL scales its counters and generates
at the output rate — so DBOPL is the less exact of the two.

How different it sounds, measured rather than guessed: level-matched to 0.06 dB
with no correction (RMS 2409 against 2392), spectrum within 0.1 dB below 3 kHz
and +1.3 dB above 6 kHz, and a 0.9995 loudness-envelope correlation. Same notes,
same times, same dynamics, slightly brighter top end.

It is one CMake variable either way. What it needs is a listen on the board and
a mixer-load reading with `PICOPOP_OPL=nuked`, not more analysis.

The two Nuked optimisations are kept and still build: **silent-slot skipping**
(`OPL3_SKIP_SILENT_SLOTS`, 38% cheaper, not bit-exact but 73 dB down, enforced by
`make -C tools/tests opl3`) and an **unfolded log-sine table** (512 bytes of
flash, bit-exact). The instruction counts behind the second were taken for
cortex-m0plus and do not transfer to Thumb-2; redo them before relying on the
figure.

## A peel pins its own mirror

`draw_main_midtable` calls `add_peel()` between `hflip()`'s arena allocation and
its free, so every flipped sprite drawn with a peel pins its mirror until the
peel goes. Measured at 860-1560 bytes, which is nothing against the arena's
16 KB, and it is not a leak — it unwinds correctly.

It is here because it is the shape of thing that has twice exhausted the arena:
the arena is LIFO, so what matters is lifetime rather than size, and out-of-order
release is silent by design. Removable by hoisting the `add_peel()` call above
the flip — the arguments do not depend on it. Not done.

## Open question: saved games, or an arcade cabinet?

`SDL_RWFromFile` fails cleanly, so the game starts from level 1 every time. A
save needs a small flash key-value store, and every write has to go through
`flash_safe_execute` with core 1 — the mixer — locked out.

That is the same mechanism as the Bluetooth bond that does not survive a reboot
(picosdl's TODO), and it is worth doing in that order: diagnose the bond first,
since it fails today and a save would fail the same way for the same reason.

Worth deciding deliberately: from-level-1-every-time is a legitimate answer for a
device with no keyboard and a pad with two buttons free.

## Worth noticing: the budget stopped binding

This project spent most of its life defending a memory budget, and that budget is
no longer scarce.

| | used | free |
|---|---|---|
| flash `.text` | 1897 KiB of 4096 | **~2199 KiB** |
| SRAM `.bss` | 343 KiB of 520 | **~177 KiB** |

Most of that flash is not code. Read-only data is 1460 KiB of it (`nm -S`,
summing the `R`/`r` symbols) — the converted sprites, the raw DAT resources and
the CYW43 firmware blob — so actual code is a few hundred KiB against roughly
seven times that in headroom.

Mods, a filesystem and a second tile set were all ruled out when this was tight,
and all are affordable now. None belongs on the critical path. The point of
recording it is that the constraint which used to say *no* to every such idea is
gone, and it is better to notice that once than to rediscover it one feature at a
time.
