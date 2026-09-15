# Prince of Persia on a Raspberry Pi Pico 2 W — plan of works

Target: SDLPoP running on a **Pico 2 W (RP2350)** with an ST7789 panel, an I2S
DAC, a Bluetooth keyboard and an analog stick.

> **Board changed on 2026-08-29** to the Pico 2 W, from the smaller part this
> plan was originally written against. Two consequences run through everything
> below. First, the RAM and flash squeezes that shaped much of it are **gone**:
> 520 KB of SRAM and 4 MB of flash, against the 264 KB and 2 MB assumed
> throughout. Several arguments here existed only to survive that squeeze
> and are now retired rather than deleted, because the reasoning is still what
> justifies the design. Second, and less comfortably, **every number marked
> *measured on hardware* was measured on the old board** — a Cortex-M0+ at
> 128 MHz, not a Cortex-M33 at up to 150 MHz. Those numbers are now estimates
> until re-taken. §3.7 is the revalidation: the build, the pad mux and the
> flash/BSS figures are **already done**; what has not happened is running the
> thing, so the frame rate and the mixer load are the two numbers still owed.

Section 0 states the two properties the firmware must have. Sections 1–3 are the
working state: what exists, what does not, what to do next. Sections 4 onward are
the analysis behind the design, kept because the reasoning still has to be
re-checked when things change.

