/*
 * The desktop backend: picosdl with the panel, the DAC and the radio replaced
 * by a directory of PNG files.
 *
 * This is the "host" half of picosdl's two backends. Everything above it - the
 * blitters, the surface pool, the arena, the event ring - is the same code the
 * firmware runs, so a wrong pixel here is a wrong pixel on the board. What it
 * does not reproduce is timing, DMA concurrency and anything to do with the
 * radio.
 *
 * Environment:
 *   PICOPOP_FRAMEDIR    where to write frames (default: no frames written)
 *   PICOPOP_FRAME_EVERY dump every Nth present (default 1)
 *   PICOPOP_MAX_FRAMES  stop writing after this many (default 200)
 *   PICOPOP_EXIT_AFTER  exit(0) after this many presents (default: never)
 *   PICOPOP_FAST        if set, delays return immediately and the clock is
 *                       virtual, so a run finishes as fast as it can
 *   PICOPOP_FRAMEHASH   print "present hash" per present instead of writing
 *                       PNGs. For comparing two builds densely: every frame is
 *                       covered, nothing hits the disk, and because the hash is
 *                       of the retained panel contents it catches a wrong pixel
 *                       anywhere. Two runs that drift apart in time still share
 *                       the same set of hashes if they render the same states.
 *   PICOPOP_WAV         write the mixer output to this path as a WAV, so the
 *                       music and the digi sounds can actually be listened to
 *   PICOPOP_WATCHDOG    seconds of wall clock after which to dump a backtrace
 *                       and abort. For finding where the game has got stuck -
 *                       on the board that shows up as the console going quiet
 *                       and the picture freezing, with no way to ask why.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <execinfo.h>

#include "psdl_internal.h"
#include "png_write.h"

/* What the last present would have shown; the unit tests read this. */
Uint8     host_last_frame[PSDL_SCREEN_W * PSDL_SCREEN_H];
int       host_present_count;
SDL_Color host_clut[256];

static const char *s_framedir;
static int         s_frame_every  = 1;
static int         s_max_frames   = 200;
static int         s_exit_after;
static int         s_fast;
static int         s_framehash;
static int         s_frames_written;
static int         s_env_read;

static void read_env(void)
{
	if (s_env_read)
		return;
	s_env_read = 1;

	s_framedir = getenv("PICOPOP_FRAMEDIR");

	const char *v;
	if ((v = getenv("PICOPOP_FRAME_EVERY")) != NULL && atoi(v) > 0)
		s_frame_every = atoi(v);
	if ((v = getenv("PICOPOP_MAX_FRAMES")) != NULL)
		s_max_frames = atoi(v);
	if ((v = getenv("PICOPOP_EXIT_AFTER")) != NULL)
		s_exit_after = atoi(v);
	s_fast = getenv("PICOPOP_FAST") != NULL;
	s_framehash = getenv("PICOPOP_FRAMEHASH") != NULL;
}

/*
 * Watchdog. A hang in the game loop is otherwise almost undebuggable here:
 * ptrace is restricted on this machine, and on hardware there is no debugger
 * attached at all. This turns "it stopped" into a stack trace.
 */
static void watchdog_fired(int sig)
{
	(void)sig;
	void  *frames[32];
	int    n = backtrace(frames, 32);
	fprintf(stderr, "\nhost: watchdog fired - the game has not made progress\n");
	backtrace_symbols_fd(frames, n, 2);
	_exit(3);
}

static void watchdog_start(void)
{
	const char *secs = getenv("PICOPOP_WATCHDOG");
	if (secs == NULL) return;
	int n = atoi(secs);
	if (n <= 0) return;
	signal(SIGALRM, watchdog_fired);
	alarm((unsigned)n);
	printf("host: watchdog armed for %d s\n", n);
}

void psdl_backend_video_init(int w, int h)
{
	watchdog_start();
	(void)w; (void)h;
	read_env();
	memset(host_last_frame, 0, sizeof(host_last_frame));
	host_present_count = 0;
	if (s_framedir != NULL)
		printf("host: writing frames to %s/ (every %d, max %d)\n",
		       s_framedir, s_frame_every, s_max_frames);
}

/*
 * Partial pushes land in host_last_frame and are never cleared, which is how the
 * real panel behaves: it keeps whatever it was last sent. Frames are written from
 * that retained buffer rather than from `pixels`, so a client that pushes only
 * what changed - or that has only one buffer and leans on the panel to hold the
 * rest - is captured the way it will actually look.
 */
