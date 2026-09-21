# Tests for Prince of Persia's own data, run on the desktop.
#
# These live here rather than in picosdl because every one of them reads the
# game: the converted sprites, the digi sounds, the Adlib tunes. picosdl is a
# library meant to be reused by other games and must not depend on any of it -
# its own self-contained tests are in picosdl/test.
#
#   make -C tools/tests verify   every sprite against an independent reference
#   make -C tools/tests digi     the resampler against the expansion it replaced
#   make -C tools/tests mirror   mirrored sprites are exact reversals
#   make -C tools/tests music    render a tune to a WAV and report its level
#   make -C tools/tests budget   the OPL emulator's cost per output sample
#   make -C tools/tests opl3     the Nuked optimisations, checked bit-exactly
#   make -C tools/tests all      verify + digi + mirror
#
# reference.bin is produced by the asset pipeline in ../assets; see its README.

CC     ?= cc
CXX    ?= c++
ROOT   := ../..
PSDL   := $(ROOT)/picosdl
POP    := $(ROOT)/SDLPoP/src
# game_glue.c lives here rather than beside the demo it was written for. The demo
# moved into picosdl and stopped needing it - its music is its own now - but these
# tests still drive SDLPoP's midi.c on the host, and that is what the glue is for.
GLUE   := $(dir $(lastword $(MAKEFILE_LIST)))
DATA   ?= $(ROOT)/SDLPoP/data
GEN    ?= $(ROOT)/generated
REFDIR := $(GEN)/resources

# Defines every target here needs, in one place - the targets below do not all use
# $(CFLAGS), and four copies of this drift.
#
# PSDL_HAVE_AUDIO: these all link picosdl's host backend, which implements the audio
# interface, so they must say so or picosdl compiles its no-audio stubs and they
# clash with the backend's. See the note in ../Makefile.
PSDL_DEFS := -DPSDL_HAVE_AUDIO=1

CFLAGS := -std=c11 -Wall -Wextra -g $(PSDL_DEFS) \
          -I $(PSDL)/include -I $(PSDL)/src -I $(REFDIR)
LIB    := $(wildcard $(PSDL)/src/psdl_*.c)
OUT    := /tmp/picopop-test

.PHONY: all verify digi mirror music budget opl3 dbopl-tables midi-fnum joystick midi-pool midi-tables datfile clean

all: $(RESOURCES) verify digi mirror dbopl-tables midi-fnum joystick midi-tables

# The generated game resources, and the reference the converter's output is
# checked against. Both are produced by ../assets - see convert.py.
RESOURCES := $(REFDIR)/resources.c
REFERENCE := $(REFDIR)/reference.bin

# Which OPL emulator the music targets use. dbopl is what the firmware ships and
# now the only thing it can ship; nuked builds here and nowhere else, which is
# what keeps the comparison in TODO.md reproducible.
#   make music OPL=nuked
# C and C++ have to be compiled separately - resources.c uses C compound
# literals, which are not addressable rvalues in C++ - then linked with the C++
# driver when DBOPL is in play.
MUSIC_CSRC = $(PSDL)/test/host_backend.c $(GLUE)/game_glue.c $(LIB) \
             $(POP)/midi.c $(RESOURCES)
MUSIC_INC  = $(PSDL_DEFS) -I $(PSDL)/include -I $(PSDL)/src -I $(GLUE) -I $(REFDIR) \
             -I $(POP)
# Overridable so the same tune can be rendered at a different optimisation level
# and the results compared bit-for-bit; the firmware builds -Os (MinSizeRel).
MUSIC_OPT ?= -O2

