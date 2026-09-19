# Prince of Persia on picosdl - desktop build.
#
# This builds the *game* against picosdl rather than against SDL2, with a
# headless backend that writes presented frames to disk. It is the loop the
# rendering work is meant to be developed in: everything except the panel, the
# DAC and the radio is the same code the firmware runs, so a rendering bug found
# here is a rendering bug on the board.
#
#   make -C tools          build
#   make -C tools run      build and run, dumping frames to tools/frames/
#   make -C tools asan     the same under AddressSanitizer and UBSan
#
# There is deliberately no SDL2 dependency. See tools/README.md.

ROOT    := ..
POP     := $(ROOT)/SDLPoP/src
PSDL    := $(ROOT)/picosdl

# The converted game resources. Built from the DAT files you supply in
# SDLPoP/data - see assets/build-resources.sh - and never committed, so this
# path is a build product, not a source file.
DATA    ?= $(ROOT)/SDLPoP/data
GEN     ?= $(ROOT)/generated
RES     := $(GEN)/resources
OUT     := picopop-host
FRAMES  ?= frames

CC      ?= cc
CXX     ?= c++

# -DPICOPOP=1 marks the port's own conditionals in the game sources.
# _GNU_SOURCE is what SDLPoP's own Makefile passes.
# PSDL_HAVE_AUDIO: this backend has an audio device - it captures the mixer to a
# WAV - so it must say so. Without it picosdl compiles its no-audio stubs and they
# clash with the ones host_backend.c provides. The input PSDL_HAVE_* macros are
# deliberately absent: the host has no joystick, pad or Bluetooth, and an undefined
# macro is 0 in #if, which is the right answer.
# PICOPOP_ONE_SCREEN_BUFFER has to match the firmware. A host that buffers
# differently from the board cannot see the class of bug that only one buffer
# produces - missing wipes, a frozen cutscene, black gameplay.
DEFS    := -D_GNU_SOURCE=1 -DPICOPOP=1 -DPICOPOP_USE_DBOPL=1 -DPSDL_HAVE_AUDIO=1 \
           -DPICOPOP_ONE_SCREEN_BUFFER=1 \
           $(DEFS_EXTRA)

INC     := -I $(PSDL)/include -I $(PSDL)/src -I $(POP) -I $(RES) -I .
WARN    := -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
           -Wno-missing-field-initializers
OPT     ?= -O1 -g3

CFLAGS   := -std=c11 $(OPT) $(WARN) $(DEFS) $(INC)
CXXFLAGS := -std=c++17 $(OPT) -fno-exceptions -fno-rtti $(WARN) $(DEFS) $(INC)

# ---------------------------------------------------------------- sources

# stb_vorbis.c is deliberately absent: it is 54 of the 64
# allocation sites in the tree and the Ogg path is compiled out anyway.
POP_SRC := \
	$(POP)/main.c      $(POP)/data.c      $(POP)/seg000.c   $(POP)/seg001.c \
	$(POP)/seg002.c    $(POP)/seg003.c    $(POP)/seg004.c   $(POP)/seg005.c \
	$(POP)/seg006.c    $(POP)/seg007.c    $(POP)/seg008.c   $(POP)/seg009.c \
	$(POP)/seqtbl.c    $(POP)/replay.c    $(POP)/options.c  $(POP)/lighting.c \
	$(POP)/screenshot.c $(POP)/menu.c     $(POP)/midi.c     $(RES)/resources.c

PSDL_SRC := $(wildcard $(PSDL)/src/psdl_*.c)

HOST_SRC := host_backend.c png_write.c

CSRC   := $(POP_SRC) $(PSDL_SRC) $(HOST_SRC)
CXXSRC := $(POP)/dbopl.cpp $(POP)/dbopl_adapter.cpp

OBJDIR := .objs
COBJ   := $(patsubst %,$(OBJDIR)/%.o,$(notdir $(basename $(CSRC))))
CXXOBJ := $(patsubst %,$(OBJDIR)/%.o,$(notdir $(basename $(CXXSRC))))
OBJ    := $(COBJ) $(CXXOBJ)

VPATH := $(POP):$(PSDL)/src:.:$(RES)

.PHONY: all run asan clean

all: $(RES)/resources.c $(OUT)

# Incremental: does nothing when the DATs have not changed.
$(RES)/resources.c:
	@$(ROOT)/tools/assets/build-resources.sh $(DATA) $(GEN)

$(OUT): $(OBJ)
	$(CXX) $(OPT) -o $@ $(OBJ) $(LDLIBS)

# -MMD -MP so a header change rebuilds everything that includes it. Without
# this a types.h edit leaves stale objects linked against the old layout, which
# fails in ways that look like logic bugs rather than build bugs.
$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

$(OBJDIR):
	mkdir -p $@

run: $(OUT)
	mkdir -p $(FRAMES)
	PICOPOP_FRAMEDIR=$(FRAMES) ./$(OUT) $(ARGS)

asan:
	$(MAKE) OPT="-O1 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer" \
	        LDLIBS="-fsanitize=address,undefined" all

clean:
	rm -rf $(OBJDIR) $(OUT) $(FRAMES)
