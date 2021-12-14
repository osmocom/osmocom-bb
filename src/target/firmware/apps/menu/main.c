/* Menu for Calypso Phone to load applicatios from flash */

/* (C) 2013 by Andreas Eversberg <jolly@eversberg.eu>
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
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <debug.h>
#include <memory.h>
#include <delay.h>
#include <byteorder.h>
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
#include <calypso/timer.h>
#include <comm/sercomm.h>
#include <comm/timer.h>
#include <uart.h>
#include <fb/framebuffer.h>
#include <battery/battery.h>
#include <asm/system.h>

#define RAM 0x00820000
#define MAGIC 0x0083ff00

static enum key_codes key_code = KEY_INV;
static volatile enum key_states key_state;

static int cursor = 0, scroll_apps = 0;

static struct apps {
	char name[16];
	void *start;
	int len;
} apps[32];

static void locate_apps(void)
{
	int i, j, k;
	char *p;

	memset(apps, 0, sizeof(apps));

	for (j = 0, i = 0x010000; i < 0x200000; i += 0x10000) {
		p = (char *)i;
		/* check for highram header: "highram:" */
		if (!!memcmp(p, "highram:", 8))
			continue;
		p += 8;
		/* check for app name after header: "highram:<name>\n" */
		printf("found highram image at flash mem address 0x%p\n",
			(char *)i);
		for (k = 0; k < (int)sizeof(apps[j].name) - 1; k++) {
			if (p[k] == '\n')
				break;
		}
		if (k == sizeof(apps[j].name) - 3) {
			printf("skipping: corrupt highram header, no '\\n' "
				"after image name or name more larger than %d "
				"digits\n", (int)sizeof(apps[j].name) - 3);
				continue;
		}
		if (j < 9)
			apps[j].name[0] = '1' + j;
		else if (j == 9)
			apps[j].name[0] = '0';
		else
			apps[j].name[0] = ' ';
		apps[j].name[1] = ' ';
		memcpy(apps[j].name + 2, p, k);
		apps[j].len = 0x20000;
		p += k + 1;
		/* p points to highram image after header */
		apps[j].start = p;
		j++;
	}
}

static void wait_key_release(void)
{
	/* wait for key release */
	while (key_state == PRESSED) {
		delay_ms(10);
		keypad_poll();
	}
}

static void load_app(void)
{
	static int i;
	static void (*f) (void) = (void (*)(void))RAM;

	wait_key_release();

	local_irq_disable();

	for (i = 0; i < apps[cursor].len; i++)
		((unsigned char *)RAM)[i] = ((unsigned char *)apps[cursor].start)[i];
	f();
}

/* UI */

static void refresh_display(void)
{
#if 0
	char text[16];
	int bat = battery_info.battery_percent;
#endif
	int i;

	fb_clear();

	/* header */
	fb_setbg(FB_COLOR_WHITE);
	if (1) {
		fb_setfg(FB_COLOR_BLUE);
		fb_setfont(FB_FONT_HELVR08);
		fb_gotoxy(0, 7);
		fb_putstr("Osmocom Menu", -1);
		fb_setfg(FB_COLOR_RGB(0xc0, 0xc0, 0x00));
		fb_setfont(FB_FONT_SYMBOLS);
#if 0
		fb_gotoxy(framebuffer->width - 15, 8);
		if (bat >= 100 && (battery_info.flags & BATTERY_CHG_ENABLED)
		 && !(battery_info.flags & BATTERY_CHARGING))
			fb_putstr("@HHBC", framebuffer->width);
		else {
			sprintf(text, "@%c%c%cC", (bat >= 30) ? 'B':'A',
				(bat >= 60) ? 'B':'A', (bat >= 90) ? 'B':'A');
			fb_putstr(text, framebuffer->width);
		}
#endif
		fb_gotoxy(0, 8);
		fb_putstr("GGEGG", framebuffer->width);
		fb_setfg(FB_COLOR_GREEN);
		fb_gotoxy(0, 10);
		fb_boxto(framebuffer->width - 1, 10);
	}
	fb_setfg(FB_COLOR_BLACK);
	fb_setfont(FB_FONT_C64);


	for (i = 0; i < 5; i++) {
		if (!apps[scroll_apps + i].name)
			break;
		if (scroll_apps + i == cursor) {
			fb_setfg(FB_COLOR_WHITE);
			fb_setbg(FB_COLOR_BLUE);
		}
		fb_gotoxy(0, 20 + i * 10);
		fb_putstr(apps[scroll_apps + i].name,
			framebuffer->width);
		if (scroll_apps + i == cursor) {
			fb_setfg(FB_COLOR_BLACK);
			fb_setbg(FB_COLOR_WHITE);
		}
	}
	if (i == 0) {
		fb_gotoxy(0, 50);
		fb_putstr("No apps!", -1);
	}

	fb_flush();
}

