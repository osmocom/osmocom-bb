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
#include <rffe.h>
#include <keypad.h>
#include <board.h>

#include <abb/twl3025.h>
#include <display.h>
#include <rf/trf6151.h>

#include <comm/sercomm.h>
#include <comm/timer.h>

#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/irq.h>
#include <calypso/misc.h>

#include <layer1/sync.h>
#include <layer1/tpu_window.h>

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

const char *hr = "======================================================================\n";

/* MAIN program **************************************************************/

static void key_handler(enum key_codes code, enum key_states state);

int main(void)
{
	puts("\n\nHello World from " __FILE__ " program code\n");

	puts(hr);
	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	keypad_set_handler(&key_handler);

	/* Dump clock config aftee PLL set */
	calypso_clk_dump();
	puts(hr);

	display_set_attr(DISP_ATTR_INVERT);
	display_puts("layer1.bin");

	layer1_init();

	tpu_frame_irq_en(1, 1);

	while (1) {
		l1a_compl_execute();
		update_timers();
	}

	/* NOT REACHED */

	twl3025_power_off();
}

static int8_t vga_gain = 40;
static int high_gain = 0;
static int afcout = 0;

static void update_vga_gain(void)
{
	printf("VGA Gain: %u %s\n", vga_gain, high_gain ? "HIGH" : "LOW");
	trf6151_set_gain(vga_gain, high_gain);
	tpu_enq_sleep();
	tpu_enable(1);
	tpu_wait_idle();
}

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
	case KEY_1:	/* VGA gain decrement */
		vga_gain -= 2;
		if (vga_gain < 14)
			vga_gain = 14;
		update_vga_gain();
		break;
	case KEY_2: 	/* High/Low Rx gain */
		high_gain ^= 1;
		update_vga_gain();
		break;
	case KEY_3:	/* VGA gain increment */
		vga_gain += 2;
		if (vga_gain > 40)
			vga_gain = 40;
		update_vga_gain();
		break;
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
}


