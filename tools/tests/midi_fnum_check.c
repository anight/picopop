/*
 * Check every entry of the generated MIDI F-number table against the powf() and
 * log2f() expressions it replaced.
 *
 * Rendering a tune and comparing WAVs - which the midi-fnum target also does -
 * only covers the notes that tune happens to play. It does not notice a wrong
 * entry anywhere else in the table, and `semi` spans 383 values because
 * midi_semitones_higher is an sbyte. So compare all of them directly.
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "midi_fnum_table.inc"

int main(void)
{
	int n = 0, bad = 0;

	for (int semi = MIDI_FNUM_SEMI_MIN; semi <= MIDI_FNUM_SEMI_MAX; semi++) {
		/* Verbatim from midi.c's #else branch. */
		float octaves_from_A4 = semi / 12.0f;
		float frequency = powf(2.0f,  octaves_from_A4) * 440.0f;
		float f_number_float = frequency * (float)(1 << 20) / 49716.0f;
		int block = (int)(log2f(f_number_float) - 9) & 7;
		int f = ((int)f_number_float >> block) & 1023;

		unsigned packed = midi_fnum_table[semi - MIDI_FNUM_SEMI_MIN];
		int t_block = packed >> 10;
		int t_f     = packed & 1023;
		n++;

		if (t_block != block || t_f != f) {
			if (bad < 10)
				printf("  FAIL semi %4d: formula block=%d f=%4d, "
				       "table block=%d f=%4d\n", semi, block, f, t_block, t_f);
			bad++;
		}
	}

	printf("%d table entries checked, %d wrong\n", n, bad);
	if (bad)
		return 1;
	printf("OK: every MIDI F-number entry matches powf/log2f\n");
	return 0;
}