static void handle_key_code()
{
	if (key_code == KEY_INV)
		return;

	switch (key_code) {
	case KEY_1:
	case KEY_2:
	case KEY_3:
	case KEY_4:
	case KEY_5:
	case KEY_6:
	case KEY_7:
	case KEY_8:
	case KEY_9:
		if (apps[key_code - KEY_1].len) {
			cursor = key_code - KEY_1;
			load_app();
		}
		break;
	case KEY_0:
		if (apps[9].len) {
			cursor = 9;
			load_app();
		}
		break;
	case KEY_UP:
		if (cursor == 0)
			return;
		cursor--;
		if (cursor < scroll_apps)
			scroll_apps = cursor;
		refresh_display();
		break;
	case KEY_DOWN:
		if (!apps[cursor + 1].name[0])
			return;
		cursor++;
		if (cursor >= scroll_apps + 5)
			scroll_apps = cursor - 4;
		refresh_display();
		break;
	case KEY_OK:
		if (apps[cursor].len)
			load_app();
		break;
	case KEY_POWER:
		wait_key_release();
		twl3025_power_off();
		break;
	default:
		break;
	}

	key_code = KEY_INV;
}

/* Main Program */
const char *hr = "======================================================================\n";

static void key_handler(enum key_codes code, enum key_states state)
{
	key_state = state;

	if (state != PRESSED)
		return;

	key_code = code;
}

extern void putchar_asm(uint32_t c);

static const uint8_t phone_ack[] = { 0x1b, 0xf6, 0x02, 0x00, 0x41, 0x03, 0x42 };

int main(void)
{
	int i;

	/* Simulate a compal loader saying "ACK" */
	for (i = 0; i < (int)sizeof(phone_ack); i++) {
		putchar_asm(phone_ack[i]);
	}

	board_init(0);

	puts("\n\nOsmocomBB Menu (revision " GIT_REVISION ")\n");
	puts(hr);

	fb_clear();

	fb_setfg(FB_COLOR_BLACK);
	fb_setbg(FB_COLOR_WHITE);
	fb_setfont(FB_FONT_HELVB14);

	fb_gotoxy(2,20);
	fb_putstr("menu",framebuffer->width-4);

	fb_setfg(FB_COLOR_RED);
	fb_setbg(FB_COLOR_BLUE);

	fb_gotoxy(2,25);
	fb_boxto(framebuffer->width-3,38);

	fb_setfg(FB_COLOR_WHITE);
	fb_setfont(FB_FONT_HELVR08);
	fb_gotoxy(8,33);
	fb_putstr("osmocom-bb",framebuffer->width-4);

	fb_flush();

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

//	display_unset_attr(DISP_ATTR_INVERT);

	locate_apps();

	while (1) {
		for (i = 0; i < 50; i++) {
			keypad_poll();
			delay_ms(10);
			osmo_timers_update();
			handle_key_code();
		}
		refresh_display();
	}

	/* NOT REACHED */

	twl3025_power_off();
}

