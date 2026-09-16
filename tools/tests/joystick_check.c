/*
 * Check the integer stick-sector tests against the atan2() form they replaced.
 *
 * SDLPoP/src/joystick_sectors.h is included directly, so this drives exactly the
 * code the firmware runs rather than a transcription of it.
 *
 * Not swept over all 2^32 int16 pairs: hardly any of those are reachable. The
 * Adafruit pad's ADC is 10-bit and the board stick's is 12-bit, so what exists is
 * 1024 and 4096 distinct levels per axis. Both sets are swept in full, both axes.
 *
 * The boundaries are swept separately, because that is the only place the two
 * formulations can disagree and it is where neither device's grid is likely to
 * land: for every x in the whole int16 range, the y nearest each threshold ray is
 * tested along with its neighbours.
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "joystick_sectors.h"

#ifndef M_PI   /* -std=c11 hides it */
#define M_PI 3.14159265358979323846
#endif
#define DEG (M_PI/180.0)

struct pred { int right, left, up, down, facing_up, facing_down; };

/* What get_joystick_state() used to compute. */
static struct pred by_atan2(int x, int y)
{
	double angle = atan2(y, x);
	struct pred p = {
		.right       = fabs(angle) < ( 60*DEG),
		.left        = fabs(angle) > (120*DEG),
		.up          = angle < (-30*DEG) && angle > (-150*DEG),
		.down        = angle > ( 35*DEG) && angle < ( 145*DEG),
		.facing_up   = angle < 0,
		.facing_down = angle > 0,
	};
	return p;
}

static struct pred by_integers(int x, int y)
{
	struct pred p = {
		.right       = joy_sector_right(x, y),
		.left        = joy_sector_left(x, y),
		.up          = joy_sector_up(x, y),
		.down        = joy_sector_down(x, y),
		.facing_up   = joy_facing_up(x, y),
		.facing_down = joy_facing_down(x, y),
	};
	return p;
}

static long long checked, bad, shown;

static void check(int x, int y)
{
	if (x == 0 && y == 0) return;          /* the dead zone returns before this */
	struct pred a = by_atan2(x, y), b = by_integers(x, y);
	checked++;
	if (a.right == b.right && a.left == b.left && a.up == b.up && a.down == b.down &&
	    a.facing_up == b.facing_up && a.facing_down == b.facing_down) return;
	bad++;
	if (shown++ < 10)
		printf("  FAIL x=%6d y=%6d  angle=%14.10f deg   "
		       "atan2 R%d L%d U%d D%d u%d d%d / int R%d L%d U%d D%d u%d d%d\n",
		       x, y, atan2(y, x)/DEG,
		       a.right, a.left, a.up, a.down, a.facing_up, a.facing_down,
		       b.right, b.left, b.up, b.down, b.facing_up, b.facing_down);
}

static int clamp16(int v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }

int main(void)
{
	/* The pad: raw 0..1023 through pad_axis_to_sdl(), (raw - 512) * 64, clamped. */
	static int pad[1024];
	for (int r = 0; r < 1024; r++)
		pad[r] = clamp16((r - 512) * 64);
	long long c0 = checked, b0 = bad;
	for (int i = 0; i < 1024; i++)
		for (int j = 0; j < 1024; j++)
			check(pad[i], pad[j]);
	printf("pad    10-bit, 1024 levels : %9lld pairs, %lld wrong\n",
	       checked - c0, bad - b0);

	/* The board stick: raw 0..4095 through joyPrvNormalize() then * 32767. */
	static int stick[4096];
	const int centre = 2048, adc_max = 4095;
	const float dead = 0.12f;                 /* JOY_DEADZONE */
	for (int r = 0; r < 4096; r++) {
		float v = (r >= centre) ? (float)(r - centre) / (float)(adc_max - centre)
		                        : -(float)(centre - r) / (float)centre;
		if (v > dead)       v = (v - dead) / (1.f - dead);
		else if (v < -dead) v = (v + dead) / (1.f - dead);
		else                v = 0.f;
		stick[r] = (int)(v * 32767.0f);
	}
	c0 = checked; b0 = bad;
	for (int i = 0; i < 4096; i++)
		for (int j = 0; j < 4096; j++)
			check(stick[i], stick[j]);
	printf("stick  12-bit, 4096 levels : %9lld pairs, %lld wrong\n",
	       checked - c0, bad - b0);

	/* Every integer straddling each threshold ray. */
	static const double rays[] = { 60, -60, 120, -120, -30, -150, 35, 145 };
	c0 = checked; b0 = bad;
	for (unsigned k = 0; k < sizeof rays / sizeof *rays; k++) {
		double t = tan(rays[k] * DEG);
		for (int x = -32768; x <= 32767; x++) {
			double yb = (double)x * t;
			if (yb < -40000.0 || yb > 40000.0) continue;
			for (int d = -2; d <= 2; d++) {
				long y = lround(yb) + d;
				if (y >= -32768 && y <= 32767)
					check(x, (int)y);
			}
		}
	}
	printf("threshold rays, +/-2       : %9lld pairs, %lld wrong\n",
	       checked - c0, bad - b0);

	printf("\n%lld pairs checked, %lld wrong\n", checked, bad);
	if (bad)
		return 1;
	printf("OK: the integer sector tests match atan2 everywhere the hardware can reach\n");
	return 0;
}
