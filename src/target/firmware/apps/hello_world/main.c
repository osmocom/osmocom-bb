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
#include <delay.h>
#include <rffe.h>
#include <keypad.h>
#include <board.h>
#include <abb/twl3025.h>
#include <display.h>
#include <rf/trf6151.h>
#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/irq.h>
#include <calypso/misc.h>
#include <comm/sercomm.h>
#include <comm/timer.h>

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
	display_puts((char *) msg->data);
	msgb_free(msg);
}

static void l1a_l23_rx_cb(uint8_t dlci, struct msgb *msg)
{
	int i;
	puts("l1a_l23_rx_cb: ");
	for (i = 0; i < msg->len; i++)
		printf("%02x ", msg->data[i]);
	puts("\n");
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

	display_set_attr(DISP_ATTR_INVERT);
	display_puts("Hello World");

	sercomm_register_rx_cb(SC_DLCI_CONSOLE, console_rx_cb);
	sercomm_register_rx_cb(SC_DLCI_L1A_L23, l1a_l23_rx_cb);

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
	char test[16];

	if (state != PRESSED)
		return;

	switch (code) {
	case KEY_0:
	case KEY_1:
	case KEY_2:
	case KEY_3:
	case KEY_4:
	case KEY_5:
	case KEY_6:
	case KEY_7:
	case KEY_8:
	case KEY_9:
		sprintf(test, "%d", code - KEY_0);
		display_puts(test);
		break;
	case KEY_STAR:
		sprintf(test, "*", 0);
		display_puts(test);
		break;
	case KEY_HASH:
		sprintf(test, "#", 0);
		display_puts(test);
		break;
	default:
		break;
	}
}
