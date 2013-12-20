/* main program of Free Software for Calypso Phone */

/* (C) 2010 Harald Welte <laforge@gnumonks.org>
 * (C) 2010 Sylvain Munaut <tnt@246tNt.com>
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

#include <memory.h>
#include <delay.h>
#include <stdio.h>
#include <stdint.h>
#include <rffe.h>
#include <keypad.h>
#include <board.h>
#include <abb/twl3025.h>
#include <rf/trf6151.h>
#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/irq.h>
#include <calypso/misc.h>
#include <comm/timer.h>
#include <fb/framebuffer.h>

/* Main Program */
const char *hr = "======================================================================\n";

int main(void)
{
	board_init(1);

	puts("\n\nOsmocomBB Compal DSP Dumper (revision " GIT_REVISION ")\n");
	puts(hr);

	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	fb_clear();

	fb_setfg(FB_COLOR_BLACK);
	fb_setbg(FB_COLOR_WHITE);
	fb_setfont(FB_FONT_HELVB14);

	fb_gotoxy(2,20);
	fb_putstr("DSP Dump",framebuffer->width-4);

	fb_setfg(FB_COLOR_RED);
	fb_setbg(FB_COLOR_BLUE);

	fb_gotoxy(2,25);
	fb_boxto(framebuffer->width-3,38);

	fb_setfg(FB_COLOR_WHITE);
	fb_setfont(FB_FONT_HELVR08);
	fb_gotoxy(8,33);
	fb_putstr("osmocom-bb",framebuffer->width-4);

	fb_flush();

	/* Dump DSP content */
	dsp_dump();

	while (1) {
		osmo_timers_update();
	}
}