static void capture_frame(void);

void psdl_backend_video_present_rect(const Uint8 *pixels, int pitch,
                                     int x, int y, int w, int h)
{
	read_env();

	for (int row = 0; row < h; ++row) {
		int dy = y + row;
		if (dy < 0 || dy >= PSDL_SCREEN_H)
			continue;
		int cx = x, cw = w;
		if (cx < 0) { cw += cx; cx = 0; }
		if (cx + cw > PSDL_SCREEN_W) cw = PSDL_SCREEN_W - cx;
		if (cw <= 0)
			continue;
		memcpy(host_last_frame + (size_t)dy * PSDL_SCREEN_W + cx,
		       pixels + (size_t)dy * pitch + cx, (size_t)cw);
	}

	capture_frame();
}

static void capture_frame(void)
{
	if (s_framehash) {
		/* FNV-1a over the retained frame plus the palette, since the same indices
		 * through a different CLUT are a different picture. */
		unsigned long long px = 14695981039346656037ull;
		const unsigned char *q = host_last_frame;
		for (size_t i = 0; i < sizeof(host_last_frame); ++i) {
			px ^= q[i]; px *= 1099511628211ull;
		}
		/* Pixels and palette separately, because they go wrong for different
		 * reasons. A wrong pixel is a rendering bug; a right pixel through a
		 * different CLUT is usually just a fade at a different point, which two
		 * builds doing different amounts of work per frame will always disagree
		 * about. */
		unsigned long long pal = px;
		for (int i = 0; i < 256; ++i) {
			pal ^= host_clut[i].r; pal *= 1099511628211ull;
			pal ^= host_clut[i].g; pal *= 1099511628211ull;
			pal ^= host_clut[i].b; pal *= 1099511628211ull;
		}
		printf("FRAME %d %016llx %016llx\n", host_present_count, px, pal);
	}

	if (s_framedir != NULL &&
	    host_present_count % s_frame_every == 0 &&
	    (s_max_frames <= 0 || s_frames_written < s_max_frames)) {
		unsigned char pal[256][3];
		for (int i = 0; i < 256; ++i) {
			pal[i][0] = host_clut[i].r;
			pal[i][1] = host_clut[i].g;
			pal[i][2] = host_clut[i].b;
		}
		char path[512];
		snprintf(path, sizeof(path), "%s/frame_%05d.png",
		         s_framedir, host_present_count);
		if (png_write_indexed(path, host_last_frame, PSDL_SCREEN_W, PSDL_SCREEN_H,
		                      PSDL_SCREEN_W, pal) != 0)
			fprintf(stderr, "host: could not write %s\n", path);
		else
			++s_frames_written;
	}

	++host_present_count;

	if (s_exit_after > 0 && host_present_count >= s_exit_after) {
		printf("host: %d presents, %d frames written; exiting\n",
		       host_present_count, s_frames_written);
		/* The firmware's budget is the interesting number, and this build uses
		 * exactly the same limits, so the peaks here are the peaks there. */
		PSDL_ReportMemory();
		{
			extern unsigned midi_clipped_samples, midi_overlong_blocks;
			extern unsigned dbopl_clipped;
			printf("host: midi mix clipped %u samples, DBOPL clamped %u, "
			       "%u overlong blocks\n",
			       midi_clipped_samples, dbopl_clipped, midi_overlong_blocks);
		}
		fflush(stdout);
		exit(0);
	}
}


/*
 * Every push is a capture point, partial ones included. It used to capture only in
 * the full-frame present, which meant a client pushing just what changed - a wipe
 * transition, or anything single-buffered - had those frames missing from the
 * record entirely. Comparing two builds then silently skipped exactly the frames
 * most likely to differ.
 */
void psdl_backend_video_present(const Uint8 *pixels, int w, int h, int pitch)
{
	read_env();
	psdl_backend_video_present_rect(pixels, pitch, 0, 0, w, h);
}

void psdl_backend_video_sync(void) { }

void psdl_backend_palette_set(int first, int ncolors, const SDL_Color *colors)
{
	for (int i = 0; i < ncolors; ++i) {
		int idx = first + i;
		if (idx >= 0 && idx < 256)
			host_clut[idx] = colors[i];
	}
}

/*
 * Scripted input.
 *
 * The game waits for keys in several places - show_splash() spins until Shift,
 * the title sequence advances on a keypress - so a headless run parks on the
 * first of them and every frame after that is identical. PICOPOP_KEYS drives
 * it past those points.
 *
 * Format: comma-separated NAME@POLL, where POLL is the input-poll count at
 * which the key goes down; it is released eight polls later. Example:
 *
 *   PICOPOP_KEYS=lshift@40,return@120
 *
 * Names are a small set covering what the attract sequence needs.
 */
