/*
 * Pre-parse every MIDI tune in the game into const tables, so the firmware does
 * not parse them at runtime and does not need an 80 KiB event pool to parse them
 * into.
 *
 * This links SDLPoP's own midi.c and calls the real parse_midi(), so the tables
 * are what that parser produces rather than what a second implementation thinks
 * it should produce. There is no transcription to get wrong, which is the same
 * reasoning as the DBOPL table generator - except that here the parser is reused
 * outright rather than copied.
 *
 * Measured over the 22 MIDI resources in the game:
 *
 *   6761 events in total, worst single tune 3635 (the ending theme)
 *   10 tracks at most, against a MIDI_MAX_TRACKS of 16
 *   largest delta_time 111360, so it has to stay 32-bit
 *   sysex/meta payloads are at most 15 bytes and 1757 bytes in total, so they
 *     are inlined into one blob rather than pointed at
 *
 * Identifying a tune at run time: keyed on a hash of the first 64 bytes of the
 * MIDI data. The alternative was to thread a resource id down through
 * play_sound_from_buffer() to play_midi_sound(), which would have touched eight
 * call sites and the host test harness, which passes a bare resource pointer and
 * never goes through sound_pointers[] at all. Hashing keeps parse_midi()'s
 * signature and behaves the same in both. Uniqueness is not assumed - this
 * program fails if any two tunes collide.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "common.h"
#include "proto.h"

bool parse_midi(const midi_raw_chunk_type *midi, parsed_midi_type *parsed_midi);
void free_parsed_midi(parsed_midi_type *parsed_midi);

const void *glue_find_resource(const char *dat, int id, int *size);

static const struct { const char *dat; int first, last; } midi_dats[] = {
	{ "MIDISND1.DAT", 10024, 10043 },
	{ "MIDISND2.DAT", 10050, 10056 },
};

#define KEY_BYTES 64

static uint32_t key_of(const byte *midi)
{
	/* FNV-1a over a fixed prefix. Every MIDI resource in the game is at least
	 * 303 bytes, so 64 is always there to read. */
	uint32_t h = 2166136261u;
	for (int i = 0; i < KEY_BYTES; ++i) {
		h ^= midi[i];
		h *= 16777619u;
	}
	return h;
}

/* ------------------------------------------------------------ collected -- */

#define MAX_TUNES  64
#define MAX_TRACKS 256
#define MAX_EVENTS 16384
#define MAX_BLOB   16384

struct out_event { uint32_t delta; uint8_t type, a, b; uint16_t off; };

static struct { uint32_t key; int id, ticks, ntracks, first_track; } tunes[MAX_TUNES];
static struct { int first_event, num_events; } tracks[MAX_TRACKS];
static struct out_event events[MAX_EVENTS];
static uint8_t blob[MAX_BLOB];
static int n_tunes, n_tracks, n_events, n_blob;

static void die(const char *what) { fprintf(stderr, "midi_tables: %s\n", what); exit(1); }

