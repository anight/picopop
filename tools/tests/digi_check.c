/*
 * Check the on-the-fly digi resampler against the expansion it replaced.
 *
 * seg009.c used to run convert_digi_sound() at load time: every digi sound was
 * expanded from 8-bit mono at its own rate into 16-bit stereo at 44.1 kHz and
 * cached, about 16x, roughly 1.5 MB for all of them. The mixer then memcpy'd
 * out of that. It now steps a 16.16 fixed-point cursor through the source in
 * flash instead and interpolates per output sample - no expansion, no heap.
 *
 * That swaps a float algorithm for an integer one, which is exactly the kind
 * of change that is easy to get subtly wrong and hard to hear. This runs both
 * over every digi resource in the game and reports the difference.
 *
 *   make -C picosdl/test digi
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL2/SDL.h"
#include "resources.h"
#include "digi_resample.h"   /* the real mixer code, not a copy */

#define OUT_RATE 44100

/* The two wave header layouts, as types.h describes them. */
static int wave_info(const unsigned char *d, unsigned len,
                     unsigned *rate, unsigned *count, const unsigned char **samples)
{
	if (len < 10) return 0;
	/* d[0] is sound_buffer_type.type; the union starts at d[1]. */
	const unsigned char *b = d + 1;

	unsigned old_size = b[6];          /* digi_type.sample_size     */
	unsigned new_size = b[2];          /* digi_new_type.sample_size */

	if (old_size == 8 && new_size != 8) {
		*rate    = (unsigned)(b[0] | (b[1] << 8));
		*count   = (unsigned)(b[2] | (b[3] << 8));
		*samples = b + 7;
		return 1;
	}
	if (new_size == 8 && old_size != 8) {
		*rate    = (unsigned)(b[0] | (b[1] << 8));
		*count   = (unsigned)(b[3] | (b[4] << 8));
		*samples = b + 9;
		return 1;
	}
	return 0;   /* ambiguous or not a wave */
}

/*
 * The same interpolation in double, as a high-precision reference.
 *
 * This is what both of the other two are approximating. It is the right thing
 * to judge the fixed-point path against: the old float path is itself
 * inaccurate, because `i * ratio` in single precision loses resolution as i
 * grows (by the end of a long sound the fractional position is only good to
 * about 5e-4, which is worth tens of LSB on a steep waveform).
 */
static void expand_double(const unsigned char *src, unsigned count, unsigned rate,
                          short *out, int frames)
{
	double ratio = (double)rate / (double)OUT_RATE;
	for (int i = 0; i < frames; ++i) {
		double f = i * ratio;
		int i0 = (int)f;
		int s0 = (src[i0] | (src[i0] << 8)) - 32768;
		if ((unsigned)i0 + 1 >= count) { out[i] = (short)s0; continue; }
		double alpha = f - i0;
		int s1 = (src[i0 + 1] | (src[i0 + 1] << 8)) - 32768;
		out[i] = (short)(s0 + alpha * (s1 - s0));
	}
}

/* What convert_digi_sound() produced, verbatim. */
static void expand_float(const unsigned char *src, unsigned count, unsigned rate,
                         short *out, int frames)
{
	float ratio = (float)rate / (float)OUT_RATE;
	for (int i = 0; i < frames; ++i) {
		float f = i * ratio;
		int i0 = (int)f;
		int s0 = (src[i0] | (src[i0] << 8)) - 32768;
		short v;
		if ((unsigned)i0 >= count - 1) {
			v = (short)s0;
		} else {
			float alpha = f - i0;
			int s1 = (src[i0 + 1] | (src[i0 + 1] << 8)) - 32768;
			v = (short)((1.0f - alpha) * s0 + alpha * s1);
		}
		out[i] = v;
	}
}

/*
 * What digi_callback() does now - driving the same digi_resample.h the mixer
 * uses, so this cannot drift away from the shipped code.
 */
static void resample_fixed(const unsigned char *src, unsigned count, unsigned rate,
                           short *out, int frames)
{
	uint64_t pos  = 0;
	uint64_t step = digi_resample_step(rate, OUT_RATE);
	for (int i = 0; i < frames; ++i) {
		out[i] = (short)digi_resample_sample(src, (int)count, pos);
		pos += step;
	}
}

int main(void)
{
	int sounds = 0, worst = 0, worst_id = -1;
	int old_worst = 0;
	long long total_abs = 0, total_n = 0, old_abs = 0;
	long long saved = 0;

	for (unsigned f = 0; f < datfiles_count; ++f) {
		for (unsigned r = 0; r < datfiles[f].resources_count; ++r) {
			const struct resource_s *res = datfiles[f].resources[r];
			if (res->resource_id < 10000 || res->resource_id >= 11000) continue;
			if (res->surface != NULL) continue;

			unsigned rate, count;
			const unsigned char *src;
			if (!wave_info(res->data, res->data_len, &rate, &count, &src)) continue;
			if (count < 2 || rate == 0) continue;
			if (src + count > res->data + res->data_len) continue;

			int frames = (int)((unsigned long long)count * OUT_RATE / rate);
			if (frames <= 0) continue;

			short *a = malloc((size_t)frames * sizeof(short));
			short *b = malloc((size_t)frames * sizeof(short));
			short *c = malloc((size_t)frames * sizeof(short));
			expand_float(src, count, rate, a, frames);
			resample_fixed(src, count, rate, b, frames);
			expand_double(src, count, rate, c, frames);

			int local_worst = 0;
			for (int i = 0; i < frames; ++i) {
				int d = c[i] - b[i];            /* fixed point vs the truth */
				if (d < 0) d = -d;
				if (d > local_worst) local_worst = d;
				total_abs += d;

				int e = c[i] - a[i];            /* old float vs the truth */
				if (e < 0) e = -e;
				if (e > old_worst) old_worst = e;
				old_abs += e;
			}
			total_n += frames;
			if (local_worst > worst) { worst = local_worst; worst_id = res->resource_id; }

			/* What the old path would have held in RAM for this sound. */
			saved += (long long)frames * 2 * (int)sizeof(short);

			sounds++;
			free(a); free(b); free(c);
		}
	}

	printf("%d digi sounds compared, %lld output samples\n", sounds, total_n);
	printf("  fixed point vs exact : worst %d LSB, mean %.4f\n",
	       worst, total_n ? (double)total_abs / (double)total_n : 0.0);
	printf("  old float   vs exact : worst %d LSB, mean %.4f  (resource %d)\n",
	       old_worst, total_n ? (double)old_abs / (double)total_n : 0.0, worst_id);
	printf("RAM the old expansion would have needed: %lld bytes (%.0f KB)\n",
	       saved, (double)saved / 1024.0);

	/*
	 * The fixed-point path truncates where the exact one rounds, so one LSB is
	 * expected and anything more means the cursor has drifted. It should also
	 * be no worse than the float path it replaced - and in fact is better,
	 * because a 32.32 cursor does not lose resolution as the sound goes on.
	 */
	if (worst > 1) {
		printf("FAIL: fixed point is %d LSB off the exact result; "
		       "expected at most 1 from truncation\n", worst);
		return 1;
	}
	if (worst > old_worst) {
		printf("FAIL: fixed point (%d LSB) is worse than the float path "
		       "it replaced (%d LSB)\n", worst, old_worst);
		return 1;
	}
	printf("OK: within one LSB of exact, and better than the float path\n");
	return 0;
}
