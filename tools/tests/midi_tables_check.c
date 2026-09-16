/*
 * Render every MIDI tune in the game and print a digest of the PCM.
 *
 * Built twice - once with PICOPOP_MIDI_TABLES and once without - the two outputs
 * must be identical. That is the whole check on the build-time MIDI tables: if a
 * delta_time, a channel parameter, a meta payload or a track boundary came out
 * wrong anywhere in 6761 events, the audio differs and the digest moves.
 *
 * Every tune is rendered to its end rather than for a fixed span, because the
 * events least like the others live in the longest tune: the ending theme is 3635
 * events and holds the only delta_time above 65535.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "game_glue.h"

#define RATE     22050
#define BLOCK    256
#define MAX_SECS 180

void  midi_callback(void *userdata, Uint8 *stream, int len);
void  stop_midi(void);
extern short midi_playing;

static const struct { const char *dat; int first, last; } midi_dats[] = {
	{ "MIDISND1.DAT", 10024, 10043 },
	{ "MIDISND2.DAT", 10050, 10056 },
};

int main(void)
{
	SDL_Init(SDL_INIT_AUDIO);
	static SDL_AudioSpec spec;
	spec.freq = RATE; spec.format = AUDIO_S16SYS; spec.channels = 2; spec.samples = BLOCK;
	glue_set_audiospec(&spec);

	Sint16 *buf = calloc(BLOCK * 2, sizeof(Sint16));
	int played = 0;

	printf("%-8s %8s %10s  %s\n", "resource", "blocks", "digest", "note");
	for (unsigned d = 0; d < sizeof midi_dats / sizeof *midi_dats; ++d) {
		for (int id = midi_dats[d].first; id <= midi_dats[d].last; ++id) {
			int size = 0;
			const void *res = glue_find_resource(midi_dats[d].dat, id, &size);
			if (res == NULL) continue;

			glue_play_music(res);
			if (!midi_playing) {
				printf("%-8d %8s %10s  not played\n", id, "-", "-");
				continue;
			}
			played++;

			uint32_t h = 2166136261u;          /* FNV-1a over every sample */
			long blocks = 0, cap = (long)RATE * MAX_SECS / BLOCK;
			while (midi_playing && blocks < cap) {
				memset(buf, 0, BLOCK * 2 * sizeof(Sint16));
				midi_callback(NULL, (Uint8 *)buf, BLOCK * 2 * (int)sizeof(Sint16));
				const uint8_t *p = (const uint8_t *)buf;
				for (size_t i = 0; i < BLOCK * 2 * sizeof(Sint16); ++i) {
					h ^= p[i];
					h *= 16777619u;
				}
				blocks++;
			}
			printf("%-8d %8ld   %08x  %s\n", id, blocks, h,
			       blocks >= cap ? "hit the cap" : "ended");
			stop_midi();
		}
	}
	printf("\n%d tunes rendered\n", played);
	free(buf);
	return 0;
}
