/* Framebuffer implementation - Toppoly TD014 LCD driver for Motorola C139/40 */
/* Based on td014.c by Steve Markgraf and Harald Welte */

/* (C) 2010 by Christian Vogel <vogelchr@vogel.cx>
 * (C) 2012 by Steve Markgraf <steve@steve-m.de>
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

#include <fb/framebuffer.h>
#include <fb/fb_rgb332.h>

#include <stdint.h>
#include <stdio.h>
#include <delay.h>
#include <uwire.h>
#include <calypso/clock.h>

#define TD014_WIDTH		96
#define TD014_HEIGHT		64
#define TD014_UWIRE_BITLEN	9
#define TD014_DEV_ID		0

static uint8_t fb_td014_mem[TD014_WIDTH * TD014_HEIGHT];

enum td014_cmdflag { CMD, DATA, END };

struct td014_cmdlist {
	enum td014_cmdflag is_cmd:8;	/* 1: is a command, 0: is data, 2: end marker! */
	uint8_t data;  			/* 8 bit to send to LC display */
} __attribute__((packed));

static const struct td014_cmdlist
td014_initdata[] = {
	{ CMD,  0x3f },
	{ DATA, 0x01 },
	{ CMD,  0x20 },
	{ DATA, 0x03 },
	{ CMD,  0x31 },
	{ DATA, 0x03 },
	{ END,  0x00 }, /* MARKER: end of list */
};

static void
fb_td014_send_cmdlist(const struct td014_cmdlist *p) {
	int i=0;
	while(p->is_cmd != END){
		uint16_t sendcmd = p->data;
		if(p->is_cmd == DATA)
			sendcmd |= 0x0100; /* 9th bit is cmd/data flag */
		uwire_xfer(TD014_DEV_ID, TD014_UWIRE_BITLEN, &sendcmd, NULL);
		p++;
		i++;
	}
}

static void
fb_td014_init(void) {
	printf("%s: initializing LCD.\n",__FUNCTION__);
	calypso_reset_set(RESET_EXT, 0);
	delay_ms(5);
	uwire_init();
	delay_ms(5);

	fb_td014_send_cmdlist(td014_initdata);
}

static void
fb_td014_flush(void) {
	int x,y;
	uint8_t *p;
	struct td014_cmdlist prepare_disp_write_cmds[] = {
		{ CMD,  0x10 },
		{ DATA, fb_rgb332->damage_x1 },
		{ CMD,  0x11 },
		{ DATA, fb_rgb332->damage_y1 },
		{ CMD,  0x12 },
		{ DATA, fb_rgb332->damage_x2-1 },
		{ CMD,  0x13 },
		{ DATA, fb_rgb332->damage_y2-1 },
		{ CMD,  0x14 },
		{ DATA, fb_rgb332->damage_x1 },
		{ CMD,  0x15 },
		{ DATA, fb_rgb332->damage_y1 },
		{ END,  0x00 }
	};

	/* If everything's clean, just return */
	if(fb_rgb332->damage_x1 == fb_rgb332->damage_x2 ||
		fb_rgb332->damage_y1 == fb_rgb332->damage_y2) {
			printf("%s: no damage\n",__FUNCTION__);
			return;
	}

	fb_td014_send_cmdlist(prepare_disp_write_cmds);

	for(y=fb_rgb332->damage_y1;y<fb_rgb332->damage_y2;y++) {
		p = & fb_rgb332->mem[y * framebuffer->width]; // start of line
		p += fb_rgb332->damage_x1; // start of damage area

		for(x=fb_rgb332->damage_x1; x<fb_rgb332->damage_x2; x++) {
			uint16_t pixel = rgb332_to_565(*p++);
			uint16_t data = 0x0100 | (pixel >> 8);

			uwire_xfer(TD014_DEV_ID, TD014_UWIRE_BITLEN,
					&data, NULL);

			data = 0x0100 | (pixel & 0xff);
			uwire_xfer(TD014_DEV_ID, TD014_UWIRE_BITLEN,
				&data, NULL);
		}
	}

	fb_rgb332->damage_x1 = fb_rgb332->damage_x2 = 0;
	fb_rgb332->damage_y1 = fb_rgb332->damage_y2 = 0;
}

static struct framebuffer fb_td014_framebuffer = {
	.name = "td014",
	.init = fb_td014_init,
	.clear = fb_rgb332_clear,
	.boxto = fb_rgb332_boxto,
	.lineto = fb_rgb332_lineto,
	.putstr = fb_rgb332_putstr,
	.flush = fb_td014_flush,
	.width = TD014_WIDTH,
	.height = TD014_HEIGHT
};

static struct fb_rgb332 fb_td014_rgb332 = {
	.mem = fb_td014_mem
};

struct framebuffer *framebuffer = &fb_td014_framebuffer;
struct fb_rgb332 *fb_rgb332 = &fb_td014_rgb332;
