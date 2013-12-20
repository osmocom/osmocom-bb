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
#include <rf/trf6151.h>
#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/irq.h>
#include <calypso/misc.h>
#include <comm/sercomm.h>
#include <comm/timer.h>
#include <fb/framebuffer.h>
#include <battery/battery.h>

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

void
write_battery_info(void *p){
	char buf[128];

	fb_setfg(FB_COLOR_WHITE);
	fb_setfont(FB_FONT_C64);

	snprintf(buf,sizeof(buf),"B: %04d mV",battery_info.bat_volt_mV);
	fb_gotoxy(8,41);
	fb_putstr(buf,framebuffer->width-8);

	snprintf(buf,sizeof(buf),"C: %04d mV",battery_info.charger_volt_mV);
	fb_gotoxy(8,49);
	fb_putstr(buf,framebuffer->width-8);

	snprintf(buf,sizeof(buf),"F: %08x",battery_info.flags);
	fb_gotoxy(8,57);
	fb_putstr(buf,framebuffer->width-8);

	fb_flush();
	osmo_timer_schedule((struct osmo_timer_list*)p,100);

}

/* timer that fires the charging loop regularly */
static struct osmo_timer_list write_battery_info_timer = {
	.cb = &write_battery_info,
	.data = &write_battery_info_timer
};

int main(void)
{
	board_init(1);

	puts("\n\nOsmocomBB Hello World (revision " GIT_REVISION ")\n");
	puts(hr);

	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	/* Dump clock config before PLL set */
	calypso_clk_dump();
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
	fb_putstr("Hello World!",framebuffer->width-4);

	fb_setfg(FB_COLOR_RED);
	fb_setbg(FB_COLOR_BLUE);

	fb_gotoxy(2,25);
	fb_boxto(framebuffer->width-3,38);

	fb_setfg(FB_COLOR_WHITE);
	fb_setfont(FB_FONT_HELVR08);
	fb_gotoxy(8,33);
	fb_putstr("osmocom-bb",framebuffer->width-4);

	fb_flush();



	/* Dump all memory */
	//dump_mem();
#if 0
	/* Dump Bootloader */
	memdump_range((void *)0x00000000, 0x2000);
	puts(hr);
#endif

	sercomm_register_rx_cb(SC_DLCI_CONSOLE, console_rx_cb);
	sercomm_register_rx_cb(SC_DLCI_L1A_L23, l1a_l23_rx_cb);

	osmo_timer_schedule(&write_battery_info_timer,100);

	/* beyond this point we only react to interrupts */
	puts("entering interrupt loop\n");
	while (1) {
		osmo_timers_update();
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
		// used to be display_puts...
		break;
	case KEY_STAR:
		// used to be display puts...
		break;
	case KEY_HASH:
		// used to be display puts...
		break;
	default:
		break;
	}
}
