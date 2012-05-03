/* Framebuffer implementation - ST1783 LCD driver for C123 */
/* Based on st7558.c by Harald Welte */

/* (C) 2010 by Christian Vogel <vogelchr@vogel.cx>
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
#include <fb/fb_bw8.h>

#include <i2c.h>
#include <calypso/clock.h>
#include <delay.h>

#include <stdio.h>

/* Sitronix ST7558 LCD Driver for OSMOCOM framebuffer interface. */
/* (c) 2010 Christian Vogel <vogelchr@vogel.cx> */
/* Based on the initial LCD driver by Harald Welte */

#define CONTROL_RS_CMD

#define ST7558_SLAVE_ADDR	0x3c
#define ST7558_CMD_ADDR		0x00
#define ST7558_RAM_ADDR		0x40

#define ST7558_WIDTH		96	/* in pixels */
#define ST7558_HEIGHT		65

#define I2C_MAX_TRANSFER 16

static uint8_t
fb_st7558_mem[ST7558_WIDTH * ((ST7558_HEIGHT+7)/8)];

/* setup as initially proposed by Harald in st7558.c */
static const uint8_t st7558_setup[] = {
	0x2e,	/* ext. display control, set mirror X, set mirror Y*/
	0x21,	/* function set, enable extended instruction mode */
	0x12,	/* bias system BS[2,1,0] = [0,1,0] */
	0xc0,	/* set V_OP (V_OP6 = 1, V_OP[5:0] = 0) */
	0x0b,	/* booster stages PC1=1, PC0=1 */
	0x20,	/* function set, disable extended instruction mode */
	0x11,	/* V_LCD L/H select, PRS=1 */
	0x00,	/* NOP */
	0x0c,	/* normal video mode */
	0x40,	/* set X address to 0 */
	0x80	/* set Y address to 0 */
};


static void
fb_st7558_init(){
	calypso_reset_set(RESET_EXT, 0);
	i2c_init(0,0);

	/* initialize controller */
	i2c_write(ST7558_SLAVE_ADDR,ST7558_CMD_ADDR,1,
		  st7558_setup,sizeof(st7558_setup));
}

static void
fb_st7558_flush(){
	uint16_t x;
	int page,chunksize,nbytes;
	uint8_t *p;
	uint8_t cmd[2];

	if(fb_bw8->damage_y1 == fb_bw8->damage_y2 ||
		fb_bw8->damage_x1 == fb_bw8->damage_x2)
			return; /* nothing to update */

	/* update display in stripes of 8 rows, called "pages" */
	for(page=fb_bw8->damage_y1 >> 3;page <= fb_bw8->damage_y2>>3;page++){
		/* base offset in RAM framebuffer */
		x = fb_bw8->damage_x1;
		nbytes = fb_bw8->damage_x2 - fb_bw8->damage_x1;
		p = fb_bw8->mem + (page * framebuffer->width + x);

		/* i2c fifo can only handle a maximum of 16 bytes */
		while(nbytes){
			cmd[0]=0x40 | page; /* Set Y address of RAM. */
			cmd[1]=0x80 | x;
			chunksize = nbytes > I2C_MAX_TRANSFER ? I2C_MAX_TRANSFER : nbytes;

			i2c_write(ST7558_SLAVE_ADDR,ST7558_CMD_ADDR,1,cmd,sizeof(cmd));
			i2c_write(ST7558_SLAVE_ADDR,ST7558_RAM_ADDR,1,p,chunksize);

			nbytes -= chunksize;
			p+=I2C_MAX_TRANSFER;
			x+=I2C_MAX_TRANSFER;
		}
	}

	/* mark current buffer as unmodified! */
	fb_bw8->damage_x1 = fb_bw8->damage_x2 = 0;
	fb_bw8->damage_y1 = fb_bw8->damage_y2 = 0;
}

static struct framebuffer fb_st7558_framebuffer = {
	.name = "st7558",
	.init = fb_st7558_init,
	.clear = fb_bw8_clear,
	.boxto = fb_bw8_boxto,
	.lineto = fb_bw8_lineto,
	.putstr = fb_bw8_putstr,
	.flush = fb_st7558_flush,
	.width = ST7558_WIDTH,
	.height = ST7558_HEIGHT
};

static struct fb_bw8 fb_st7558_bw8 = {
	.mem = fb_st7558_mem
};

struct framebuffer *framebuffer = &fb_st7558_framebuffer;
struct fb_bw8 *fb_bw8 = &fb_st7558_bw8;