Numbers marked *measured* came from this tree or from the board; where it
matters, the text says which. Everything else is an estimate and says so. The
distinction earns its keep: several host measurements had to cross an ISA
boundary before they could be trusted, and one of them (the OPL emulator's cost)
decided the design.

---

## 0. Hard constraints

Two properties of the shipped firmware are requirements, not preferences. They
are stated here because they constrain nearly every item below, and because both
are cheap to hold and expensive to retrofit.

**No dynamically allocated memory.** The image must contain no reachable
`malloc`, `calloc`, `realloc`, `free`, `strdup` or any other heap call. Not "a
small heap", not "a heap only used at init" — none.

The board change removed one of the three reasons for this rule, and it is worth
being explicit about which: *"a heap large enough for `parse_midi` will not fit"*
**no longer holds** — on 520 KB it fits easily. The other two are untouched and
are the ones that matter:

- A `malloc` inside the I2S DMA interrupt on core 1 returned NULL and the
  firmware played ROM contents as audio (§3.3). That was not a theoretical
  failure; it was the noise on first power-up. Address 0 is bootrom on RP2350
  on this part as on the last, so the failure mode is unchanged.
- A heap on a microcontroller with no MMU turns every out-of-memory into a
  silent, position-dependent failure hours into play. Static reservation makes
  the whole memory budget visible in the `.map` file at build time. More RAM
  raises the threshold; it does not change the shape of the bug.

*How it is enforced*: link with
`-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup` and
panicking wrappers, plus a CI grep of the `.map`. §9 is the audit and §3.4 the
remaining work. Two SDK exemptions are known and one-shot at init
(`alarm_pool_create_on_timer_with_unused_hardware_alarm`, `cyw43_btbus_init`).

**No math library.** The image must not link `sin`, `cos`, `pow`, `log`, `exp`,
`atan2`, `fmod` or their `f`-suffixed forms.

The board change weakens the *arithmetic* half of this argument and leaves the
*transcendental* half standing, so state it precisely. The RP2350's Cortex-M33
cores have a single-precision FPU, which the previous M0+ core did not — so plain
`float` add, subtract, multiply, divide and `sqrt` are now single instructions
and cost nothing worth avoiding. **But an FPU does not give you `sin`, `pow`,
`log`, `exp` or `atan2`**: those remain library routines costing hundreds of
cycles and a few KB of flash each, on this core as on the last. `sqrt` therefore
comes off the banned list; the rest stay on it.

Two riders. If the RP2350's RISC-V (Hazard3) cores are ever selected instead of
the M33s, there is no FPU at all and the original argument returns in full.
And `double` is still soft-float on the M33's single-precision FPU, so a stray
`double` constant in an otherwise `float` expression is as expensive as it ever
was.

Every use in this tree is either debug output, a table that can be computed at
build time, or an integer decision wearing floating-point clothes — see §3.8,
which lists all four sites. Dropping them also removes the 8.8 KB of RAM that
DBOPL spends on tables it computes at boot (§7).

*How it is enforced*: a CI check that the link map contains no libm objects, and
that the firmware links with `-lm` removed rather than merely unused. §3.8 is the
work.

Both rules apply to the **firmware** only. The host test harness and the
build-time asset and MIDI converters are ordinary desktop programs and may use
whatever they like — indeed the point of moving work to build time is that the
converter gets to use `pow` so the firmware does not have to.

---

## 1. Status at a glance

| | state | where |
|---|---|---|
| Asset pipeline | **done** — 707 sprites, one global palette, all in flash | [`PR/src/bin/`](PR/src/bin/README.md) |
| picosdl (the SDL layer) | **done** for the API the game uses | [`picosdl/`](picosdl/README.md) |
| Hardware integration | **done on the old board** — display, audio, BT keyboard and stick all live together; RP2350 build compiles but is unrun | `picosdl/backend/pico/` |
| Host test harness | **done** — 56 unit checks + 741 sprite checks | `picosdl/test/` |
| Adlib music | **done** — ran on the old board at 38–41% of one core; *needs re-measuring on RP2350* | `picosdl/demo/`, §8 |
| Bluetooth pairing | **broken** — the bond does not survive a reboot; re-pair every time | §8 |
| Game-side pixel pipeline | **not started** — SDLPoP still decodes to 24bpp at runtime | `SDLPoP/src/seg009.c` |
| Game-side audio | **not started** — the digi path still cannot fit in RAM | `SDLPoP/src/seg009.c` |
| No heap in the firmware | **partly** — picosdl side done, game side pending, not yet enforced at link time | §0, §3.4, §9 |
| No libm in the firmware | **not started** — 4 sites, all removable | §0, §3.8 |
| RP2350 migration | **build done, not yet run** — targets `pico2_w`, pad mux fixed, sizes retaken; fps and mixer load still stale | §3.7 |

Two things are true at once: the hard, uncertain half — does the hardware
cooperate, does the palette scheme work, does anything fit, is the emulator
affordable — was answered **on a real board**, and the large, mechanical half
(rewiring SDLPoP onto it) has not been started.

The board change adds a third: those answers were obtained on the *previous*
board. The design questions they settled (the palette scheme works, the sprites
fit in flash, DBOPL is affordable) do not depend on the silicon and stand. The
driver-level and performance answers do depend on it and have to be taken
again — see §3.7.

---

## 2. What is done

### 2.1 The build-time asset pipeline

`PR/src/bin/convert.py` turns the extracted DAT resources into C. Full detail in
[`PR/src/bin/README.md`](PR/src/bin/README.md); the essentials:

- **707 sprites** converted from 4bpp-with-a-private-palette to 8bpp indexed
  into one global 256-colour palette, **875.6 KB** of pixels.
- **741 `const SDL_Surface` objects** (34 of GUARD.DAT's are shared by two
  sprite sets, hence more surfaces than sprites).
- **20 sprite sets**, each carrying its palette row, its sixteen colours, its
  colour key and its image list.
- **7 guard colour variants** from PRINCE.DAT resource 10.
- **383 raw resources** (183.3 KB) — levels, sounds, MIDI, sprite-set headers —
  embedded byte-for-byte, unchanged.

*Measured*, compiled for Cortex-M0+: **1138 KB `.rodata`, 0 bytes `.data`.**
Nothing is decoded at runtime, copied to RAM, or allocated.

**The global palette did not have to be invented.** The DOS original ran in VGA
mode 13h with one 256-entry hardware palette and loaded each sprite set's sixteen
colours into one *row* within it — and the game still says which row at every
load site (`load_chtab_from_file(id_chtab_2_kid, 400, "KID.DAT", 1<<7)` means row
7, indices 112–127). `PALETTE_ROWS` in `convert.py` has one entry per set, each
citing the line of game source that decides it. Rows 5, 6, 8 and 10 are shared
(dungeon vs palace, the five guard types, the two cutscene sets) and get
rewritten on load, which is one `SDL_SetPaletteColors` call touching no pixels.

**Verified end to end, not just asserted.** `dump_reference.py` renders every
sprite from the original BMPs through a separate code path;
`picosdl/test/verify_resources.c` loads each set's palette into the CLUT the way
the game will, blits the const flash surface with picosdl's real blitter, maps
the result back through the palette and compares pixel for pixel. **741 sprites,
0 mismatched.** That covers the four things that can each be subtly wrong and
still look plausible: the row offset, the palette contents, the 1bpp/4bpp
unpacking, and the BMP's bottom-up row order. `preview.py` renders contact sheets
for the question a pixel comparison cannot answer — whether it *looks* right.

### 2.2 picosdl

A minimal SDL2 work-alike, ~2.5k lines. Full detail in
[`picosdl/README.md`](picosdl/README.md).

Implemented: surfaces, blitters, palette, window/present, event queue and key
state, timers, audio open/callback/pause/lock, memory RWops, joystick, and the
stubs for everything meaningless on a microcontroller.

Two additions that are not SDL but earned their place on hardware: a **master
volume** applied where the mixed samples reach the DAC (SDL has none, and a
MAX98357A at full scale with a mixer bug is genuinely unpleasant to debug), and
**serial console commands** for Bluetooth — the one subsystem whose failures
nothing on screen explains.

Beyond SDL, five blitters that exist because they are the operations SDLPoP
wraps SDL in anyway, and doing them directly avoids a scratch surface:

| | replaces | saves |
|---|---|---|
| `PSDL_BlitMirrored` | `hflip()` (`seg008.c:959`) | a full surface allocation **per left-facing sprite draw** |
| `PSDL_BlitXor` | `blit_xor()` (`seg009.c:3101`) | two scratch surfaces per call |
| `PSDL_BlitMono` | `method_3_blit_mono()` | an ARGB8888 conversion of the whole sprite |
| `PSDL_BlitTransp` | per-blit `SDL_SetColorKey` | a write to a const flash sprite (which would fault) |
| `PSDL_BlitOffset` | — | for sets not baked at build time |

Two invariants the implementation is built around:

- **One pixel format.** 8bpp indexed, one shared `psdl_pixel_format`, one
  palette. The palette *is* the display CLUT, so a fade is 256 register writes
  rather than 64000 pixel writes.
- **Nothing allocates.** Surfaces come from a screen-buffer pool, a LIFO bump
  arena (for peels, which SDLPoP releases in strict stack order), or externally
  owned pixels. `PSDL_SURF_CONST` marks objects that live in flash and cannot be
  written at all — every mutating entry point checks it first.

### 2.3 Hardware integration

One CMake project, one board (`pico2_w`), one clock (128 MHz), all three
peripherals alive together, compiling warning-free under `-Wall -Wextra` — and
**run on hardware**, though on the previous board: the panel draws, the stick
reads, a Bluetooth keyboard pairs and types, and the title theme plays from boot.
The RP2350 build compiles and has been sized but not yet run (§3.7).

The resource map is in `picosdl/backend/pico/psdl_pico.h`, and it survived first
contact with the board unchanged. Four things that had to be got right, each of
which would have cost real debugging time:

- **The display driver hardcodes DMA 0–3 and does not claim them**, while CYW43
  and I2S both ask the SDK for "any free channel". The video backend claims them
  on the driver's behalf and must initialise first; it panics if it is too late.
- **BTstack must not run its own loop.** Under
  `pico_cyw43_arch_threadsafe_background` the run loop is interrupt-driven, so
  `btstack_run_loop_execute()` parks forever. The game owns the main loop.
- **`PicoI2S_verifyPIOClockDivision()` was wrong.** It rejected any divider whose
  fraction was not a multiple of 1/16; the PIO divider is 16.**8** fixed point,
  so the step is 1/256. 8 kHz at 128 MHz divides exactly — which is why the clock
  was kept at 128 MHz across the board change (§3.7) — so nothing had noticed.
  22050 Hz was being rejected over a 0.0002% rounding error. Patched in the
  vendored copy.
- **`PioI2S_init` runs on core 1.** `irq_set_exclusive_handler` acts on the
  *calling* core's NVIC, so initialising from core 0 would fire the DMA interrupt
  where the mixer is not.

The flash-versus-core-1 hazard is handled rather than deferred: core 1 calls
`multicore_lockout_victim_init()` and the whole audio path is
`__not_in_flash_func`, so a BTstack pairing write glitches audio instead of hard
faulting.

### 2.4 Test harness

`picosdl/src/` is portable — hardware lives behind the backend interface — so it
builds with a plain compiler:

| | |
|---|---|
| `make -C picosdl/test` | 56 checks: clipping at every edge, mirrored-blit clipping, LIFO reclamation, pool exhaustion, event-ring overflow |
| `make -C picosdl/test asan` | the same under ASan + UBSan |
| `make -C picosdl/test verify` | 741 sprites compared pixel-for-pixel against the originals |

`test/host_backend.c` is also the seed of the host backend section 6 calls for:
it captures presented frames, and giving it a real SDL2 window is a change to
that one file.

---

## 3. What remains

Ordered by dependency. Items 3.1–3.3 can all be done and debugged **on the
desktop**, which is the point of having a host backend, and should be — which is
convenient, because **§3.7 now blocks everything that touches hardware** and the
desktop work is unaffected by the board change. Do 3.7 and 3.1 in parallel: they
share no files.

### 3.1 Rewire SDLPoP's pixel pipeline onto picosdl

The largest remaining piece, and the one everything else waits on. SDLPoP still
decodes sprites into `malloc`'d 8bpp surfaces at level load and blits them into
24bpp offscreen surfaces. All of that goes.

**Sprite loading.** `load_sprites_from_file()` (`seg009.c:451`) becomes a lookup:
`resources_find_sprite_set(datfile, resource)`, then write the set's sixteen
colours to `palette_row * 16`, then return a chtab pointing at
`set->images`. This requires changing `chtab_type` (`types.h:273`) from a
flexible array of `image_type*` to a `const SDL_Surface *const *`, plus a static
array of the 10 chtabs — `chtab_addrs[]` currently holds heap pointers. Affects
`get_image()` (12 call sites), `free_chtab()`, `set_chtab_palette()`.

**Palette.** `set_pal()`, `set_pal_arr()`, `set_pal_256()` and
`read_palette_256()` (`seg009.c:2799` and around) currently maintain a
`palette[256]` array that goes nowhere. Wire them to `SDL_SetPaletteColors`.
`set_chtab_palette()` (`seg009.c:4101`) currently walks every image in a chtab
setting per-image palettes; it becomes one call writing sixteen entries at
`row * 16`. Once that is done the fade machinery
(`make_pal_buffer_fadein/out`, `fade_in_frame`, `fade_out_frame`) should work
unchanged and cost almost nothing.

**Blitters.** The wrappers change; their ~68 call sites do not:

| wrapper | sites | becomes |
|---|---|---|
| `method_1_blit_rect` | 20 | `PSDL_BlitTransp(..., blit != blitters_0_no_transp)` |
| `method_6_blit_img_to_scr` | 14 | dispatch to `PSDL_BlitTransp` / `PSDL_BlitXor` / `PSDL_BlitMono` |
| `method_5_rect` | 10 | `SDL_FillRect` (already close) |
| `method_3_blit_mono` | 5 | `PSDL_BlitMono` |
| `draw_image_transp(_vga)` | 5 | `PSDL_BlitTransp` |
| `hflip` (`seg008.c:959`) | 2 | thread a mirror flag into `draw_image`, call `PSDL_BlitMirrored` |
| `blit_xor` | 2 | `PSDL_BlitXor` |

**Delete.** Once the above lands, nothing calls: `decode_image`,
`decompr_img`, `conv_to_8bpp`, `calc_stride`, `decompress_rle_lr/ud`,
`decompress_lzg_lr/ud`, `load_image`, `hflip`, and the decoding half of
`load_sprites_from_file`. That is roughly 400 lines of `seg009.c` and every
allocation in group (b) of section 9.

**Do the single-buffer work here too.** §10 concludes the game can run on one
screen surface, saving 62.5 KB, by replacing `copy_screen_rect()`'s
offscreen→onscreen blit with a dirty-rect push to the panel. Every call site
involved is already being opened by the work above; doing it afterwards means
doing it twice. That means: `make_offscreen_buffer` becomes a stub returning the
screen surface, `PSDL_SCREEN_BUFFERS` drops to 1, and each row of §10's
table gets its own treatment.

**Done when** the game runs on the desktop against picosdl's host backend,
drawing entirely in 8bpp from flash-resident sprites, on one screen surface, and
looks identical to the current build.

### 3.2 The font

`hc_font_data[]` is compiled into `seg009.c:980` — it is not a DAT resource — and
`load_font_from_data()` decodes it at runtime into a chtab of ~100 `malloc`'d
surfaces. It is drawn *mono* (`method_3_blit_mono` substitutes a colour per
call), so it needs no palette row, which is why the asset pipeline leaves it
alone.

Two options: extend `convert.py` to read `hc_font_data[]` and emit const glyph
surfaces the same way as sprites, or keep the runtime decode but target the LIFO
arena instead of the heap. The former is tidier and about 4 KB of flash; prefer
it, since it also deletes the last caller of `decode_image`.

### 3.3 Audio

**The digi path has to change before anything can run.** *Measured*: 106 KB of
digi resources, at 11000 Hz (28 of 30 sounds), 8200 Hz and 14000 Hz.
`convert_digi_sound()` (`seg009.c:2294`) expands each to 16-bit stereo at 44.1
kHz — about 16× — and caches all 58 in `sound_pointers[]`, which is on the order
of **1.5 MB of RAM**. This is not an allocation to make static; the data must not
exist. Replace with a fractional-step resampler in the mixer reading the 8-bit
source directly out of flash. `SDL_BuildAudioCVT`/`SDL_ConvertAudio` then never
need implementing. **Use the SIO interpolator for this** (RP2350 carries two per
core, as before): it is exactly the
DDA the hardware is built for — config loaded once when the sound starts, then
one POP per output sample gives the sample address. *Measured* at 10 Thumb
instructions down to 7 on the old core — a figure that needs retaking for
Cortex-M33, whose Thumb-2 encoding may well close the gap (§8).

**MIDI.** `parse_midi()` (`midi.c:131`) `calloc`s a track array and then
`realloc`s the event array **once per event**. `midi_event_type` is 16 bytes; the
title theme is *measured* at 436 events (~7 KB) and the largest resource is 12.7
KB raw, so several times that. **The reason this is not optional has changed
but the conclusion has not.** It used to be that the heap did not fit; on 520 KB
it fits with room to spare. It is now simply that §0 forbids a heap, and
`realloc`-per-event is the single worst offender in the tree. Pre-parse at build
time into const tables; the `sysex.data`/`meta.data` pointers already point into
the resource, which is itself in flash, so they stay valid.

The *other* malloc in `midi.c` is **already fixed**, and it was not a
theoretical problem. `midi_callback()` allocated its OPL scratch buffer inside
the audio callback — which on this target is the I2S DMA interrupt on core 1 —
and used the result with no NULL check. Address 0 is bootrom on this part as on
the last: the
generate writes are dropped and the mix loop then reads ROM contents as
full-scale samples. On the board that was a loud, ugly noise instead of music,
and it is what the first hardware bring-up ran into. It is now a static buffer
with an overflow counter the demo displays.

**OPL3. Done** — the firmware runs **DBOPL** at ~21M instructions/second, about
a sixth of a core on the old board, and the music plays in the demo. See §8 for the
measurements and for the Nuked optimisations kept behind `PICOPOP_OPL=nuked`.

The fallbacks that were on this list are no longer needed and are recorded only
so the reasoning is not repeated: pre-rendering all the music would be 1444 KB
at 8 kHz 4-bit IMA against 6.2 minutes of material, and the upstream Nuked v1.8
is 40% *more* expensive than the 1.7.4 the game shipped with.

**Order.** Music is done and measured on hardware. Digi is next: it is simple,
it is the last thing keeping `convert_digi_sound` alive, and it makes the game
feel finished.

### 3.4 Remaining allocation sites

**The goal is zero, not "few"** — see §0. After 3.1–3.3, section 9's audit
leaves these:

| site | treatment |
|---|---|
| `seg009.c:426` `open_dat` | static pool of 8 `dat_type` |
| `seg009.c:1403` `make_dialog_info` | static singleton (one dialog at a time) |
| `seg009.c:1707` `read_peel_from_screen` | the LIFO arena — already built, needs wiring |
| `seg009.c:2100` `init_digi`'s `SDL_AudioSpec` | static singleton |
| `seg009.c:3884, 3995` palette fade buffers | two statics, assert non-nesting |
| `seg009.c:926` `flip_not_ega` row buffer | one static 320-byte buffer |
| ~~`midi.c:524` OPL scratch in the audio callback~~ | **done** — static buffer; see 3.3 |
| `seg009.c:215, 256` directory listing | dead under this config — delete |
| `options.c:594` `exe_memory` | caller already commented out — delete |

**Size the arena from data, not from the worst case.** Peels are sprite-sized
(*measured* max 53×35, 48×45, 42×39) so ~2 KB each, capped at 50 by `add_peel`,
but the realistic concurrent count is far lower. Instrument the desktop build in
3.1 for the high-water mark; the current 28 KB is a guess.

**Then enforce it**: link with
`-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup` and
panicking wrappers, plus a CI grep of the `.map`. Two exemptions are required and
known: `alarm_pool_create_on_timer_with_unused_hardware_alarm` and
`cyw43_btbus_init` are the only heap callers in the current firmware (*measured*
by disassembly), both one-shot at init, both in the SDK.

Watch for two things while doing this: newlib's `printf`/`snprintf` touch the
heap for some conversions, and once the heap is gone the **stack** becomes the
remaining way to run out of memory — set both core stacks explicitly rather than
taking the defaults.

### 3.5 Hardware bring-up

**Done once, on the previous board, and now to be done again.** The demo ran on
the old board with the panel, the I2S DAC, the Bluetooth keyboard and the stick
all live at once, and the title theme playing from boot: *measured* 43–45 fps,
mixer load 38–41% average and 43–64% peak. The resource map and the init
ordering were correct as designed.

**None of that has been reproduced on the RP2350 yet.** What the exercise
established that still stands is architectural — the peripherals can coexist,
the init order works, the three bugs below were real. What it established about
timing and about register-level driver behaviour has to be re-established:
that is §3.7.

Three things came out of it, all now fixed and all worth remembering:

- **`malloc` in the audio callback.** `midi.c` allocated per block inside the
  I2S DMA interrupt on core 1 and used the result unchecked; address 0 being
  bootrom, that reads ROM as audio. It was the loud noise on first power-up.
  Static buffer now.
- **A zero identity address.** `bt_app.c` overwrote a working keyboard address
  with the all-zero "identity" BTstack reports for a device that advertises a
  *static random* address and so has no identity to resolve. Every reconnect
  then failed. Pre-existing in the vendored keyboard driver, not caused by the port.
- **No console commands.** The demo had no way to inspect or reset Bluetooth
  without a debugger. `s`/`r`/`n` are wired up now.

What remains:

1. **Re-run the demo on the RP2350** and retake every number (§3.7). Until that
   is done, nothing else on this list can be judged.
2. **The pairing does not persist** across reboots — see §8, and §3.6. Note the
   flash geometry and the `flash_safe_execute` path both want re-checking on the
   new part before this is diagnosed, or the diagnosis will chase the wrong bug.
3. Measure input latency end to end, and the BT link's jitter under load.
4. Then the game: title screen → attract mode → level 1 playable.

### 3.6 Fit and finish

- **Bluetooth pairing does not persist** (§8). This is the first thing to fix:
  it makes the board tedious to use, and it is the same flash-write mechanism
  saved games will need, so solving it once solves both.
- **Saved games.** `SDL_RWFromFile` currently fails cleanly. Needs a small flash
  key-value store — and every write must go through `flash_safe_execute` with
  core 1 locked out (§8).
- **Pairing UX** on a device whose only console is a serial port. `s`/`r`/`n`
  over the serial console exist now; the display could show link state too.
- **Joystick mapping** as an alternative to the keyboard; SDLPoP has its own
  joystick handling and `USE_AUTO_INPUT_MODE`.
- **Lock-key LEDs**: picosdl takes over the keyboard decoder's callbacks after
  `bt_app_setup()`, and `bt_app.c`'s LED handler is static. Two lines to restore.

### 3.7 Board migration to RP2350 — *the critical path*

No longer deferred: the board is a Pico 2 W as of 2026-08-29, and **nothing that
touches hardware can be trusted until this is done**. It is also the cheapest
item on the list to get wrong quietly, because most of it will appear to work.

**Already done** — `picosdl` commit `3582e17`, 2026-08-29:

- **The build targets `pico2_w`** (`picosdl/CMakeLists.txt:27`) and compiles
  clean, and it is the default.
- **The pad isolation latch is handled.** RP2350 powers up with pads isolated, so
  a raw FUNCSEL poke leaves a pin that will not drive. The display driver's mux
  now goes through `gpio_set_function()`
  ([`dispPioSt7789.c:193`](picosdl/vendor/pio-st7789/dispPioSt7789.c:193)),
  which clears it. This was the gotcha most likely to present as dead hardware,
  and it is closed.
- **The clock stays at 128 MHz**, not the RP2350 default of 150
  (`psdl_pico.h:30`). Deliberate: it makes the I2S divider
  exactly 125.0 at 8 kHz and it is the clock the ST7789 PIO timings were measured
  at. Keeping it means the divider analysis and the panel timings both carry over
  unchanged, which is worth more than the extra 22 MHz.
- **`flash.sh` uses the SDK's OpenOCD and `target/rp2350.cfg`** — the distro's
  0.12.0 has no RP2350 target at all, which would otherwise be a confusing first
  failure.
- **Flash and BSS have been re-measured** from the RP2350 build; see §7.

**Still open.**

- **The rest of the ST7789 driver.** The mux is fixed, but the driver still pokes
  PIO registers directly rather than going through the SDK, so its instruction
  encodings and register layout are unverified beyond "it compiles". This is the
  one remaining item that could be actually broken rather than merely unmeasured,
  and it gates every visual result.
- **It has not been run.** No frame rate, no mixer load, no confirmation that the
  three peripherals still coexist. Everything below waits on this.
- **Resource map.** Three PIO blocks instead of two, twelve state machines
  instead of eight. Worth revisiting rather than carrying over — the contention
  that had to be designed around may simply not exist now. Not urgent, since the
  existing map is a valid subset of the new one.
- **Errata.** Check the current RP2350 errata list against what this design uses
  before debugging anything — in particular the known input-pad behaviour
  affecting GPIOs held low through an internal pull-down, which the stick and any
  button inputs would be exposed to. *Unverified against the datasheet revision
  in hand; check before trusting.*

**Numbers still to retake.** The two static ones are done. The two that need a
running board are not, and they are the ones the plan's conclusions rest on:

| number | status |
|---|---|
| flash `.text` | **retaken**: 1,701,508 B, down from 1,715,168 B (§7) |
| BSS | **retaken**: 223,960 B, down from 224,344 B (§7) |
| mixer load | **stale** — 38–41% avg, 43–64% peak, on a Cortex-M0+ at 128 MHz. Same clock now, faster core, so expect a fall |
| frame rate | **stale** — 43–45 fps. Panel-push bound at an unchanged clock, so expect little movement |
| interpolator vs plain C | **stale** — §8's Thumb counts were taken for cortex-m0plus. Redo before relying on §8's conclusion |

**Done when** the demo runs on the Pico 2 W with panel, audio, keyboard and stick
live together, and the two stale rows above have been refilled from the board.

### 3.8 Eliminating the math library

Independent of 3.1–3.7 and doable at any point; do it before the first
link-enforced build so §0's CI check has something to pass. There are **four**
sites in the whole tree, none of them hard.

| site | what it does | treatment |
|---|---|---|
| `dbopl.cpp:1384–1420` `InitTables()` | `pow`, `log10`, `sin` building `ExpTable`, `SinTable`, `MulTable`, `WaveTable` at boot | emit as `static const` from a build-time generator — also reclaims **8.8 KB of RAM** (§7) |
| `midi.c:449–452` note-on | `powf`/`log2f` turning a MIDI note into an OPL block/F-number, **per note-on** | the input is `note - 81 + midi_semitones_higher`, a small integer: a const `{block, fnum}` lookup table indexed by semitone |
| `seg000.c:1310–1340` joystick | `atan2` + `fabs` to pick one of eight stick sectors | every comparison is against a fixed angle, so it is `raw_y * k` against `raw_x * k'` — integer cross-multiplies, no trig |
| `midi.c:250–252` | the same `powf`/`log2f` inside a `printf` debug dump of a note-on | compiled out with the debug dump |

`stb_vorbis.c` is the tree's other heavy `math.h` user and leaves the build
entirely (§9), taking 54 of the 64 allocation sites with it.

One near-miss, worth recording so it is not re-investigated:
`backend/pico/pio-i2s.c` includes `math.h` and computes its clock divider in
`float` (`PioI2S_calculateClockDivision`, `PicoI2S_verifyPIOClockDivision`). It
calls no libm function — the divides come from libgcc's soft-float — so it does
not violate the rule, and it runs once at init so it costs nothing. Drop the
vestigial include; leave the arithmetic alone.

Two things to check once these land, because they are the usual way libm creeps
back in: newlib's `printf` family pulls float conversion in for `%f`/`%g` (the
firmware should have no such format string), and `double`-typed constants in
otherwise integer expressions can emit soft-float helper calls. The
`float`/`double` audit already flagged under §8 ("Float in hot paths") is the
same work as this item and should be folded into it.

