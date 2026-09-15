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
DEMO   := $(ROOT)/src/demo
DATA   ?= $(ROOT)/SDLPoP/data
GEN    ?= $(ROOT)/generated
REFDIR := $(GEN)/resources

CFLAGS := -std=c11 -Wall -Wextra -g -I $(PSDL)/include -I $(PSDL)/src -I $(REFDIR)
LIB    := $(wildcard $(PSDL)/src/psdl_*.c)
OUT    := /tmp/picopop-test

.PHONY: all verify digi mirror music budget opl3 clean

all: $(RESOURCES) verify digi mirror

# The generated game resources, and the reference the converter's output is
# checked against. Both come from PR/src/bin - see convert.py.
RESOURCES := $(REFDIR)/resources.c
REFERENCE := $(REFDIR)/reference.bin

# Which OPL emulator the music targets use, matching PICOPOP_OPL in
# src/CMakeLists.txt. dbopl is what the firmware ships.
#   make music OPL=nuked
# C and C++ have to be compiled separately - resources.c uses C compound
# literals, which are not addressable rvalues in C++ - then linked with the C++
# driver when DBOPL is in play.
MUSIC_CSRC = $(PSDL)/test/host_backend.c $(DEMO)/game_glue.c $(LIB) \
             $(POP)/midi.c $(RESOURCES)
MUSIC_INC  = -I $(PSDL)/include -I $(PSDL)/src -I $(DEMO) -I $(REFDIR) \
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



# Check the build-time sprite conversion end to end: every sprite blitted from
# its const flash surface, mapped back through the global palette, and compared
# against the original BMPs. The scratch surface is bigger than any sprite, so
# the arena is enlarged for this build only - on the device nothing blits a
# 512x512 image.
verify: $(REFERENCE)
	$(CC) $(CFLAGS) -DPSDL_ARENA_BYTES=400000 -I $(POP) \
	    -o $(OUT)-verify verify_resources.c $(PSDL)/test/host_backend.c $(LIB) $(RESOURCES)
	$(OUT)-verify

# Render one of the game's Adlib tunes to a WAV, through the same midi.c and
# Nuked OPL3 the Pico runs. Checks the music path works and times the synthesis,
# which is the number PLAN.md's largest open question turns on.
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
	    -I $(DEMO) -I $(POP) \
	    -o $(OUT)-opl3-$$skip render_music.c $(PSDL)/test/host_backend.c \
	    $(DEMO)/game_glue.c $(LIB) $(POP)/midi.c \
	    $(POP)/opl3.c $(RESOURCES) -lm || exit 1; \
	  echo "--- OPL3_SKIP_SILENT_SLOTS=$$skip ---"; \
	  $(OUT)-opl3-$$skip 10054 $(OUT)-opl3-$$skip.wav | grep -E "synthesis|RMS"; \
	done
	$(CC) $(CFLAGS) -o $(OUT)-wavdiff wav_diff.c -lm
	$(OUT)-wavdiff $(OUT)-opl3-0.wav $(OUT)-opl3-1.wav $(OPL3_MIN_DB)

$(REFERENCE) $(RESOURCES):
	@$(ROOT)/tools/assets/build-resources.sh --reference $(DATA) $(GEN)

clean:
	rm -rf $(OUT) $(OUT)-asan $(OUT)-verify $(OUT)-music $(OUT)-budget \
	       $(OUT)-opl3-* $(OUT)-wavdiff $(OUT)-*.objs

# Check the on-the-fly digi resampler against the 16x expansion it replaced.
digi: $(RESOURCES)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -I $(PSDL)/include -I $(PSDL)/src \
	  -I $(POP) -I $(REFDIR) -o /tmp/picopop-digi-check \
	  digi_check.c $(LIB) $(PSDL)/test/host_backend.c $(RESOURCES) -lm
	/tmp/picopop-digi-check

# Check that a mirrored sprite is an exact reversal - the path a right-facing
# character is drawn through.
mirror: $(RESOURCES)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -I $(PSDL)/include -I $(PSDL)/src \
	  -I $(POP) -I $(REFDIR) -o /tmp/picopop-mirror-check \
	  mirror_check.c $(LIB) $(PSDL)/test/host_backend.c $(RESOURCES) -lm
	/tmp/picopop-mirror-check
