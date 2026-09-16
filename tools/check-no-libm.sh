#!/bin/sh
#
# Fail the build if the firmware links a transcendental maths function.
#
#   check-no-libm.sh <nm> <image.elf>
#
# The rule and the reasoning are in TODO.md. In short: an FPU gives you add,
# multiply, divide and sqrt as single instructions, but it does not give you sin,
# pow, log, exp or atan2 - those stay library routines of hundreds of cycles and
# a few KB of flash each, and every one this tree had was either a table that
# could be built at build time or an integer decision wearing floating-point
# clothes.
#
# Checked by symbol presence rather than by call sites: if any of these were
# needed, the linker would have pulled the implementation in. The SDK's
# pico_float / pico_double route the standard names through __wrap_ aliases, so
# both spellings are looked for.
#
# sqrt is deliberately absent from the list - it is a single instruction on the
# M33 and costs nothing worth banning.

set -eu

NM=${1:?usage: check-no-libm.sh <nm> <image.elf>}
ELF=${2:?usage: check-no-libm.sh <nm> <image.elf>}

BANNED='(__wrap_)?(pow|powf|sin|sinf|cos|cosf|tan|tanf|asin|asinf|acos|acosf|atan|atanf|atan2|atan2f|sinh|sinhf|cosh|coshf|tanh|tanhf|exp|expf|exp2|exp2f|expm1|log|logf|log2|log2f|log10|log10f|log1p|fmod|fmodf|cbrt|cbrtf|hypot|hypotf)'

found=$("$NM" "$ELF" | awk '{print $NF}' | grep -xE "$BANNED" | sort -u || true)

if [ -n "$found" ]; then
    echo "check-no-libm: $ELF links a maths library:" >&2
    echo "$found" | sed 's/^/  /' >&2
    echo "" >&2
    echo "See the libm section of TODO.md. Find the caller with:" >&2
    echo "  arm-none-eabi-objdump -d $ELF | awk '/^[0-9a-f]+ <.*>:/{fn=\$2} /bl.*<SYMBOL>/{print fn}' | sort -u" >&2
    exit 1
fi
