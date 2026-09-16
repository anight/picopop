/*
 * Prince of Persia on a Pico 2 W: the firmware entry point.
 *
 * There is deliberately almost nothing here. The game brings up its own
 * subsystems the way it always has - set_gr_mode() calls SDL_Init and
 * SDL_CreateWindow, init_digi() calls SDL_OpenAudio - and picosdl turns those
 * into the panel, the I2S DAC and the Bluetooth keyboard. So this does the two
 * things that have to happen before any of that, and then hands over.
 *
 * The order of the first two lines matters and is not obvious:
 * set_sys_clock_khz() re-parents clk_peri off clk_sys, and stdio derives the
 * UART divisor from clk_peri when it starts, so starting stdio first leaves the
 * console at the wrong baud rate.
 */
#include <stdio.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include "SDL2/SDL.h"
#include "psdl_pico.h"

/* SDLPoP's entry point, and the argv it reads options from. */
void pop_main(void);
extern int    g_argc;
extern char **g_argv;

static char *s_argv[] = { (char *)"prince", NULL };

int main(void)
{
	set_sys_clock_khz(PSDL_PICO_SYS_CLOCK_KHZ, true);
	stdio_init_all();

	/* Long enough for a USB CDC console to enumerate and attach, so the first
	 * lines of a boot are not lost when that is the only cable connected. */
	sleep_ms(1500);

	printf("\n=== picopop: Prince of Persia ===\n");
	printf("sys clock %u Hz\n", (unsigned)clock_get_hz(clk_sys));

	/*
	 * Use the letterbox strips the 320x240 panel leaves above and below a 320x200
	 * canvas. picosdl fills the header with its own figures - frame rate and the
	 * load on both cores - and the footer with whatever we give it.
	 *
	 * Indices 15 and 0 are bright white on black in Prince of Persia's palette, and
	 * only we know that: picosdl takes indices precisely because the meaning of a
	 * palette belongs to the game.
	 */
	/* White on black, as RGB: the bands are picosdl's, and must not follow the
	 * game's palette - the damage flash rewrites entry 0. */
	static const SDL_Color band_fg = { 255, 255, 255, SDL_ALPHA_OPAQUE };
	static const SDL_Color band_bg = {   0,   0,   0, SDL_ALPHA_OPAQUE };
	PSDL_StatusBands(SDL_TRUE, band_fg, band_bg);
	PSDL_SetFooterText("github.com/anight/picopop");

	g_argc = 1;
	g_argv = s_argv;

	pop_main();

	/* pop_main() does not return in normal play; quit() exits the process. */
	printf("pop_main returned\n");
	for (;;)
		tight_loop_contents();
}
