/* Framebuffer implementation - combined Sunplus SPCA552E and
 * Samsung S6B33B1X LCD driver - as used in the Pirelli DP-L10 */

/* (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * based on fb_ssd1783.c:
 * (C) 2010 by Christian Vogel <vogelchr@vogel.cx>
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
#include <memory.h>

#define S6B33B1X_WIDTH		128
#define S6B33B1X_HEIGHT		128
#define LCD_INVIS_X_PIXELS	4

#define ARMIO_LATCH_OUT		0xfffe4802
#define nCS4_ADDR		0x02800000

static uint8_t fb_s6b33b1x_mem[S6B33B1X_WIDTH * S6B33B1X_HEIGHT];

enum s6b33b1x_cmdflag { CMD, DATA, END };

struct s6b33b1x_cmdlist {
	enum s6b33b1x_cmdflag is_cmd:8;	/* 1: is a command, 0: is data, 2: end marker! */
	uint8_t data;			/* 8 bit to send to LC display */
} __attribute__((packed));

static const struct s6b33b1x_cmdlist
s6b33b1x_initdata[] = {
	{ CMD,  0x26 }, /* CMD   DCDC and AMP ON/OFF set */
	{ DATA, 0x00 }, /* DATA: everything off */
	{ CMD,  0x02 }, /* CMD   Oscillation Mode Set */
	{ DATA, 0x00 }, /* DATA: oscillator off */
	{ CMD,  0x2c }, /* CMD   Standby Mode off */
	{ CMD,  0x50 }, /* CMD   Display off */
	{ CMD,  0x02 }, /* CMD   Oscillation Mode Set */
	{ DATA, 0x01 }, /* DATA: oscillator on */
	{ CMD,  0x26 }, /* CMD   DCDC and AMP ON/OFF set */
	{ DATA, 0x01 }, /* DATA: Booster 1 on */
	{ CMD,  0x26 }, /* CMD   DCDC and AMP ON/OFF set */
	{ DATA, 0x09 }, /* DATA: Booster 1 on, OP-AMP on */
	{ CMD,  0x26 }, /* CMD   DCDC and AMP ON/OFF set */
	{ DATA, 0x0b }, /* DATA: Booster 1 + 2 on, OP-AMP on */
	{ CMD,  0x26 }, /* CMD   DCDC and AMP ON/OFF set */
	{ DATA, 0x0f }, /* DATA: Booster 1 + 2 + 3 on, OP-AMP on */
	{ CMD,  0x20 }, /* CMD   DC-DC Select */
	{ DATA, 0x01 }, /* DATA: step up x1.5 */
	{ CMD,  0x24 }, /* CMD   DCDC Clock Division Set */
	{ DATA, 0x0a }, /* DATA: fPCK = fOSC/6 */
	{ CMD,  0x2a }, /* CMD   Contrast Control */
	{ DATA, 0x2d }, /* DATA: default contrast */
	{ CMD,  0x30 }, /* CMD   Adressing mode set */
	{ DATA, 0x0b }, /* DATA: 65536 color mode */
	{ CMD,  0x10 }, /* CMD   Driver output mode set */
	{ DATA, 0x03 }, /* DATA: Display duty: 1/132 */
	{ CMD,  0x34 }, /* CMD   N-line inversion set */
	{ DATA, 0x88 }, /* DATA: inversion on, one frame, every 8 blocks */
	{ CMD,  0x40 }, /* CMD   Entry mode set */
	{ DATA, 0x00 }, /* DATA: Y address counter mode */
	{ CMD,  0x28 }, /* CMD   Temperature Compensation set */
	{ DATA, 0x01 }, /* DATA: slope -0.05%/degC */
	{ CMD,  0x32 }, /* CMD   ROW vector mode set */
	{ DATA, 0x01 }, /* DATA: every 2 subgroup */
	{ CMD,  0x51 }, /* CMD   Display on */
	{ END,  0x00 }, /* MARKER: end of list */
};

static void fb_s6b33b1x_send_cmdlist(const struct s6b33b1x_cmdlist *p)
{
	while(p->is_cmd != END){
		writew(p->data, nCS4_ADDR);
		p++;
	}
}

