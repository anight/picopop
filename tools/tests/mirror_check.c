/*
 * Check that mirroring a sprite produces exactly the reversed sprite.
 *
 * A right-facing character is a left-facing one mirrored, so every character
 * sprite in the game goes through sprite_mirror_into(). The failure this
 * guards against is not subtle arithmetic - it is the mirror being composited
 * *over* something instead of replacing it, which showed up on hardware as a
 * right-facing kid with a left-facing ghost of himself on top.
 *
 * The reference is a naive reversal of the source pixels, computed here. Every
 * pixel must match, transparent ones included: a mirror that leaves stale
 * pixels in the transparent gaps is exactly the bug.
 *
 *   make -C picosdl/test mirror
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL2/SDL.h"
#include "resources.h"
#include "sprite_mirror.h"   /* the real thing hflip() calls */

int main(void)
{
	SDL_Init(0);

	int checked = 0, bad = 0, skipped = 0;

	for (unsigned s = 0; s < sprite_sets_count; ++s) {
		const struct sprite_set_s *set = &sprite_sets[s];
		for (unsigned i = 0; i < set->n_images; ++i) {
			const SDL_Surface *src = set->images[i];
			if (src == NULL) continue;

			SDL_Surface *dst = SDL_CreateRGBSurface(0, src->w, src->h, 8, 0, 0, 0, 0);
			if (dst == NULL) { skipped++; continue; }

			if (sprite_mirror_into((SDL_Surface *)src, dst) != 0) {
				printf("  FAIL   %s image %u: sprite_mirror_into returned an error\n",
				       set->datfile, i);
				bad++;
				SDL_FreeSurface(dst);
				continue;
			}

			int wrong = 0;
			for (int y = 0; y < src->h && !wrong; ++y) {
				const Uint8 *sr = (const Uint8 *)src->pixels + (size_t)y * src->pitch;
				const Uint8 *dr = (const Uint8 *)dst->pixels + (size_t)y * dst->pitch;
				for (int x = 0; x < src->w; ++x) {
					if (dr[x] != sr[src->w - 1 - x]) {
						printf("  PIXEL  %s image %u at (%d,%d): got %u, want %u\n",
						       set->datfile, i, x, y, dr[x], sr[src->w - 1 - x]);
						wrong = 1;
						break;
					}
				}
			}
			if (wrong) bad++;

			/* Mirroring twice must give the original back. */
			if (!wrong) {
				SDL_Surface *back = SDL_CreateRGBSurface(0, src->w, src->h, 8, 0, 0, 0, 0);
				if (back != NULL) {
					sprite_mirror_into(dst, back);
					for (int y = 0; y < src->h; ++y) {
						const Uint8 *sr = (const Uint8 *)src->pixels + (size_t)y * src->pitch;
						const Uint8 *br = (const Uint8 *)back->pixels + (size_t)y * back->pitch;
						if (memcmp(sr, br, (size_t)src->w) != 0) {
							printf("  ROUND  %s image %u row %d: mirroring twice "
							       "did not restore the original\n",
							       set->datfile, i, y);
							bad++;
							break;
						}
					}
					SDL_FreeSurface(back);
				}
			}

			checked++;
			SDL_FreeSurface(dst);
		}
	}

	printf("\n%d sprites mirrored, %d wrong, %d skipped\n", checked, bad, skipped);
	if (bad != 0) {
		printf("FAIL\n");
		return 1;
	}
	printf("OK: every mirror is an exact reversal, and mirroring twice is identity\n");
	return 0;
}
