/* main program of Free Software for Calypso Phone */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdint.h>
#include <stdio.h>

#include <debug.h>
#include <memory.h>
#include <string.h>
#include <delay.h>
#include <rffe.h>
#include <keypad.h>
#include <board.h>

#include <abb/twl3025.h>
#include <rf/trf6151.h>

#include <comm/sercomm.h>
#include <comm/timer.h>

#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/irq.h>
#include <calypso/misc.h>
#include <calypso/sim.h>

#include <layer1/sync.h>
#include <layer1/async.h>
#include <layer1/tpu_window.h>
#include <layer1/l23_api.h>

#include <fb/framebuffer.h>

const char *hr = "======================================================================\n";

/* MAIN program **************************************************************/

static void key_handler(enum key_codes code, enum key_states state);

int main(void)
{
	uint8_t atr[20];
	uint8_t atrLength = 0;

	board_init(1);

	puts("\n\nOsmocomBB Layer 1 (revision " GIT_REVISION ")\n");
	puts(hr);

	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	keypad_set_handler(&key_handler);

	/* Dump clock config after PLL set */
	calypso_clk_dump();
	puts(hr);

	fb_clear();

	fb_setfg(FB_COLOR_BLACK);
	fb_setbg(FB_COLOR_WHITE);
	fb_setfont(FB_FONT_HELVB14);

	fb_gotoxy(2,20);
	fb_putstr("Layer 1",framebuffer->width-4);

	fb_setfg(FB_COLOR_RED);
	fb_setbg(FB_COLOR_BLUE);

	fb_gotoxy(2,25);
	fb_boxto(framebuffer->width-3,38);

	fb_setfg(FB_COLOR_WHITE);
	fb_setfont(FB_FONT_HELVR08);
	fb_gotoxy(8,33);
	fb_putstr("osmocom-bb",framebuffer->width-4);

	fb_flush();

	/* initialize SIM */
	calypso_sim_init();

	puts("Power up simcard:\n");
	memset(atr,0,sizeof(atr));
	atrLength = calypso_sim_powerup(atr);

	layer1_init();

	tpu_frame_irq_en(1, 1);

	while (1) {
		l1a_compl_execute();
		osmo_timers_update();
		sim_handler();
		l1a_l23_handler();
	}

	/* NOT REACHED */

	twl3025_power_off();
}

static int afcout = 0;

static void tspact_toggle(uint8_t num)
{
	printf("TSPACT%u toggle\n", num);
	tsp_act_toggle((1 << num));
	tpu_enq_sleep();
	tpu_enable(1);
	tpu_wait_idle();
}

static void key_handler(enum key_codes code, enum key_states state)
{
	if (state != PRESSED)
		return;

	switch (code) {
	case KEY_4:
		tspact_toggle(6);	/* TRENA (RFFE) */
		break;
	case KEY_5:
		tspact_toggle(8);	/* GSM_TXEN (RFFE) */
		break;
	case KEY_6:
		tspact_toggle(1);	/* PAENA (RFFE) */
		break;
	case KEY_7:			/* decrement AFC OUT */
		afcout -= 100;
		if (afcout < -4096)
			afcout = -4096;
		twl3025_afc_set(afcout);
		printf("AFC OUT: %u\n", twl3025_afcout_get());
		break;
	case KEY_9:			/* increase AFC OUT */
		afcout += 100;
		if (afcout > 4095)
			afcout = 4095;
		twl3025_afc_set(afcout);
		printf("AFC OUT: %u\n", twl3025_afcout_get());
		break;
	default:
		break;
	}
	/* power down SIM, TODO:  this will happen with every key pressed,
       put it somewhere else ! */
	calypso_sim_powerdown();
}


