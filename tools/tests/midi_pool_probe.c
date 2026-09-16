/*
 * What the MIDI event pool actually has to hold.
 *
 * Parses every MIDI resource in the game through the real parse_midi() and
 * reports the maxima that decide how the pool can be sized or packed: events per
 * tune, tracks per tune, and - for each field of midi_event_type - the largest
 * value seen, which is what says whether a narrower type would be lossless.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "common.h"
#include "proto.h"

/* midi.c exports these with external linkage but declares them in no header. */
bool parse_midi(const midi_raw_chunk_type *midi, parsed_midi_type *parsed_midi);
void free_parsed_midi(parsed_midi_type *parsed_midi);

static const struct { const char *dat; int first, last; } midi_dats[] = {
	{ "MIDISND1.DAT", 10024, 10043 },
	{ "MIDISND2.DAT", 10050, 10056 },
};

const void *glue_find_resource(const char *dat, int id, int *size);

int main(void)
{
	int max_events = 0, max_events_id = 0;
	int max_tracks = 0, max_tracks_id = 0;
	int max_track_events = 0;
	dword max_delta = 0; int max_delta_id = 0;
	dword max_len = 0;   int max_len_id = 0;
	int max_res = 0;
	int tunes = 0, total_events = 0;
	long long delta_over_65535 = 0, delta_over_255 = 0, len_over_255 = 0;
	long long n_channel = 0, n_sysex = 0, n_meta = 0, inline_bytes = 0;

	setvbuf(stdout,NULL,_IONBF,0);
	printf("%-10s %8s %7s %7s %10s %8s\n",
	       "resource", "bytes", "tracks", "events", "max delta", "max len");

	for (unsigned d = 0; d < sizeof midi_dats / sizeof *midi_dats; ++d) {
		for (int id = midi_dats[d].first; id <= midi_dats[d].last; ++id) {
			int size = 0;
			const void *res = glue_find_resource(midi_dats[d].dat, id, &size);
			if (res == NULL || size <= 0) continue;
			if (size > max_res) max_res = size;

			/* A DAT sound resource leads with a format byte, so the MIDI chunk
			 * starts a little way in; find it rather than assume the offset. The
			 * id ranges also include non-MIDI sounds, which are skipped - and
			 * skipping them matters, because parse_midi trusts the chunk lengths
			 * once the header looks right and walks off the end otherwise. */
			int off = -1;
			for (int i = 0; i + 4 <= size && i < 16; ++i)
				if (memcmp((const byte *)res + i, "MThd", 4) == 0) { off = i; break; }
			if (off < 0) {
				printf("%-10d %8d   not MIDI, skipped\n", id, size);
				continue;
			}
			const byte *mid = (const byte *)res + off;
			(void)0;

			parsed_midi_type m;
			memset(&m, 0, sizeof m);
			if (!parse_midi((const midi_raw_chunk_type *)mid, &m)) {
				printf("%-10d %8d   parse failed\n", id, size);
				continue;
			}
			tunes++;

			int ev = 0; dword md = 0, ml = 0;
			for (int t = 0; t < m.num_tracks; ++t) {
				midi_track_type *tr = &m.tracks[t];
				ev += tr->num_events;
				if (tr->num_events > max_track_events) max_track_events = tr->num_events;
				for (int i = 0; i < tr->num_events; ++i) {
					midi_event_type *e = &tr->events[i];
					if (e->delta_time > md) md = e->delta_time;
					if (e->delta_time > 65535) delta_over_65535++;
					if (e->delta_time > 255)   delta_over_255++;
					dword l = 0;
					if (e->event_type == 0xF0 || e->event_type == 0xF7) l = e->sysex.length;
					else if (e->event_type == 0xFF)                     l = e->meta.length;
					if (l > ml) ml = l;
					if (l > 255) len_over_255++;
					if (e->event_type == 0xF0 || e->event_type == 0xF7) {
						n_sysex++; inline_bytes += l;
					} else if (e->event_type == 0xFF) {
						n_meta++; inline_bytes += l;
					} else {
						n_channel++;
					}
				}
			}
			total_events += ev;
			printf("%-10d %8d %7d %7d %10u %8u\n",
			       id, size, m.num_tracks, ev, md, ml);

			if (ev > max_events)      { max_events = ev; max_events_id = id; }
			if (m.num_tracks > max_tracks) { max_tracks = m.num_tracks; max_tracks_id = id; }
			if (md > max_delta)       { max_delta = md; max_delta_id = id; }
			if (ml > max_len)         { max_len = ml; max_len_id = id; }

			free_parsed_midi(&m);
		}
	}

	printf("\n%d tunes, %d events parsed in total\n", tunes, total_events);
	printf("worst tune        : %d events (resource %d)\n", max_events, max_events_id);
	printf("worst track       : %d events\n", max_track_events);
	printf("most tracks       : %d (resource %d)\n", max_tracks, max_tracks_id);
	printf("largest resource  : %d bytes\n", max_res);
	printf("largest delta_time: %u (resource %d)\n", max_delta, max_delta_id);
	printf("largest sysex/meta: %u bytes (resource %d)\n", max_len, max_len_id);
	printf("\nfields that would NOT fit a narrower type:\n");
	printf("  delta_time > 65535 : %lld events\n", delta_over_65535);
	printf("  delta_time > 255   : %lld events\n", delta_over_255);
	printf("  length     > 255   : %lld events\n", len_over_255);
	printf("\nevent mix: %lld channel, %lld sysex, %lld meta\n", n_channel, n_sysex, n_meta);
	printf("inline sysex/meta data across every tune: %lld bytes\n", inline_bytes);
	printf("\nsizeof(midi_event_type) = %zu, pool = %d events = %zu B\n",
	       sizeof(midi_event_type), 4096, 4096 * sizeof(midi_event_type));
	return 0;
}