**Done when** the firmware links with no libm object in the `.map` and the music
still matches the reference render (`make -C picosdl/test music`).

---

## 4. Where things stand: SDLPoP

More porting groundwork was already done than the tree suggests. From the git
log: Ogg and SDL_image dropped and resources embedded (`4a2fac6`), software
rendering with `USE_RENDERER`/`USE_SCALING` off (`0a536c1`, `1023821`), the
embedded filesystem replaced by plain `static const` arrays (`d8c8af7`).

Currently on in `config.h`, confirmed through the preprocessor rather than by
grepping (`USE_REPLAY` has a `#define` line but sits inside
`#ifdef USE_QUICKSAVE`, so it is *off*): `USE_FADE`, `USE_COPYPROT`, `USE_FLASH`,
`USE_TEXT`, `USE_FAKE_TILES`, `USE_JUMP_GRAB`, `USE_AUTO_INPUT_MODE`,
`USE_DARK_TRANSITION`. Off: `USE_MENU`, `USE_SCREENSHOT`, `USE_LIGHTING`,
`USE_ALPHA`, `USE_FAST_FORWARD`, `USE_DEBUG_CHEATS`, `USE_QUICKSAVE`,
`USE_REPLAY`. That is a good target profile; keep it.

`resources.h/.c/.inc` in `SDLPoP/src/` are now the generated ones. The stale
`resources.o` and the other `.o` files in that directory are from an old host
build and should be cleaned.