define build_music
	rm -rf $(1).objs && mkdir -p $(1).objs
	for f in $(2) $(MUSIC_CSRC); do \
	  $(CC) -std=c11 $(MUSIC_OPT) -g $(OPL_FLAGS) $(MUSIC_INC) -c $$f \
	    -o $(1).objs/$$(basename $$f .c).o || exit 1; done
	for f in $(OPL_SRC); do \
	  case $$f in \
	    *.cpp) $(CXX) -std=c++17 $(MUSIC_OPT) -g -fno-exceptions -fno-rtti $(OPL_FLAGS) \
	             $(MUSIC_INC) -c $$f -o $(1).objs/$$(basename $$f .cpp).o ;; \
	    *.c)   $(CC) -std=c11 $(MUSIC_OPT) -g $(OPL_FLAGS) $(MUSIC_INC) -c $$f \
	             -o $(1).objs/$$(basename $$f .c).o ;; \
	  esac || exit 1; done
	$(OPL_LINK) $(MUSIC_OPT) -o $(1) $(1).objs/*.o -lm
endef

OPL ?= dbopl
ifeq ($(OPL),dbopl)
  OPL_SRC   := $(POP)/dbopl.cpp $(POP)/dbopl_adapter.cpp
  OPL_FLAGS := -DPICOPOP_USE_DBOPL=1
  OPL_LINK  := $(CXX)
else
  OPL_SRC   := $(POP)/opl3.c
  OPL_FLAGS := -DPICOPOP_USE_DBOPL=0
  OPL_LINK  := $(CC)
endif



# Check tools/assets/datfile.py against Princed Resources: every resource as
# bytes, and every image as pixels. Not part of `all`, because it needs a PR
# checkout and an extraction, and the build has neither - see the header of
# datfile_check.py for how to produce them. It is the only independent check on
# the decoder, so run it after changing datfile.py.
#
#   make datfile PR_EXTRACT=/path/to/extraction
PR_EXTRACT ?= $(GEN)

datfile:
	python3 datfile_check.py $(DATA) $(PR_EXTRACT)

# Check the build-time sprite conversion end to end: every sprite blitted from
# its const flash surface, mapped back through the global palette, and compared
# against the original BMPs. The scratch surface is bigger than any sprite, so
# the arena is enlarged for this build only - on the device nothing blits a
# 512x512 image.
verify: $(REFERENCE)
	$(CC) $(CFLAGS) -DPSDL_ARENA_BYTES=400000 -I $(POP) \
	    -DREFERENCE_PATH='"$(REFERENCE)"' \
	    -o $(OUT)-verify verify_resources.c $(PSDL)/test/host_backend.c $(LIB) $(RESOURCES)
	$(OUT)-verify

# Render one of the game's Adlib tunes to a WAV, through the same midi.c and
# Nuked OPL3 the Pico runs. Checks the music path works and times the synthesis,
# which is the number the OPL question in TODO.md turns on.
#
#   make music              the title theme
#   make music ID=10056     the winning theme
ID  ?= 10054
WAV ?= /tmp/pop-music-$(ID).wav

music: $(RESOURCES)
	$(call build_music,$(OUT)-music,render_music.c)
	$(OUT)-music $(ID) $(WAV)

# Render every tune in the game to the end and price the result under the
# encodings worth considering. Answers "what would pre-rendering the music cost
# in flash", which is the standing fallback if OPL3 is too expensive.
budget: $(RESOURCES)
	$(call build_music,$(OUT)-budget,music_budget.c)
	$(OUT)-budget

# Check the OPL3 silent-slot optimisation against the stock emulator: render the
# same tune both ways and require the difference to stay inaudible. Guards the
# fidelity claim in opl3.c so it cannot silently regress.
OPL3_MIN_DB ?= 60

opl3: $(RESOURCES)
	@for skip in 0 1; do \
	  $(CC) $(CFLAGS) -O2 -DOPL3_SKIP_SILENT_SLOTS=$$skip \
	    -I $(GLUE) -I $(POP) \
	    -o $(OUT)-opl3-$$skip render_music.c $(PSDL)/test/host_backend.c \
	    $(GLUE)/game_glue.c $(LIB) $(POP)/midi.c \
	    $(POP)/opl3.c $(RESOURCES) -lm || exit 1; \
	  echo "--- OPL3_SKIP_SILENT_SLOTS=$$skip ---"; \
	  $(OUT)-opl3-$$skip 10054 $(OUT)-opl3-$$skip.wav | grep -E "synthesis|RMS"; \
	done
	# wav_diff.c is picosdl's, not ours - it compares two WAVs and knows nothing
	# about any game. Named by path rather than expected in this directory, which
	# is what broke this target when the tests moved out of picosdl/test.
	$(CC) $(CFLAGS) -o $(OUT)-wavdiff $(PSDL)/test/wav_diff.c -lm
	$(OUT)-wavdiff $(OUT)-opl3-0.wav $(OUT)-opl3-1.wav $(OPL3_MIN_DB)

# Check the integer stick-sector tests against the atan2() they replaced. Needs
# no game data and no picosdl - it includes one header and links libm for the
# reference side only.
joystick:
	$(CC) $(CFLAGS) -O2 -I $(POP) -o $(OUT)-joystick joystick_check.c -lm
	$(OUT)-joystick

# Check the build-time DBOPL tables against the ones dbopl.cpp computes at boot.
#
# tools/assets/dbopl_tables.c holds a copy of two loops from dbopl.cpp, which is
# the only way to generate them without a translation step and also the only way
# for them to drift. So render the same tune twice - once with the generated
# tables, once with the runtime ones - and require the WAVs to be identical byte
# for byte. That covers the tables through the code that actually reads them,
# rather than comparing arrays and hoping they are wired up the same way.
#
# Note what it does not cover: the firmware's own libm is newlib's, not the
# host's, so a table the board would have computed could differ from this one in
# the last bit of an entry. That is a difference of -90 dB in a 16-bit wave
# table, and DBOPL's own source calls the exact values an approximation choice.
DBOPL_TABLES_INC = $(ROOT)/generated/dbopl_tables.inc
MIDI_FNUM_INC    = $(ROOT)/generated/midi_fnum_table.inc

$(DBOPL_TABLES_INC): $(ROOT)/tools/assets/dbopl_tables.c $(ROOT)/tools/assets/gen-table.sh
	$(ROOT)/tools/assets/gen-table.sh $< $(ROOT)/generated dbopl_tables.inc

$(MIDI_FNUM_INC): $(ROOT)/tools/assets/midi_fnum_table.c $(ROOT)/tools/assets/gen-table.sh
	$(ROOT)/tools/assets/gen-table.sh $< $(ROOT)/generated midi_fnum_table.inc

dbopl-tables: $(RESOURCES) $(DBOPL_TABLES_INC)
	@$(call build_music,$(OUT)-dbopl-runtime,render_music.c)
	@$(MAKE) -s --no-print-directory dbopl-tables-const
	@$(OUT)-dbopl-runtime 10054 $(OUT)-dbopl-runtime.wav > /dev/null
	@$(OUT)-dbopl-const   10054 $(OUT)-dbopl-const.wav   > /dev/null
	@cmp $(OUT)-dbopl-runtime.wav $(OUT)-dbopl-const.wav \
	  && echo "OK: the generated DBOPL tables render identically to the computed ones"

dbopl-tables-const: MUSIC_OPT += -DPICOPOP_DBOPL_CONST_TABLES=1 -I $(ROOT)/generated
dbopl-tables-const:
	$(call build_music,$(OUT)-dbopl-const,render_music.c)

# Same idea for the MIDI note -> OPL {block, F-number} table: render a tune with
# the generated table and with the powf()/log2f() it replaced, and require the
# WAVs to be identical. A wrong entry here is a wrong note, so this is the test
# that has to pass before trusting the table at all.
midi-fnum: $(RESOURCES) $(MIDI_FNUM_INC)
	$(CC) $(CFLAGS) -O2 -I $(ROOT)/generated -o $(OUT)-fnum-check \
	  midi_fnum_check.c -lm
	$(OUT)-fnum-check
	@$(call build_music,$(OUT)-fnum-float,render_music.c)
	@$(MAKE) -s --no-print-directory midi-fnum-table
	@$(OUT)-fnum-float 10054 $(OUT)-fnum-float.wav > /dev/null
	@$(OUT)-fnum-table 10054 $(OUT)-fnum-table.wav > /dev/null
	@cmp $(OUT)-fnum-float.wav $(OUT)-fnum-table.wav \
	  && echo "OK: the generated MIDI F-number table renders identically to powf/log2f"

midi-fnum-table: MUSIC_OPT += -DPICOPOP_MIDI_FNUM_TABLE=1 -I $(ROOT)/generated
midi-fnum-table:
	$(call build_music,$(OUT)-fnum-table,render_music.c)

# The build-time MIDI tables against the runtime parser they replace.
#
# Renders every tune in the game to its end, twice - once parsing at run time,
# once reading the pre-parsed tables from flash - and requires the two digests to
# match line for line. 6761 events across 22 tunes, so a wrong delta_time or a
# misplaced track boundary anywhere shows up here.
MIDI_TABLES_INC = $(ROOT)/generated/midi_tables.inc

$(MIDI_TABLES_INC): $(ROOT)/tools/assets/midi_tables.c \
                    $(ROOT)/tools/assets/gen-midi-tables.sh \
                    $(POP)/midi.c $(RESOURCES)
	$(ROOT)/tools/assets/gen-midi-tables.sh $(ROOT)/generated

midi-tables: $(RESOURCES) $(MIDI_TABLES_INC)
	@$(call build_music,$(OUT)-mt-parse,midi_tables_check.c)
	@$(MAKE) -s --no-print-directory midi-tables-flash
	@$(OUT)-mt-parse > $(OUT)-mt-parse.txt
	@$(OUT)-mt-flash > $(OUT)-mt-flash.txt
	@diff -u $(OUT)-mt-parse.txt $(OUT)-mt-flash.txt > /dev/null \
	  && echo "OK: all $$(grep -c ended $(OUT)-mt-parse.txt) tunes render identically from the build-time tables" \
	  || { echo "FAIL: the pre-parsed tables do not match the parser"; \
	       diff -u $(OUT)-mt-parse.txt $(OUT)-mt-flash.txt | head -20; exit 1; }

midi-tables-flash: MUSIC_OPT += -DPICOPOP_MIDI_TABLES=1 -I $(ROOT)/generated
midi-tables-flash:
	$(call build_music,$(OUT)-mt-flash,midi_tables_check.c)

# What the MIDI event pool actually has to hold: events and tracks per tune, and
# the largest value each field of midi_event_type ever takes. Parses every MIDI
# resource in the game through the real parse_midi(), so the numbers are the
# firmware's own rather than an estimate.
midi-pool: $(RESOURCES)
	$(call build_music,$(OUT)-midi-pool,midi_pool_probe.c)
	$(OUT)-midi-pool

$(REFERENCE) $(RESOURCES):
	@$(ROOT)/tools/assets/build-resources.sh --reference $(DATA) $(GEN)

clean:
	rm -rf $(OUT) $(OUT)-asan $(OUT)-verify $(OUT)-music $(OUT)-budget \
	       $(OUT)-dbopl-* $(OUT)-fnum-* $(OUT)-joystick $(OUT)-mt-* $(OUT)-midi-pool \
	       $(OUT)-opl3-* $(OUT)-wavdiff $(OUT)-*.objs

# Check the on-the-fly digi resampler against the 16x expansion it replaced.
digi: $(RESOURCES)
	$(CC) -std=c11 -O2 -g -Wall -Wextra $(PSDL_DEFS) -I $(PSDL)/include -I $(PSDL)/src \
	  -I $(POP) -I $(REFDIR) -o /tmp/picopop-digi-check \
	  digi_check.c $(LIB) $(PSDL)/test/host_backend.c $(RESOURCES) -lm
	/tmp/picopop-digi-check

# Check that a mirrored sprite is an exact reversal - the path a right-facing
# character is drawn through.
#
# The arena is enlarged for this build only, as in `verify`. The device never
# allocates to mirror - PSDL_BlitMirrored reverses during the blit, which is why
# it replaced SDLPoP's hflip() - but this test holds two full-size copies of a
# sprite at once to compare them. Left at the device budget it silently reports
# the largest sprites as "skipped" rather than failing, so the sprites most
# worth checking are the ones that quietly stop being checked.
mirror: $(RESOURCES)
	$(CC) -std=c11 -O2 -g -Wall -Wextra $(PSDL_DEFS) -DPSDL_ARENA_BYTES=400000 \
	  -I $(PSDL)/include -I $(PSDL)/src \
	  -I $(POP) -I $(REFDIR) -o /tmp/picopop-mirror-check \
	  mirror_check.c $(LIB) $(PSDL)/test/host_backend.c $(RESOURCES) -lm
	/tmp/picopop-mirror-check