static const struct { const char *name; int scancode; } s_keynames[] = {
	{ "lshift", SDL_SCANCODE_LSHIFT }, { "rshift", SDL_SCANCODE_RSHIFT },
	{ "return", SDL_SCANCODE_RETURN }, { "space",  SDL_SCANCODE_SPACE  },
	{ "escape", SDL_SCANCODE_ESCAPE }, { "up",     SDL_SCANCODE_UP     },
	{ "down",   SDL_SCANCODE_DOWN   }, { "left",   SDL_SCANCODE_LEFT   },
	{ "right",  SDL_SCANCODE_RIGHT  },
	/* The volume keys never reach the game - psdl_push_key() consumes them - but
	 * scripting them is how the ladder and the mute toggle get exercised. */
	{ "volup",  SDL_SCANCODE_VOLUMEUP   },
	{ "voldown",SDL_SCANCODE_VOLUMEDOWN },
	{ "mute",   SDL_SCANCODE_MUTE       },
};

#define HOST_MAX_KEYS 16
static struct { int scancode, at; int done_down, done_up; } s_keys[HOST_MAX_KEYS];
static int s_nkeys;
static int s_poll_count;

void psdl_backend_input_init(void)
{
	const char *spec = getenv("PICOPOP_KEYS");
	if (spec == NULL) return;

	char buf[256];
	snprintf(buf, sizeof(buf), "%s", spec);
	for (char *tok = strtok(buf, ","); tok && s_nkeys < HOST_MAX_KEYS;
	     tok = strtok(NULL, ",")) {
		char name[32]; int at = 0;
		if (sscanf(tok, "%31[^@]@%d", name, &at) != 2) continue;
		for (unsigned i = 0; i < sizeof(s_keynames)/sizeof(s_keynames[0]); ++i) {
			if (strcmp(name, s_keynames[i].name) == 0) {
				s_keys[s_nkeys].scancode = s_keynames[i].scancode;
				s_keys[s_nkeys].at       = at;
				printf("host: will press %s at poll %d\n", name, at);
				++s_nkeys;
				break;
			}
		}
	}
}

/*
 * Deliver a scripted key the way the hardware backend does.
 *
 * This used to build an SDL_Event and SDL_PushEvent() it, which skipped
 * psdl_push_key() - the entry point backend/pico/psdl_pico_input.c actually uses.
 * Two things were wrong with that. SDL_GetKeyboardState() never saw a scripted
 * key, because only psdl_push_key() maintains the keystate array; and anything
 * psdl_push_key() filters, such as the volume keys, was not filtered here. A
 * harness whose input path differs from the board's is worth less than it looks.
 */
static void push_key(int scancode, int down)
{
	psdl_push_key((SDL_Scancode)scancode, down, KMOD_NONE);
}

void psdl_backend_input_poll(void)
{
	++s_poll_count;
	for (int i = 0; i < s_nkeys; ++i) {
		if (!s_keys[i].done_down && s_poll_count >= s_keys[i].at) {
			s_keys[i].done_down = 1;
			push_key(s_keys[i].scancode, 1);
		} else if (s_keys[i].done_down && !s_keys[i].done_up &&
		           s_poll_count >= s_keys[i].at + 8) {
			s_keys[i].done_up = 1;
			push_key(s_keys[i].scancode, 0);
		}
	}
}

/*
 * Audio capture.
 *
 * The board's backend fills an I2S DMA buffer from core 1; here there is no
 * clock to pull against, so the mixer is pumped from the same virtual time the
 * rest of the run uses and the result is appended to a WAV. That makes the
 * music and the digi sounds checkable rather than merely "not crashing" -
 * which matters, because a mixer bug is inaudible in a frame dump.
 */
static FILE  *s_wav;
static int    s_wav_freq;
static Uint64 s_wav_frames;

static void wav_u32(FILE *f, unsigned v)
{
	unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8),
	                       (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
	fwrite(b, 1, 4, f);
}
static void wav_u16(FILE *f, unsigned v)
{
	unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) };
	fwrite(b, 1, 2, f);
}