### The problem this port is shaped around

*Measured*, by summing the decoded dimensions of every sprite:

```
KID 176.6 KB   PV 159.2 KB   VPALACE 91.7 KB   VDUNGEON 73.0 KB   GUARD 35.1 KB
FAT 33.5 KB    VIZIER 33.0 KB   SKEL 28.7 KB   SHADOW 28.4 KB   PRINCE 8.6 KB
TITLE 297.6 KB   font 4.0 KB                                   TOTAL 969 KB
```

An in-game level needs ~330 KB of that at once, and SDLPoP `malloc`s all of it at
level load, then blits it into **24bpp** offscreen surfaces at 192 KB each. That
is 330 KB plus 384 KB against 520 KB of SRAM — it does not fit on this board
either, and the extra RAM does not rescue it.

Both fixes were the same fix — go back to the indexed model the original used,
and stop keeping sprites in RAM at all — and both are now done (§2.1).

---

## 5. Target architecture

**Cores.** Core 0: game logic, drawing, display DMA, BTstack via
`cyw43_arch_threadsafe_background`. Core 1: audio — OPL3 synthesis, digi mixing,
filling the I2S double buffer. Audio is the only hard-real-time workload and the
only one worth isolating.

**Pixels.** One global 256-entry palette; sprites 8bpp indexed with their set's
row baked in. Consequences, all good: the framebuffer is 62.5 KB rather than 192
KB; the CLUT *is* the palette, so fades are nearly free; blits are byte copies
with a colour-key test.