int main(void)
{
	for (unsigned d = 0; d < sizeof midi_dats / sizeof *midi_dats; ++d) {
		for (int id = midi_dats[d].first; id <= midi_dats[d].last; ++id) {
			int size = 0;
			const void *res = glue_find_resource(midi_dats[d].dat, id, &size);
			if (res == NULL || size < KEY_BYTES) continue;

			/* A DAT sound resource leads with a format byte, so the MIDI chunk
			 * starts a little way in; find it rather than assume. Resources in
			 * these id ranges that are not MIDI at all are skipped. */
			int off = -1;
			for (int i = 0; i + 4 <= size && i < 16; ++i)
				if (memcmp((const byte *)res + i, "MThd", 4) == 0) { off = i; break; }
			if (off < 0) continue;
			const byte *mid = (const byte *)res + off;
			if (size - off < KEY_BYTES) continue;

			parsed_midi_type m;
			memset(&m, 0, sizeof m);
			if (!parse_midi((const midi_raw_chunk_type *)mid, &m)) {
				fprintf(stderr, "midi_tables: resource %d failed to parse\n", id);
				continue;
			}

			if (n_tunes >= MAX_TUNES) die("too many tunes");
			uint32_t key = key_of(mid);
			for (int i = 0; i < n_tunes; ++i)
				if (tunes[i].key == key) {
					fprintf(stderr, "midi_tables: resources %d and %d share a "
					        "%d-byte prefix hash; widen KEY_BYTES\n",
					        tunes[i].id, id, KEY_BYTES);
					exit(1);
				}

			tunes[n_tunes].key         = key;
			tunes[n_tunes].id          = id;
			tunes[n_tunes].ticks       = m.ticks_per_beat;
			tunes[n_tunes].ntracks     = m.num_tracks;
			tunes[n_tunes].first_track = n_tracks;
			n_tunes++;

			for (int t = 0; t < m.num_tracks; ++t) {
				midi_track_type *tr = &m.tracks[t];
				if (n_tracks >= MAX_TRACKS) die("too many tracks");
				tracks[n_tracks].first_event = n_events;
				tracks[n_tracks].num_events  = tr->num_events;
				n_tracks++;

				for (int i = 0; i < tr->num_events; ++i) {
					midi_event_type *e = &tr->events[i];
					if (n_events >= MAX_EVENTS) die("too many events");
					struct out_event *o = &events[n_events++];
					o->delta = e->delta_time;
					o->type  = e->event_type;

					if (e->event_type == 0xF0 || e->event_type == 0xF7) {
						if (e->sysex.length > 255) die("sysex longer than 255");
						o->a = 0;
						o->b = (uint8_t)e->sysex.length;
						o->off = (uint16_t)n_blob;
						if (n_blob + (int)e->sysex.length > MAX_BLOB) die("blob full");
						memcpy(blob + n_blob, e->sysex.data, e->sysex.length);
						n_blob += e->sysex.length;
					} else if (e->event_type == 0xFF) {
						if (e->meta.length > 255) die("meta longer than 255");
						o->a = e->meta.type;
						o->b = (uint8_t)e->meta.length;
						o->off = (uint16_t)n_blob;
						if (n_blob + (int)e->meta.length > MAX_BLOB) die("blob full");
						memcpy(blob + n_blob, e->meta.data, e->meta.length);
						n_blob += e->meta.length;
					} else {
						o->a = e->channel.channel;
						o->b = e->channel.param1;
						o->off = e->channel.param2;   /* param2 rides in `off` */
					}
				}
			}
			free_parsed_midi(&m);
		}
	}

	/* ------------------------------------------------------------- emit -- */
	printf("/*\n"
	       " * Generated by tools/assets/midi_tables.c - do not edit.\n"
	       " *\n"
	       " * Every MIDI tune in the game, parsed at build time by SDLPoP's own\n"
	       " * parse_midi(). %d tunes, %d tracks, %d events, %d bytes of inlined\n"
	       " * sysex/meta payload. Replaces an 80 KiB runtime event pool.\n"
	       " */\n\n", n_tunes, n_tracks, n_events, n_blob);

	printf("#define MIDI_TABLE_KEY_BYTES %d\n\n", KEY_BYTES);

	/* struct midi_table_event is declared in SDLPoP/src/types.h: the layout is
	 * code, only the values are generated. */
	printf("static const struct midi_table_event midi_table_events[%d] = {\n", n_events);
	for (int i = 0; i < n_events; ++i)
		printf("\t{%u,0x%02X,0x%02X,0x%02X,%u},\n",
		       events[i].delta, events[i].type, events[i].a, events[i].b, events[i].off);
	printf("};\n\n");

	printf("static const struct { Uint16 first_event, num_events; } "
	       "midi_table_tracks[%d] = {\n", n_tracks);
	for (int i = 0; i < n_tracks; ++i)
		printf("\t{%d,%d},\n", tracks[i].first_event, tracks[i].num_events);
	printf("};\n\n");

	printf("static const struct { Uint32 key; Uint16 ticks_per_beat, first_track; "
	       "Uint8 num_tracks; } midi_table_tunes[%d] = {\n", n_tunes);
	for (int i = 0; i < n_tunes; ++i)
		printf("\t{0x%08Xu,%d,%d,%d},  /* resource %d */\n",
		       tunes[i].key, tunes[i].ticks, tunes[i].first_track,
		       tunes[i].ntracks, tunes[i].id);
	printf("};\n\n");

	printf("static const Uint8 midi_table_blob[%d] = {", n_blob ? n_blob : 1);
	for (int i = 0; i < n_blob; ++i)
		printf("%s0x%02X,", (i % 16) ? " " : "\n\t", blob[i]);
	if (n_blob == 0) printf("0");
	printf("\n};\n");

	fprintf(stderr, "midi_tables: %d tunes, %d tracks, %d events, %d blob bytes; "
	        "tables are %zu B of flash\n",
	        n_tunes, n_tracks, n_events, n_blob,
	        n_events * sizeof(struct out_event) + n_tracks * 4 + n_tunes * 9 + n_blob);
	return 0;
}
