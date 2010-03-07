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
#include <stdio.h>
#include <stdint.h>
#include <rffe.h>
#include <keypad.h>
#include <board.h>
#include <abb/twl3025.h>
#include <display/st7558.h>
#include <rf/trf6151.h>
#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/irq.h>
#include <comm/timer.h>

/* FIXME: We need proper calibrated delay loops at some point! */
void delay_us(unsigned int us)
{
	volatile unsigned int i;

	for (i= 0; i < us*4; i++) { i; }
}

void delay_ms(unsigned int ms)
{
	volatile unsigned int i;

	for (i= 0; i < ms*1300; i++) { i; }
}

/* Main Program */
const char *hr = "======================================================================\n";

void main(void)
{
	int i;
	uint16_t twl_reg;

	/* Initialize basic board support */
	board_init();

	puts("\n\nCompal DSP data dumper\n");
	puts(hr);

	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	/* Dump DSP content */
	dsp_dump();

	while (1) {
		update_timers();
	}
}