**Sprites live in flash, unpacked and ready to use.** No decode step, no copy
into RAM, no allocation.

---

## 6. The mini-SDL2 layer

Built; see [`picosdl/README.md`](picosdl/README.md) for the module breakdown and
the design notes. The one decision worth restating here, because it is what keeps
the layer small: **it supports exactly one pixel format**, so there is no format
negotiation, no conversion and no generic blitter.

**It has two backends, `host` and `pico`.** The host one is currently a capture
stub used by the tests. Giving it a real SDL2 window is a small change to
`test/host_backend.c` and is worth doing *before* 3.1, because it makes the whole
of that work debuggable under gdb with a visible screen. That is the single
highest-leverage remaining piece of infrastructure.

### Where it plugs in

`seg009.c` is the entire platform layer (4168 lines) and already funnels drawing
through the handful of wrappers listed in 3.1. Those wrappers are the seam.
picosdl sits under them; the parts of `seg009.c` that exist only to bridge 8bpp
sprites into 24bpp surfaces get deleted rather than reimplemented.

---

## 7. Budgets

### Flash — *measured* on the RP2350 build

| | |
|---|---|
| `picosdl-demo` text | 1662 KB (1,701,508 B) |
| — CYW43 firmware blob | 234 KB (not code; cannot be removed) |
| — game resources | 1138 KB |
| — actual code | 303 KB |