static void fb_spca_write(uint16_t addr, uint16_t val)
{
	writew(addr, nCS4_ADDR);
	delay_ms(1);
	writew(val , nCS4_ADDR | 2);
}

static void fb_spca_init(void)
{
	uint16_t reg;

	/* Initialize Sunplus SPCA552E Media Controller for bypass mode */
	fb_spca_write(0x7e, 0x00);	/* internal register access */
	delay_ms(10);
	fb_spca_write(0x7a, 0x00);	/* keep CPU in reset state */
	delay_ms(10);
	fb_spca_write(0x7f, 0x00);	/* select main page */
	delay_ms(5);
	fb_spca_write(0x72, 0x07);	/* don't reshape timing, 16 bit mode */
	fb_spca_write(0x14, 0x03);
	fb_spca_write(0x7f, 0x00);	/* select main page */
	delay_ms(5);
	fb_spca_write(0x06, 0xff);
	fb_spca_write(0x7f, 0x09);
	fb_spca_write(0x19, 0x08);	/* backlight: 0x08 is on, 0x0c is off */
	fb_spca_write(0x23, 0x18);

	/* enable bypass mode */
	reg = readw(ARMIO_LATCH_OUT);
	reg |= (1 << 7);
	writew(reg, ARMIO_LATCH_OUT);
}

static void fb_s6b33b1x_init(void)
{
	printf("%s: initializing LCD.\n",__FUNCTION__);

	fb_spca_init();
	fb_s6b33b1x_send_cmdlist(s6b33b1x_initdata);
}

static void fb_s6b33b1x_flush(void)
{
	int x,y;
	uint8_t *p;
	struct s6b33b1x_cmdlist prepare_disp_write_cmds[] = {
		{ CMD,  0x42 },			 /*  set column address */
		{ DATA, fb_rgb332->damage_x1 + LCD_INVIS_X_PIXELS },
		{ DATA, fb_rgb332->damage_x2 + LCD_INVIS_X_PIXELS - 1 },
		{ CMD,  0x43 },			 /*  set page address (Y) */
		{ DATA, fb_rgb332->damage_y1 },
		{ DATA, fb_rgb332->damage_y2 - 1 },
		{ END,  0x00 }
	};

	/* If everything's clean, just return */
	if(fb_rgb332->damage_x1 == fb_rgb332->damage_x2 ||
		fb_rgb332->damage_y1 == fb_rgb332->damage_y2) {
			printf("%s: no damage\n",__FUNCTION__);
			return;
	}

	fb_s6b33b1x_send_cmdlist(prepare_disp_write_cmds);

	for(y=fb_rgb332->damage_y1;y<fb_rgb332->damage_y2;y++) {
		p = & fb_rgb332->mem[y * framebuffer->width]; // start of line
		p += fb_rgb332->damage_x1; // start of damage area

		for(x=fb_rgb332->damage_x1; x<fb_rgb332->damage_x2; x++) {
			uint16_t data = rgb332_to_565(*p++);
			writew(data , nCS4_ADDR | 2);
		}
	}

	fb_rgb332->damage_x1 = fb_rgb332->damage_x2 = 0;
	fb_rgb332->damage_y1 = fb_rgb332->damage_y2 = 0;
}

static struct framebuffer fb_s6b33b1x_framebuffer = {
	.name = "s6b33b1x",
	.init = fb_s6b33b1x_init,
	.clear = fb_rgb332_clear,
	.boxto = fb_rgb332_boxto,
	.lineto = fb_rgb332_lineto,
	.putstr = fb_rgb332_putstr,
	.flush = fb_s6b33b1x_flush,
	.width = S6B33B1X_WIDTH,
	.height = S6B33B1X_HEIGHT
};

static struct fb_rgb332 fb_s6b33b1x_rgb332 = {
	.mem = fb_s6b33b1x_mem
};

struct framebuffer *framebuffer = &fb_s6b33b1x_framebuffer;
struct fb_rgb332 *fb_rgb332 = &fb_s6b33b1x_rgb332;
