/* Framebuffer implementation - SSD1963 (S1D15G14 clone) LCD driver for J100i */
/* Based on ssd1963.c by Steve Markgraf and Harald Welte */

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

#define SSD1963_WIDTH		96
#define SSD1963_HEIGHT		64
#define SSD1963_UWIRE_BITLEN	9
#define SSD1963_DEV_ID		0

static uint8_t fb_ssd1963_mem[SSD1963_WIDTH * SSD1963_HEIGHT];

enum ssd1963_cmdflag { CMD, DATA, END };

struct ssd1963_cmdlist {
	enum ssd1963_cmdflag is_cmd:8;	/* 1: is a command, 0: is data, 2: end marker! */
	uint8_t data;  			/* 8 bit to send to LC display */
} __attribute__((packed));

static const struct ssd1963_cmdlist
ssd1963_initdata[] = {
	{ CMD,  0xb6 }, /* CMD   Display Control, set panel parameters */
	{ DATA, 0x4b },
	{ DATA, 0xf1 },
	{ DATA, 0x40 },
	{ DATA, 0x40 },
	{ DATA, 0x00 },
	{ DATA, 0x8c },
	{ DATA, 0x00 },
	{ CMD,  0x3a }, /* CMD   Set pixel format */
	{ DATA, 0x02 }, /* DATA: 8 bit per pixel */
	{ CMD,  0x2d }, /* Colour set, RGB332 -> RGB 565 mapping */
	{ DATA, 0x00 }, /* DATA red 000 */ 
	{ DATA, 0x04 }, /* DATA red 001 */ 
	{ DATA, 0x09 }, /* DATA red 010 */ 
	{ DATA, 0x0d }, /* DATA red 011 */ 
	{ DATA, 0x12 }, /* DATA red 100 */ 
	{ DATA, 0x16 }, /* DATA red 101 */ 
	{ DATA, 0x1b }, /* DATA red 110 */ 
	{ DATA, 0x1f }, /* DATA red 111 */ 
	{ DATA, 0x00 }, /* Those bytes are probably a second palette */
	{ DATA, 0x00 }, /* for an unused powersaving mode with reduced colors */
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 }, /* DATA green 000 */
	{ DATA, 0x09 }, /* DATA green 001 */
	{ DATA, 0x12 }, /* DATA green 010 */
	{ DATA, 0x1b }, /* DATA green 011 */
	{ DATA, 0x24 }, /* DATA green 100 */
	{ DATA, 0x2d }, /* DATA green 101 */
	{ DATA, 0x36 }, /* DATA green 110 */
	{ DATA, 0x3f }, /* DATA green 111 */
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 }, /* DATA blue 00 */
	{ DATA, 0x0a }, /* DATA blue 01 */
	{ DATA, 0x15 }, /* DATA blue 10 */
	{ DATA, 0x1f }, /* DATA blue 11 */
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ DATA, 0x00 },
	{ CMD,  0x11 }, /* CMD   Exit sleep mode*/
	{ CMD,  0xba }, /* CMD   Set contrast/Electronic Volume Control */
	{ DATA, 0x5b }, /* DATA: */
	{ DATA, 0x84 }, /* DATA: */
	{ CMD,  0x36 }, /* CMD Memory access control */
	{ DATA, 0x00 }, /* DATA: */
	{ CMD,  0x13 }, /* CMD   Enter normal mode */
	{ CMD,  0x29 }, /* CMD   Set display on */
	{ END,  0x00 }, /* MARKER: end of list */
};

static void
fb_ssd1963_send_cmdlist(const struct ssd1963_cmdlist *p) {
	int i=0;
	while(p->is_cmd != END){
		uint16_t sendcmd = p->data;
		if(p->is_cmd == DATA)
			sendcmd |= 0x0100; /* 9th bit is cmd/data flag */
		uwire_xfer(SSD1963_DEV_ID, SSD1963_UWIRE_BITLEN, &sendcmd, NULL);
		p++;
		i++;
	}
}

static void
fb_ssd1963_init(void){
	printf("%s: initializing LCD.\n",__FUNCTION__);
	calypso_reset_set(RESET_EXT, 0);
	delay_ms(5);
	uwire_init();
	delay_ms(5);
	fb_ssd1963_send_cmdlist(ssd1963_initdata);
}

static void
fb_ssd1963_flush(void){
	int x,y;
	uint8_t *p;
	struct ssd1963_cmdlist prepare_disp_write_cmds[] = {
		{ CMD,  0x2a },			 /*  set column address */
		{ DATA, fb_rgb332->damage_x1 },
		{ DATA, fb_rgb332->damage_x2-1 },
		{ CMD,  0x2b },			 /*  set page address (Y) */
		{ DATA, fb_rgb332->damage_y1 },
		{ DATA, fb_rgb332->damage_y2-1 },
		{ CMD,  0x2c },			 /* enter write display ram mode */
		{ END,  0x00 }
	};
	struct ssd1963_cmdlist nop[] = {
		{ CMD, 0x00 }, // NOP command
		{ END, 0x00 }
	};

	/* If everything's clean, just return */
	if(fb_rgb332->damage_x1 == fb_rgb332->damage_x2 ||
		fb_rgb332->damage_y1 == fb_rgb332->damage_y2) {
			printf("%s: no damage\n",__FUNCTION__);
			return;
	}

	fb_ssd1963_send_cmdlist(prepare_disp_write_cmds);

	for(y=fb_rgb332->damage_y1;y<fb_rgb332->damage_y2;y++) {
		p = & fb_rgb332->mem[y * framebuffer->width]; // start of line
		p += fb_rgb332->damage_x1; // start of damage area

		for(x=fb_rgb332->damage_x1;x<fb_rgb332->damage_x2;x++) {
			uint16_t data = 0x0100 | *p++;
			uwire_xfer(SSD1963_DEV_ID, SSD1963_UWIRE_BITLEN,
					&data, NULL);
		}
	}
	fb_ssd1963_send_cmdlist(nop);

	fb_rgb332->damage_x1 = fb_rgb332->damage_x2 = 0;
	fb_rgb332->damage_y1 = fb_rgb332->damage_y2 = 0;
}

static struct framebuffer fb_ssd1963_framebuffer = {
	.name = "ssd1963",
	.init = fb_ssd1963_init,
	.clear = fb_rgb332_clear,
	.boxto = fb_rgb332_boxto,
	.lineto = fb_rgb332_lineto,
	.putstr = fb_rgb332_putstr,
	.flush = fb_ssd1963_flush,
	.width = SSD1963_WIDTH,
	.height = SSD1963_HEIGHT
};

static struct fb_rgb332 fb_ssd1963_rgb332 = {
	.mem = fb_ssd1963_mem
};

struct framebuffer *framebuffer = &fb_ssd1963_framebuffer;
struct fb_rgb332 *fb_rgb332 = &fb_ssd1963_rgb332;