That leaves **2434 KB of the 4 MB** for the game's own code. SDLPoP's `.text` is
152 KB on x86-64; ARM Thumb plus MIDI is an *estimated* 200–250 KB.

**The flash squeeze is over.** Roughly ten times the headroom the game needs,
which reopens three things that had been ruled out: mods, a filesystem, and a
second tile set. It also removes the only remaining argument for dropping
TITLE.DAT (§11).

Worth noting because it was the one place the new core could have gone the wrong
way: **it did not.** The M33 rebuild is 13,660 bytes *smaller* than the M0+ one
(1,701,508 against 1,715,168), so Thumb-2 code density is a small win here rather
than the loss that was the obvious risk. Most of the image is resources, so the
effect on actual code is proportionally larger than that figure suggests.

### RAM — *measured* on the RP2350 build

BSS is 223,960 bytes on the RP2350 build. Against the previous board's 264 KB
that left about 45 KB and was the binding constraint on the whole project;
against **520 KB** it leaves about **301 KB**.

| | |
|---|---|
| screen pool (2 × 320×200) | 128.0 KB — **halves**, §10 |
| LIFO arena | 28.0 KB |
| heap (`parse_midi` only) | 20.0 KB — **goes to zero**, §0 |
| BTstack + CYW43 state | ~20 KB |
| DBOPL wave/mul tables | 8.8 KB |
| surface headers (96) | 6.4 KB |
| DBOPL chip state | 4.3 KB |
| I2S DMA double buffer | 4.0 KB |
| event ring (64) | 3.5 KB |

The game's own globals are an *estimated* 60 KB. Against 301 KB free that is
comfortable — **the RAM squeeze is over too**, and with it the argument that
shaped much of sections 7 through 10.

The three savings identified while it was not comfortable are worth keeping
straight, because two of them are still going to happen and one is now a real
choice rather than a forced move:

1. **One screen buffer instead of two: 62.5 KB.** No longer needed for fit.
   **Still worth doing**, but now on its own merits rather than as a rescue —
   §10 has the argument, and it is about a per-frame 62.5 KB memcpy and about
   panel traffic, not about RAM.
2. **Pre-parse the MIDI at build time: 20 KB.** Happening regardless: §0 forbids
   the heap and this is the worst heap user in the tree (§3.3).
3. **Precompute DBOPL's wave tables into flash: 8.8 KB.** Happening regardless:
   §0 forbids libm and this is the biggest libm user (§3.8).

So 28.8 KB arrives as a side effect of the two hard constraints, and the
remaining 62.5 KB is optional. **Nothing in the memory budget now depends on
taking it**, which is a much better position to make that decision from than the
one §10 was written in.

---

## 8. Risks

### Retired

- ~~Palette unification~~ — the game already assigns rows; 741 sprites verified.
- ~~Sprite RAM~~ — sprites are in flash; 0 bytes of RAM.
- ~~Peel fragmentation~~ — LIFO arena, fragmentation structurally impossible.
- ~~Peripheral coexistence~~ — **confirmed on the old board**: the panel, the
  CYW43 radio and the I2S DAC run together, with the resource map and init
  ordering as designed and no DMA or PIO contention. RP2350 has *more* PIO and
  DMA resource, not less, so this is retired rather than reopened — but the
  resource map itself should be revisited (§3.7).
- ~~Frame budget~~ — *measured on the old board*: **43–45 fps** sustained with
  the music playing and Bluetooth connected. The panel push is the limit, as
  expected, and the blitters are not close to it. Being link-bound, this should
  carry over; retake it anyway (§3.7).
- ~~OPL CPU cost~~ — *measured on the old board*: **38–41% average, 43–64% peak**
  of one core with DBOPL, on a slower core than the one now fitted. See below.
- ~~Flash headroom~~ — 2434 KB free against an *estimated* 200–250 KB of game
  code (§7). Retired by the board change.
- ~~RAM headroom~~ — ~301 KB free against an *estimated* 60 KB of game globals
  (§7). Retired by the board change. This was the binding constraint on the
  entire project until 2026-08-29.

### Live

**OPL3 CPU cost — resolved by switching emulator.**

The firmware now uses **DBOPL**, DOSBox's OPL emulator, in place of Nuked. Both
are driven by SDLPoP's unmodified `midi.c`; `PICOPOP_OPL` in
`picosdl/CMakeLists.txt` switches between them. *Measured* on the game's title
theme with callgrind:

| | instr / output sample | chip state | host time |
|---|---|---|---|
| Nuked 1.7.4 stock | 9203 | 20776 B | 5.5% |
| Nuked + silent-slot skip + unfolded table | 5583 | 20776 B | 3.3% |
| Nuked 1.8 upstream | — | — | 8.6% |
| **DBOPL (in use)** | **938** | **4380 B** | **0.6%** |

That is ~21M instructions/second instead of ~123M — roughly a sixth of the old
core rather than all of it — and it saves RAM as well, because Nuked's 20 KB
chip struct goes away. On the M33 both emulators get cheaper and the 20 KB no
longer matters, so **the case for revisiting Nuked is now open** where before it
was closed: DBOPL was chosen partly under RAM and CPU pressure that no longer
exists, and it is the less exact model (see below). Retake the measurement in
§3.7 before deciding; the switch is one CMake variable either way.

**Confirmed on the old board.** The demo reported mixer load every second:
**38–41% average, 43–64% peak** of each audio block's budget, while the display
ran at 43–45 fps and Bluetooth was connected. The host extrapolation crossed an
ISA boundary and could have been wrong in either direction; it was not — which
is the encouraging precedent for the *second* ISA boundary now being crossed.

The peak was worth watching as the game got added: 64% left room for the digi
mixer but not much else on core 1. On a faster core that headroom should widen
considerably, and it is the single most useful number to retake first (§3.7),
because it is what decides whether core 1 can absorb anything beyond audio.

Part of the difference is structural: Nuked always runs its chip model at
49716 Hz and resamples to the output rate, while DBOPL scales its internal
counters and generates at the output rate directly. DBOPL is the less exact
model, which is the trade being made.

