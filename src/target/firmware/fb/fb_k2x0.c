/* Framebuffer implementation for SE K200i/K220i -
 * combined driver for Core Logic CL761ST and S6B33B1X derivative */

/* (C) 2022 by Steve Markgraf <steve@steve-m.de>
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

#include <fb/framebuffer.h>
#include <fb/fb_rgb332.h>

#include <stdint.h>
#include <stdio.h>
#include <delay.h>
#include <memory.h>

#define K2X0_WIDTH		128
#define K2X0_HEIGHT		128

#define ARMIO_LATCH_OUT		0xfffe4802
#define CS3_ADDR		0x02000000

#define DISPLAY_CMD_ADDR	(CS3_ADDR + 0)
#define DISPLAY_DATA_ADDR	(CS3_ADDR + 2)
#define CL761_INDEX_ADDR	(CS3_ADDR + 4)
#define CL761_DATA_ADDR		(CS3_ADDR + 6)

#define CL761_CLK		(1 << 4)
#define CL761_RESET		(1 << 9)
#define K2X0_ENABLE_BACKLIGHT	(1 << 3)

static uint8_t fb_k2x0_mem[K2X0_WIDTH * K2X0_HEIGHT];

static const uint8_t k2x0_initdata[] = {
	0x2c,	/* CMD:  Standby Mode off */
	0x02,	/* CMD:  Oscillation Mode Set */
	0x01,	/* DATA: oscillator on */
	0x26,	/* CMD:  DCDC and AMP ON/OFF set */
	0x01,	/* DATA: Booster 1 on */
	0x26,	/* CMD:  DCDC and AMP ON/OFF set */
	0x09,	/* DATA: Booster 1 on, OP-AMP on */
	0x26,	/* CMD:  DCDC and AMP ON/OFF set */
	0x0b,	/* DATA: Booster 1 + 2 on, OP-AMP on */
	0x26,	/* CMD:  DCDC and AMP ON/OFF set */
	0x0f,	/* DATA: Booster 1 + 2 + 3 on, OP-AMP on */
	0x10,	/* CMD:  Driver output mode set */
	0x00,	/* DATA: Display duty: 1/66 */
	0x20,	/* CMD:  DC-DC Select */
	0x01,	/* DATA: step up x1.5 */
	0x24,	/* CMD:  DCDC Clock Division Set */
	0x08,	/* DATA: fPCK = fOSC/32 */
	0x28,	/* CMD:  Temperature Compensation set */
	0x02,	/* DATA: slope -0.10%/degC */
	0x2a,	/* CMD:  Contrast Control */
	0x1d,	/* DATA: Constrast Level 29 */
	0x30,	/* CMD:  Addressing mode set */
	0x53,	/* DATA: 256 color mode (orignal FW uses 0x13, 65k colors) */
	0x32,	/* CMD:  ROW vector mode set */
	0x0e,	/* DATA: every subframe */
	0x34,	/* CMD:  N-block inversion set */
	0x8d,	/* DATA: inversion on, every 1 block and every 2 frames */
	0x36,	/* CMD:  unknown */
	0x00,	/* DATA: unknown */
	0x40,	/* CMD:  Entry mode set */
	0x80,	/* DATA: Y address counter mode */
	0x45,	/* CMD:  RAM Skip Area Set */
	0x00,	/* DATA: No Skip */
	0x53,	/* CMD:  Specified Display Pattern Set */
	0x00,	/* DATA: Normal display */
	0x55,	/* CMD:  Partial Display Mode Set */
	0x00,	/* DATA: Partial display OFF */
	0x51,	/* CMD:  Display on */
};

uint16_t cl761_read_reg(uint16_t reg)
{
	writew(reg, CL761_INDEX_ADDR);
	return readw(CL761_INDEX_ADDR);
}

void cl761_write_reg(uint8_t reg, uint16_t data)
{
	writew(reg, CL761_INDEX_ADDR);
	writew(data, CL761_DATA_ADDR);
}

static void fb_k2x0_init(void)
{
	unsigned int i;
	uint16_t reg;

	printf("%s: initializing LCD.\n", __FUNCTION__);

	reg = readw(ARMIO_LATCH_OUT);
	reg &= ~(CL761_RESET | (1 << 1));
	reg |= CL761_CLK;
	writew(reg, ARMIO_LATCH_OUT);
	delay_ms(10);
	reg |= CL761_RESET;
	writew(reg, ARMIO_LATCH_OUT);

	/* we need to perform a dummy register read for the
	 * CL761 to pass through the chip select to the display */
	cl761_read_reg(0x2e);
	delay_ms(1);

	reg &= ~CL761_CLK;
	reg |= (1 << 1) | K2X0_ENABLE_BACKLIGHT;
	writew(reg, ARMIO_LATCH_OUT);

	for (i = 0; i < sizeof(k2x0_initdata); i++)
		writew(k2x0_initdata[i], DISPLAY_CMD_ADDR);
}

static void fb_k2x0_flush(void)
{
	unsigned int i;
	int x, y;
	uint8_t *p;
	uint8_t prepare_disp_write_cmds[] = {
		0x43,			 /*  set column address */
		fb_rgb332->damage_x1,
		fb_rgb332->damage_x2 - 1,
		0x42,			 /*  set page address (Y) */
		fb_rgb332->damage_y1,
		fb_rgb332->damage_y2 - 1,
	};

	/* If everything's clean, just return */
	if (fb_rgb332->damage_x1 == fb_rgb332->damage_x2 ||
		fb_rgb332->damage_y1 == fb_rgb332->damage_y2) {
			printf("%s: no damage\n", __FUNCTION__);
			return;
	}

	for (i = 0; i < sizeof(prepare_disp_write_cmds); i++)
		writew(prepare_disp_write_cmds[i], DISPLAY_CMD_ADDR);

	for (y = fb_rgb332->damage_y1; y < fb_rgb332->damage_y2; y++) {
		p = & fb_rgb332->mem[y * framebuffer->width]; // start of line
		p += fb_rgb332->damage_x1; // start of damage area

		for (x = fb_rgb332->damage_x1; x < fb_rgb332->damage_x2; x++) {
				/* For whatever reason, the 256 color mode of this
				 * display uses 'RBG323' */
				uint8_t d = *p++;
				d = (d & 0xe0) | ((d & 0x1c) >> 2) | ((d & 0x03) << 3);

				/* We need to transfer the data twice in the 256 color mode.
				 * Interestingly, the red and green information is taken
				 * from the first byte written, and the blue information
				 * from the second byte written. */
				writew(d, DISPLAY_DATA_ADDR);
				writew(d, DISPLAY_DATA_ADDR);
		}
	}

	fb_rgb332->damage_x1 = fb_rgb332->damage_x2 = 0;
	fb_rgb332->damage_y1 = fb_rgb332->damage_y2 = 0;
}

static struct framebuffer fb_k2x0_framebuffer = {
	.name = "k2x0",
	.init = fb_k2x0_init,
	.clear = fb_rgb332_clear,
	.boxto = fb_rgb332_boxto,
	.lineto = fb_rgb332_lineto,
	.putstr = fb_rgb332_putstr,
	.flush = fb_k2x0_flush,
	.width = K2X0_WIDTH,
	.height = K2X0_HEIGHT
};

static struct fb_rgb332 fb_k2x0_rgb332 = {
	.mem = fb_k2x0_mem
};

struct framebuffer *framebuffer = &fb_k2x0_framebuffer;
struct fb_rgb332 *fb_rgb332 = &fb_k2x0_rgb332;
