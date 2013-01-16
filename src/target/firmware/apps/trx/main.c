/* main program for TRX function */

/*
 * Copyright (C) 2013  Sylvain Munaut <tnt@246tNt.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdio.h>

#include <debug.h>
#include <keypad.h>
#include <board.h>

#include <abb/twl3025.h>

#include <comm/timer.h>

#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/misc.h>

#include <layer1/sync.h>
#include <layer1/async.h>
#include <layer1/l23_api.h>
#include <layer1/trx.h>

#include <fb/framebuffer.h>


static void
key_handler(enum key_codes code, enum key_states state)
{
	if (state != PRESSED)
		return;

	switch (code) {
	default:
		break;
	}
}


/* MAIN program **************************************************************/

int main(void)
{
	const char *hr = "======================================================================\n";

	/* Init board */
	board_init(1);

	/* Register keypad handler */
	keypad_set_handler(&key_handler);

	/* Debug console output */
		/* Ident */
	puts("\n\nOSMOCOM TRX (revision " GIT_REVISION ")\n");
	puts(hr);

		/* Dump device identification */
	dump_dev_id();
	puts(hr);

		/* Dump clock config after PLL set */
	calypso_clk_dump();
	puts(hr);

	/* Fill screen */
	fb_clear();

	fb_setfg(FB_COLOR_BLACK);
	fb_setbg(FB_COLOR_WHITE);
	fb_setfont(FB_FONT_HELVB14);

	fb_gotoxy(2,20);
	fb_putstr("TRX",framebuffer->width-4);

	fb_setfg(FB_COLOR_RED);
	fb_setbg(FB_COLOR_BLUE);

	fb_gotoxy(2,25);
	fb_boxto(framebuffer->width-3,38);

	fb_setfg(FB_COLOR_WHITE);
	fb_setfont(FB_FONT_HELVR08);
	fb_gotoxy(8,33);
	fb_putstr("osmocom-bb",framebuffer->width-4);

	fb_flush();

	/* Init TRX */
	trx_init();

	/* Init layer 1 */
	layer1_init();

	tpu_frame_irq_en(1, 1);

	/* Main loop */
	while (1) {
		l1a_compl_execute();
		osmo_timers_update();
		l1a_l23_handler();
	}

	/* NOT REACHED */
	twl3025_power_off();
}