*How different does it sound?* Level-matched to 0.06 dB with no correction
applied (RMS 2409 against Nuked's 2392), spectrum within 0.1 dB below 3 kHz and
+1.3 dB above 6 kHz, and a **0.9995 loudness-envelope correlation** — same notes,
same times, same dynamics, slightly brighter top end. Judged by ear and accepted.
`make -C picosdl/test music OPL=nuked` renders the comparison.

*The Nuked optimisations are kept*, since `PICOPOP_OPL=nuked` still builds:

- **Silent-slot skipping** (`OPL3_SKIP_SILENT_SLOTS`). All four per-slot steps
  ran on all 36 slots every sample; a slot whose envelope has reached "off" is
  pinned at maximum attenuation, and the game keys only nine channels. 38%
  cheaper. Not bit-exact — a fully attenuated slot emits a one-LSB dither that
  this drops — but 73 dB down, which `make -C picosdl/test opl3` enforces.
- **Unfolded log-sine table.** The table is indexed as a 512-point table folded
  in half; unfolding it costs 512 bytes of flash and takes the lookup from 15
  Thumb instructions to 7 on cortex-m0plus — a count that does not transfer to
  Thumb-2 and should be redone. Bit-exact regardless.

*The SIO interpolator did not help either emulator on the old core* — checked by
compiling both forms for cortex-m0plus and counting Thumb instructions. **RP2350
has the same interpolators but a different instruction set, so this table is
now advisory rather than decisive; redo it (§3.7) before it is relied on.** The
structural argument in the paragraph below it does still hold, since it is about
the shape of the data flow rather than the encoding:

| | plain | via interpolator |
|---|---|---|
| phase accumulate | 4 | **9** |
| log-sine lookup | 15 (7 unfolded) | 7 |
| digi resampler step | 10 | **7** |

The interpolator is a DDA: one accumulator stepping by a constant, yielding
`base + ((accum >> shift) & mask)` per POP. It pays when the config is loaded
once and the loop then streams. An OPL chip is the opposite shape — dozens of
slots round-robin, each with its own phase accumulator — so the state has to be
pushed in and pulled out around every use, and the phase step costs *more* than
plain C. It is a clear win for **the digi resampler** (§3.3), where the config is
loaded once per sound.

**Flash writes versus core 1 — no longer a prediction; it has happened.**
*Observed on the board*: the Bluetooth pairing does not survive a reboot. After
a fresh pair the keyboard works and `stored_keyboard` holds the right address in
RAM, but on the next boot `load_stored_keyboard()` finds nothing and the firmware
goes back to scanning, so the keyboard has to be re-paired every time.

BTstack persists bonds through `btstack_tlv_flash_bank`, which erases and
programs flash, which requires core 1 to be locked out — and core 1 is running
the audio mixer. picosdl does its half (`multicore_lockout_victim_init()`, and
the whole audio path is `__not_in_flash_func`), so the mitigation is in place;
what has not been established is whether the write is actually reaching flash,
failing silently, or being refused. **Not yet diagnosed.** It is the top item in
§3.6, and the same mechanism will govern saved games.

**Float in hot paths** — **downgraded, not retired.** The M33 has a
single-precision FPU, so the `float` arithmetic in `midi.c`, the fade code and
the joystick path is no longer the problem it was. Two things survive: `double`
is still soft-float (`seg000.c:1310` uses it), and an FPU does not provide the
transcendentals §0 bans. Same work as **§3.8**; do it before 3.3.

**The runtime measurements are stale, and the firmware has not been run on the
new board.** *New, and currently the largest open risk.* The build was retargeted
on 2026-08-29 and the static figures retaken, but nothing has been executed on
the RP2350. The stale numbers are not wrong so much as unowned: they describe a
Cortex-M0+ and are being read as if they described a Cortex-M33.

The clock was deliberately held at 128 MHz across the change (§3.7), which
removes a great deal of this risk — the I2S divider and the ST7789 PIO timings
both carry over exactly, and they were the two most timing-sensitive things
here. What is left is the core, and the core only got faster.

So the danger is not that any single figure moves; most will move favourably.
It is that a *conclusion* drawn from a comparison between two of them silently
inverts. §8's interpolator analysis is the clearest case: it compares Thumb
instruction counts on a core that is no longer fitted, and it currently decides
the design of the digi resampler (§3.3). **Do not treat the DBOPL-versus-Nuked
decision, the frame budget, or the interpolator conclusion as settled until the
board has been run.**

---

## 9. Eliminating dynamic allocation

Goal: no heap at all, enforced at link time — see §0 for why this is a
requirement rather than a target, and §3.8 for its companion, the math library. The tree has 64 allocation call
sites; **44 are already unreachable** under this config, and `stb_vorbis.c` alone
accounts for 54 of the 64 and can leave the build.

Of the 20 live ones:

| group | status |
|---|---|
| (a) dead under this config — delete | pending, part of 3.1/3.4 |
| (b) moved to build time (the sprite decode chain) | **done** — §2.1 |
| (c) eliminated by algorithm change | picosdl side **done**; game side pending (3.1, 3.3) |
| (d) static singletons | pending, 3.4 |
| (e) static pools | pending, 3.4 |
| (f) the peel arena | picosdl side **done**; wiring pending (3.4) |

Group (c) is the one that matters most, and only its picosdl half is finished:
`PSDL_BlitMirrored`, `PSDL_BlitXor` and `PSDL_BlitMono` exist, but SDLPoP still
calls `hflip()` and `blit_xor()`. `convert_digi_sound` — the ~1.5 MB one — is
untouched.

---

## 10. Why the game needs two screen surfaces — and whether one will do

This was written when 62.5 KB decided whether the project fit at all. It no
longer does (§7), so **the conclusion below is now a recommendation rather than a
requirement**. It is kept in full because the analysis is what tells you what the
second surface is actually for, and that question does not go away when the RAM
does — it still governs a per-frame 62.5 KB memcpy and how much data is pushed
to the panel. Read the recommendation at the end with the board change in mind.

The code has been read; what follows is what it actually does.

### What the two surfaces are

`onscreen_surface_` is picosdl's window surface (`psdl_video.c`) — 320×200 8bpp,
allocated from the screen pool, and **the buffer the panel DMA reads**.
`offscreen_surface` is created by `make_offscreen_buffer()` (`seg009.c:892`) and
is the game's compositing target. Its size depends on the mode:

- gameplay: `rect_top` = **320×192** (`seg003.c:32`), 61,440 bytes;
- titles and cutscenes: `screen_rect` = **320×200** (`seg000.c:1945`,
  `seg001.c:585`, `seg001.c:649`), 64,000 bytes.

They are never both alive: every site frees the old one before making the new.
So the true peak is **two** surfaces, 128,000 bytes, and that is the 128.0 KB row
in §7. Note that only an exact 320×200 request hits the pool
(`psdl_surface.c:146`), so the 320×192 gameplay offscreen would today fall
through to the general allocator — a small mismatch to fix whichever way this
question is decided.

### The four things that genuinely need both

The naive reason — "it double-buffers the frame" — is **not** one of them.
`draw_tables()` (`seg008.c:1364`) recomposites the whole of `rect_top` from
scratch every frame (peels, wipes, backtable, midtable, foretable), so the
offscreen holds no history that drawing straight to the screen would lose. What
does need two images is this, and only this:

1. **The panel DMA runs concurrently with drawing.** `psdl_backend_video_present`
   kicks `dispDrawBuffer` and returns; the wait for the previous transfer happens
   at the *start* of the next present
   (`backend/pico/psdl_pico_video.c:98–108`). That overlap is what buys the
   *measured* 43–45 fps (§8). Compositing directly into the buffer the DMA is
   reading would tear the whole frame, every frame — the panel would show
   background with no prince in it. **This is the real reason, and it is a
   picosdl-side property, not a SDLPoP one.**

2. **`transition_ltr()`** (`seg000.c:2042`) reveals the new room over the old one
   in 2-pixel columns, 160 steps, idling between each. It holds the complete old
   image and the complete new image simultaneously, by construction.

3. **`set_bg_attr()`'s flash** (`seg009.c:3800–3820`) fills the *onscreen*
   surface with a single flash colour and then blits the offscreen over it with
   black colour-keyed out. With one buffer the fill destroys the image it is
   about to composite against.

4. **Dialog save-under.** `showmessage_any_key()` (`seg000.c:1820`) and
   `save_recorded_replay_dialog()` (`replay.c:780`) blit *onscreen → offscreen*
   over `copyprot_dialog->peel_rect`, using the offscreen as scratch storage for
   the pixels the dialog is about to cover.

Two more uses look like reasons and are not:

- **`upside_down`.** `flip_screen(offscreen_surface)` (`seg000.c:939/946`,
  `seg003.c:297/301`, `seg009.c:3812/3823`) reverses the offscreen's rows *in
  place*, copies out, then reverses it back. It needs a scratch row, not a
  scratch frame — and picosdl already has `PSDL_BlitMirrored`, so the mirror can
  move into the copy or into the panel scan-out and the double flip disappears.
- **Fades.** `fade_in_2`/`make_pal_buffer_fadein` operate on the palette, and on
  this design the palette *is* the CLUT (§5), so they need no second image at
  all.

### Can one buffer work?

Yes, and the route runs through picosdl rather than through SDLPoP. The
observation that makes it work: **`dispDrawBuffer` already takes a `Rect`**, and
SDLPoP already computes exactly the right rectangles.

The gameplay loop is `draw_tables()` into the offscreen, then
`copy_screen_rect()` for each accumulated dirty rect (`seg000.c:940–948`,
`seg001.c:126–128`). Replace that copy with a *panel push of the same rect*:
`draw_tables()` composites straight into the framebuffer, and each drect is then
DMA'd to the panel. The second buffer is gone, and so is a 62.5 KB memcpy per
frame — and the panel traffic drops from a full 320×200 to the dirty area, which
should *raise* the frame rate rather than lower it. The dirty-rect machinery
already exists and is already correct; nothing in the game changes.

Writes outside the drects are still a concern in principle, since `draw_tables()`
touches the whole room. In practice those writes are idempotent — same tables,
same tiles, same values — so a torn read there is invisible. Anything that
genuinely changed is by definition inside a drect, and that is what gets pushed.
This needs confirming on the board rather than assuming; it is the one real
unknown in the scheme.

The four cases above, plus the title and cutscene path, then need handling
individually:

| case | with one buffer |
|---|---|
| async panel DMA | dirty-rect push, as above; sequence the composite after the previous rect's transfer completes |
| `transition_ltr` | it is a left-to-right column wipe of a freshly drawn room. Draw the new room into the framebuffer once, then push columns to the panel progressively — the panel's own memory becomes the second image, which is what it was always standing in for |
| `set_bg_attr` flash | it flashes the whole screen one colour and restores. On a CLUT design this is a **palette** operation, not a pixel one: point every entry at the flash colour, push nothing, restore. Cheaper than today |
| dialog save-under | `peel_rect` is dialog-sized, not screen-sized. This is exactly what the LIFO arena (§3.4) is for |
| title / cutscene builds | these fade in from black via the palette and are drawn once, so they can composite in place |

### Recommendation

**Still do it, still as part of 3.1 — but for the frame rate, not for the RAM.**

The RAM justification is gone: 62.5 KB against 301 KB free is not worth a day's
work, let alone a risk. What survives the board change is the other half of the
argument, and it was always the more interesting half:

- `copy_screen_rect()` is a **62.5 KB memcpy every frame** that the dirty-rect
  push deletes outright.
- The panel push drops from a full 320×200 to the dirty area. The frame rate is
  *measured* panel-bound (§8), so this is the one change in the plan that
  attacks the actual limit rather than a budget.

Neither of those got cheaper when the board got bigger. If anything the case
strengthened: on a faster core the panel link is an even larger share of the
frame.

The timing argument also survives unchanged. Every call site involved is being
opened by 3.1 anyway, and retrofitting the dirty-rect push after the blitters are
rewired means doing the same work twice. The order is: give the host backend a
real SDL2 window (§6), rewire the blitters (3.1), and while the call sites are
open replace `copy_screen_rect()` with a panel push, drop `make_offscreen_buffer`
to a stub returning the screen surface, and set `PSDL_SCREEN_BUFFERS` to 1.

What *has* changed is the fallback position, and for the better. The risk to
watch is the tearing assumption — that writes outside the drects are idempotent.
If it fails on hardware there are now two retreats instead of one: the
**one-band scratch buffer** (composite a strip, push it, repeat) at a few KB, or
simply **keeping the second surface**, which no longer costs anything the project
needs. Previously that second option did not exist. So this is now a change that
can be attempted cheaply and abandoned cheaply, which is the best reason of all
to try it.

---

## 11. Open questions

*The board question is closed — it is a Pico 2 W.* What remains:

- **Is the ST7789 PIO program RP2350-clean?** Still unknown, and now **blocking**
  rather than hypothetical: it pokes registers directly, and there is no working
  display until it is answered. Top of §3.7.
- **What system clock?** The old board ran 128 MHz, chosen so one clock satisfied
  both PIO programs. RP2350 goes to 150 MHz and the I2S divider is fixed-point,
  so the joint constraint has to be re-solved rather than rescaled (§3.7). Faster
  is not automatically better here — a clock that divides cleanly for I2S is
  worth more than the top of the range.
- **DBOPL or Nuked, now that RAM and CPU are not scarce?** Nuked is the more
  exact model and was rejected under pressure that no longer exists (§8). One
  CMake variable. Decide after §3.7's measurements, not before.
- **Saved games at all**, or is a from-level-1-every-time arcade cabinet
  acceptable? Affects 3.6 and the flash-write safety work. Unchanged by the
  board, except that the flash geometry differs and the bond-persistence bug has
  to be re-diagnosed on the new part first.
- ~~**How much of the title sequence is worth keeping?**~~ **Closed.** TITLE.DAT
  is 297.6 KB of the 969 KB of sprites, and dropping it was the easiest large
  saving when flash was tight. Against 2434 KB free it saves nothing anyone
  needs. Keep it.

**New, and worth deciding deliberately rather than by drift: what to do with the
headroom.** The plan spent most of its life defending a budget that has roughly
tripled in RAM and sextupled in flash. The mods, filesystem and second tile set
that §7 used to rule out are all affordable now. None of them are on the critical
path and none should displace it — but the constraint that was implicitly saying
*no* to every such idea has been removed, and that is worth noticing before it
gets rediscovered one feature at a time.