static void wav_open(const char *path, int freq)
{
	s_wav = fopen(path, "wb");
	if (s_wav == NULL) {
		fprintf(stderr, "host: cannot write %s\n", path);
		return;
	}
	s_wav_freq = freq;
	fwrite("RIFF", 1, 4, s_wav); wav_u32(s_wav, 0);   /* patched on close */
	fwrite("WAVEfmt ", 1, 8, s_wav);
	wav_u32(s_wav, 16); wav_u16(s_wav, 1); wav_u16(s_wav, 2);
	wav_u32(s_wav, (unsigned)freq); wav_u32(s_wav, (unsigned)freq * 4);
	wav_u16(s_wav, 4); wav_u16(s_wav, 16);
	fwrite("data", 1, 4, s_wav); wav_u32(s_wav, 0);   /* patched on close */
	printf("host: recording audio to %s at %d Hz\n", path, freq);
}

static void wav_close(void)
{
	if (s_wav == NULL) return;
	unsigned data = (unsigned)(s_wav_frames * 4);
	fseek(s_wav, 4, SEEK_SET);  wav_u32(s_wav, 36 + data);
	fseek(s_wav, 40, SEEK_SET); wav_u32(s_wav, data);
	fclose(s_wav);
	s_wav = NULL;
	printf("host: %llu audio frames (%.1f s)\n",
	       (unsigned long long)s_wav_frames,
	       s_wav_freq ? (double)s_wav_frames / s_wav_freq : 0.0);
}

/* Generate however much audio the clock says is owed, and record it. */
static void pump_audio(void)
{
	if (s_wav == NULL || s_wav_freq <= 0) return;

	Uint64 want = psdl_backend_ticks_us() * (Uint64)s_wav_freq / 1000000u;
	while (s_wav_frames < want) {
		Sint16 block[PSDL_AUDIO_BLOCK_FRAMES * 2];
		int n = PSDL_AUDIO_BLOCK_FRAMES;
		if ((Uint64)n > want - s_wav_frames)
			n = (int)(want - s_wav_frames);
		memset(block, 0, sizeof(block));
		psdl_audio_render(block, n);
		fwrite(block, sizeof(Sint16) * 2, (size_t)n, s_wav);
		s_wav_frames += (Uint64)n;
	}
}

void psdl_backend_audio_open(int freq, int channels, int block_frames)
{
	(void)channels; (void)block_frames;
	read_env();
	const char *path = getenv("PICOPOP_WAV");
	if (path != NULL && s_wav == NULL) {
		wav_open(path, freq);
		atexit(wav_close);
	}
}
void psdl_backend_audio_close(void) { wav_close(); }
void psdl_backend_audio_pause(int pause_on) { (void)pause_on; }
void psdl_backend_audio_lock(void) { }
void psdl_backend_audio_unlock(void) { }

/*
 * The clock. Real by default; under PICOPOP_FAST it is a counter that only
 * moves when the game asks to wait, which makes a headless run finish in the
 * time it takes to draw rather than the time it takes to play.
 */
static Uint64 s_virtual_us;

Uint64 psdl_backend_ticks_us(void)
{
	read_env();
	if (s_fast)
		return s_virtual_us;

	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (Uint64)tv.tv_sec * 1000000u + (Uint64)tv.tv_usec;
}

Uint32 psdl_backend_ticks_ms(void)
{
	return (Uint32)(psdl_backend_ticks_us() / 1000u);
}

void psdl_backend_delay_ms(Uint32 ms)
{
	read_env();
	if (s_fast) {
		s_virtual_us += (Uint64)ms * 1000u;
		pump_audio();
		return;
	}
	pump_audio();
	struct timespec ts = { (long)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
	nanosleep(&ts, NULL);
}

/*
 * Master volume. Declared in SDL.h and implemented by every backend, so the
 * portable half can use it - psdl_audio_volume_key() does. Stored and returned
 * rather than applied: this backend's "DAC" is a capture buffer, and scaling what
 * the tests compare would make every one of them depend on the volume.
 */
static int s_master_volume = PSDL_DEFAULT_VOLUME;

void PSDL_SetMasterVolume(int volume)
{
	if (volume < 0)              volume = 0;
	if (volume > PSDL_VOLUME_UNITY) volume = PSDL_VOLUME_UNITY;
	s_master_volume = volume;
}

int PSDL_GetMasterVolume(void)
{
	return s_master_volume;
}

/*
 * The backend logger. No ring needed here: this backend is single-threaded and
 * nothing prints from a signal handler, so printf is already safe. On hardware it
 * is not, which is why the interface exists - see backend/pico/psdl_pico_log.c.
 */
void psdl_backend_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}
