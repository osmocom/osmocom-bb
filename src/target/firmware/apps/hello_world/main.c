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
#include <string.h>

#include <debug.h>
#include <memory.h>
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
#include <calypso/misc.h>
#include <comm/sercomm.h>
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

void key_handler(enum key_codes code, enum key_states state);

static void console_rx_cb(uint8_t dlci, struct msgb *msg)
{
	if (dlci != SC_DLCI_CONSOLE) {
		printf("Message for unknown DLCI %u\n", dlci);
		return;
	}

	printf("Message on console DLCI: '%s'\n", msg->data);
	st7558_puts((char *) msg->data);
	msgb_free(msg);
}

int main(void)
{
	board_init();
	puts("\n\nHello World from " __FILE__ " program code\n");
	puts(hr);
	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	/* Dump clock config before PLL set */
	calypso_clk_dump();
	puts(hr);

	keypad_set_handler(&key_handler);

	/* Dump clock config aftee PLL set */
	calypso_clk_dump();
	puts(hr);

	/* Dump all memory */
	//dump_mem();
#if 0
	/* Dump Bootloader */
	memdump_range((void *)0x00000000, 0x2000);
	puts(hr);
#endif

	st7558_set_attr(DISP_ATTR_INVERT);
	st7558_puts("Hello World");

	sercomm_register_rx_cb(SC_DLCI_CONSOLE, console_rx_cb);

	/* beyond this point we only react to interrupts */
	puts("entering interrupt loop\n");
	while (1) {
		update_timers();
	}

	twl3025_power_off();

	while (1) {}
}

void key_handler(enum key_codes code, enum key_states state)
{
	if (state != PRESSED)
		return;

	switch (code) {
	default:
		break;
	}
}
