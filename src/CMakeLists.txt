#
# picopop - Prince of Persia on a Raspberry Pi Pico 2 W.
#
# This is the top-level build. Configure it directly:
#
#     cmake -S src -B build
#     cmake --build build
#
# and it produces build/picopop.uf2, which picodev.sh will program.
#
# The layout it assumes:
#
#   src/        this file, main.c, and the hardware demo
#   picosdl/    the SDL2 subset, a submodule and an independent library
#   SDLPoP/     the game, a submodule
#   tools/      the desktop build, the tests and the asset pipeline
#
# picosdl knows nothing about this project: it is added as a subdirectory and
# linked, the same way any other game would use it. Everything below that is
# specific to Prince of Persia lives here rather than in the library.
#
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#
# Build for speed, not for size. -O3.
#
# This is the SDK's own default, but it is set explicitly here because it was
# once changed and the change had to be reverted, and the next person to look at
# a 1.8 MB image will have the same idea.
#
# -Os is tempting: it costs nothing visible and saves 127 KB of .text (34% of the
# code; 130 KB of the flash image, 45.3% -> 42.2% of 4 MB). The frame rate does
# not care - it is panel-bound, see PLAN.md section 10.
#
# The mixer does. DBOPL at -Os produces bit-identical output, verified by
# rendering the title theme both ways and comparing the WAVs byte for byte, but
# it takes 1.47x as long (0.154 s -> 0.227 s of host CPU per 30 s of audio,
# `make -C tools/tests music MUSIC_OPT=-Os`). On the board that is enough to miss
# the mixer's block deadline, and when that happens the I2S DMA chain re-triggers
# on the other buffer regardless of whether the CPU refilled it - see the long
# comment in picosdl/picodev.sh. The result is audible noise in the first tune.
#
# Flash is not the scarce resource here: 45% of 4 MB used, and most of that is
# converted sprites in .rodata, which -Os does not touch (-1,248 bytes of
# 1,510,464). Trading real-time headroom for 3% of a flash chip is the wrong way
# round.
#
# If size ever does matter, the shape of the fix is per-file: keep -O3 on
# dbopl.cpp, dbopl_adapter.cpp, midi.c and picosdl's audio path, and let the rest
# go to -Os. That was not done because nothing needs the space.
#
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Release = -O3; see the note above before changing this)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release MinSizeRel RelWithDebInfo)
endif()
message(STATUS "picopop: build type ${CMAKE_BUILD_TYPE}")

set(PICOPOP_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(PICOSDL_DIR  ${PICOPOP_ROOT}/picosdl)
set(SDLPOP_DIR   ${PICOPOP_ROOT}/SDLPoP/src)

foreach(dir ${PICOSDL_DIR}/src ${SDLPOP_DIR})
    if(NOT EXISTS ${dir})
        message(FATAL_ERROR
            "${dir} is missing - run: git submodule update --init --recursive")
    endif()
endforeach()

# ------------------------------------------------------------- the resources
#
# The game's graphics and sounds are built from the original DAT files, which
# are not distributed with this project and are not in its git history. You
# supply them in SDLPoP/data and the build converts them; nothing derived from
# them is ever committed.
#
# The conversion is not a detail of the build - it is what makes the sprites
# blittable straight out of flash, 8bpp indexed into one global palette with
# each set's row already baked into the pixels. See tools/README.md.
#
set(PICOPOP_DATA_DIR ${PICOPOP_ROOT}/SDLPoP/data CACHE PATH
    "Where the Prince of Persia DAT files are")
set(PICOPOP_GENERATED ${PICOPOP_ROOT}/generated CACHE PATH
    "Where the converted resources are written")

set(PICOPOP_RESOURCES ${PICOPOP_GENERATED}/resources)

# Run at configure time so a missing DAT is reported before anything compiles,
# and so resources.h exists for the include path below. The script is
# incremental: with the DATs unchanged it does nothing.
execute_process(
    COMMAND ${PICOPOP_ROOT}/tools/assets/build-resources.sh
            ${PICOPOP_DATA_DIR} ${PICOPOP_GENERATED}
    RESULT_VARIABLE PICOPOP_ASSETS_RESULT
    OUTPUT_VARIABLE PICOPOP_ASSETS_OUTPUT
    ERROR_VARIABLE  PICOPOP_ASSETS_OUTPUT
)
if (NOT PICOPOP_ASSETS_RESULT EQUAL 0)
    message(FATAL_ERROR "${PICOPOP_ASSETS_OUTPUT}")
endif()
message(STATUS "Resources: ${PICOPOP_RESOURCES}")

# Rebuild them if a DAT changes, without having to re-run cmake.
file(GLOB PICOPOP_DAT_FILES ${PICOPOP_DATA_DIR}/*.DAT)
add_custom_command(
    OUTPUT  ${PICOPOP_RESOURCES}/resources.c
    COMMAND ${PICOPOP_ROOT}/tools/assets/build-resources.sh
            ${PICOPOP_DATA_DIR} ${PICOPOP_GENERATED}
    DEPENDS ${PICOPOP_DAT_FILES}
            ${PICOPOP_ROOT}/tools/assets/convert.py
            ${PICOPOP_ROOT}/tools/assets/build-resources.sh
    COMMENT "Converting the game resources"
    VERBATIM
)

# Pico 2 W (RP2350). Override with -DPICO_BOARD=pico_w for the original board.
# The system clock is 138 MHz, set by picosdl - below the RP2350's 150 MHz
# default, so the core is not overclocked. The panel is: SCK is sysclk/2.
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# picosdl carries the SDK bootstrap so there is one copy of it, not two.
include(${PICOSDL_DIR}/cmake/pico_sdk_bootstrap.cmake)

project(picopop C CXX ASM)
pico_sdk_init()

# picosdl sees CMAKE_SOURCE_DIR != its own dir and skips its bootstrap.
#
# Its demo is off by default as a subdirectory, and asked for here because it is
# the bring-up tool for this board: a palette animation, a sprite, a tone, a key
# and a stick, with none of the game running. It used to be defined in this file
# and play the game's own music; it lives in picosdl now, links nothing but the
# library, and needs no DAT files - so it is the library's example rather than
# ours. It still lands at build/picosdl-demo.elf.
set(PICOSDL_BUILD_DEMO ON CACHE BOOL "Build picosdl-demo for board bring-up")
add_subdirectory(${PICOSDL_DIR} picosdl)

# ------------------------------------------------------------ OPL emulator
#
#   dbopl  DOSBox's, the default. Measured on the title theme at 938
#          instructions per output sample against Nuked's 5583 - about 6x
#          cheaper - and 5176 bytes of chip state against Nuked's 20776. It is
#          the less exact chip model: level-matched to 0.06 dB with a 0.9995
#          loudness-envelope correlation, but ~1.3 dB brighter above 6 kHz.
#   nuked  Nuked OPL3 1.7.4, what SDLPoP ships, with picopop's silent-slot and
#          unfolded-table optimisations. More faithful, and much more expensive.
#
# See PLAN.md section 8.
set(PICOPOP_OPL dbopl CACHE STRING "OPL emulator: dbopl or nuked")
set_property(CACHE PICOPOP_OPL PROPERTY STRINGS dbopl nuked)
message(STATUS "OPL emulator: ${PICOPOP_OPL}")

function(picopop_add_opl target)
    if (PICOPOP_OPL STREQUAL "dbopl")
        target_sources(${target} PRIVATE
            ${SDLPOP_DIR}/dbopl.cpp ${SDLPOP_DIR}/dbopl_adapter.cpp)
        # opl3.h switches on this: midi.c keeps including it, but gets a token
        # opl3_chip instead of Nuked's 19 KB struct, and opl3.c compiles to
        # nothing.
        target_compile_definitions(${target} PRIVATE PICOPOP_USE_DBOPL=1)
        # Vendored C++ with no runtime needs: no exceptions, RTTI or libstdc++.
        set_source_files_properties(
            ${SDLPOP_DIR}/dbopl.cpp ${SDLPOP_DIR}/dbopl_adapter.cpp
            PROPERTIES COMPILE_OPTIONS
            "-fno-exceptions;-fno-rtti;-fno-threadsafe-statics")
    elseif (PICOPOP_OPL STREQUAL "nuked")
        target_sources(${target} PRIVATE ${SDLPOP_DIR}/opl3.c)
        target_compile_definitions(${target} PRIVATE PICOPOP_USE_DBOPL=0)
    else()
        message(FATAL_ERROR "PICOPOP_OPL must be dbopl or nuked, not '${PICOPOP_OPL}'")
    endif()
endfunction()

# ----------------------------------------------------------------- the game
#
# stb_vorbis.c is deliberately absent (PLAN.md section 9: 54 of the 64
# allocation sites, and the Ogg path is compiled out anyway), and so is
# SDLPoP's main.c - the entry point here is main.c beside this file, which
# brings the hardware up and then calls pop_main().
#
add_executable(picopop
    main.c
    ${SDLPOP_DIR}/data.c       ${SDLPOP_DIR}/seg000.c   ${SDLPOP_DIR}/seg001.c
    ${SDLPOP_DIR}/seg002.c     ${SDLPOP_DIR}/seg003.c   ${SDLPOP_DIR}/seg004.c
    ${SDLPOP_DIR}/seg005.c     ${SDLPOP_DIR}/seg006.c   ${SDLPOP_DIR}/seg007.c
    ${SDLPOP_DIR}/seg008.c     ${SDLPOP_DIR}/seg009.c   ${SDLPOP_DIR}/seqtbl.c
    ${SDLPOP_DIR}/replay.c     ${SDLPOP_DIR}/options.c  ${SDLPOP_DIR}/lighting.c
    ${SDLPOP_DIR}/screenshot.c ${SDLPOP_DIR}/menu.c     ${SDLPOP_DIR}/midi.c
    ${PICOPOP_RESOURCES}/resources.c
)
picopop_add_opl(picopop)

target_link_libraries(picopop PRIVATE picosdl pico_float)
target_include_directories(picopop PRIVATE
    ${PICOSDL_DIR}/src ${SDLPOP_DIR} ${PICOPOP_RESOURCES})
target_compile_definitions(picopop PRIVATE PICOPOP=1)

# Print the mixer's load once a second on the console. It is the number PLAN.md
# section 3.7 still owes for the RP2350, and what decides whether the output
# rate can go back to 44100 - see the note on digi_samplerate in seg009.c.
target_compile_definitions(picopop PRIVATE PICOPOP_REPORT_AUDIO_LOAD=1)

# Our own code only; SDLPoP's is not warning-clean under -Wextra.
set_source_files_properties(main.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

# Nothing in the game reaches the heap any more - every allocation site is a
# static pool now, verified by disassembly. What is left are three SDK callers
# that cannot be reached from here:
#
#   alarm_pool_create_on_timer_with_unused_hardware_alarm   one-shot at init
#   alarm_pool_destroy
#   cyw43_btbus_init                                        one-shot at init
#
# plus newlib's printf, which takes the heap for its stream buffers.
#
# 64 KB was sized for parse_midi(), which no longer needs it. 32 KB is a
# conservative figure for what remains: the SDK's use has not been measured on
# hardware, and starving cyw43_btbus_init would take Bluetooth out quietly.
# Worth shrinking once the peak is known.
target_compile_definitions(picopop PRIVATE PICO_HEAP_SIZE=32768)

pico_enable_stdio_uart(picopop 1)
pico_enable_stdio_usb(picopop 1)
pico_add_extra_outputs(picopop)

