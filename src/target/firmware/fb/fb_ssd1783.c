/* Framebuffer implementation - SSD1783 LCD driver for C155 */
/* Based on ssd1783.c by Steve Markgraf and Harald Welte */

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
#include <fb/fb_rgb332.h>

#include <stdint.h>
#include <stdio.h>
#include <delay.h>
#include <uwire.h>
#include <calypso/clock.h>

#define SSD1783_WIDTH		98
#define SSD1783_HEIGHT		67
#define SSD1783_UWIRE_BITLEN 	9
#define SSD1783_DEV_ID		0

#define LCD_TOP_FREE_ROWS	3
#define LCD_LEFT_FREE_COLS	0
#define	PIXEL_BYTES		3
#define FONT_HEIGHT		8
#define FONT_WIDTH		8

static uint8_t fb_ssd1783_mem[SSD1783_WIDTH * SSD1783_HEIGHT];

enum ssd1783_cmdflag { CMD, DATA, END };

struct ssd1783_cmdlist {
	enum ssd1783_cmdflag is_cmd:8;	/* 1: is a command, 0: is data, 2: end marker! */
	uint8_t data;  			/* 8 bit to send to LC display */
} __attribute__((packed));

static const struct ssd1783_cmdlist
ssd1783_initdata[] = {
	{ CMD,  0xD1 }, /* CMD   set internal oscillator on */
	{ CMD,  0x94 }, /* CMD   leave sleep mode */
	{ CMD,  0xbb }, /* CMD   Set COM Output Scan Direction: */
	{ DATA, 0x01 }, /* DATA: 01: COM0-79, then COM159-80 */
/* -------- DIFFERENT FROM ORIGINAL CODE: -------------- */
/* we use 8bit per pixel packed RGB 332 */
	{ CMD,  0xbc }, /* CMD   Set Data Output Scan Direction */
	{ DATA, 0x00 }, /* DATA: column scan, normal rotation, normal display */
	{ DATA, 0x00 }, /* DATA: RGB color arrangement R G B R G B ... */
/*-->*/ { DATA, 0x01 }, /* DATA: 8 bit per pixel mode MSB <RRRGGGBB> LSB */
/* --------- /DIFFERENT ---------- */
	{ CMD,  0xce }, /* CMD   Set 256 Color Look Up Table LUT */
	{ DATA, 0x00 },	/* DATA red 000 */
	{ DATA, 0x03 },	/* DATA red 001 */
	{ DATA, 0x05 },	/* DATA red 010 */
	{ DATA, 0x07 },	/* DATA red 011 */
	{ DATA, 0x09 },	/* DATA red 100 */
	{ DATA, 0x0b },	/* DATA red 101 */
	{ DATA, 0x0d },	/* DATA red 110 */
	{ DATA, 0x0f },	/* DATA red 111 */
	{ DATA, 0x00 },	/* DATA green 000 */
	{ DATA, 0x03 },	/* DATA green 001 */
	{ DATA, 0x05 },	/* DATA green 010 */
	{ DATA, 0x07 },	/* DATA green 011 */
	{ DATA, 0x09 },	/* DATA green 100 */
	{ DATA, 0x0b },	/* DATA green 101 */
	{ DATA, 0x0d },	/* DATA green 110 */
	{ DATA, 0x0f },	/* DATA green 111 */
	{ DATA, 0x00 },	/* DATA blue 00 */
	{ DATA, 0x05 },	/* DATA blue 01 */
	{ DATA, 0x0a },	/* DATA blue 10 */
	{ DATA, 0x0f },	/* DATA blue 11 */
	{ CMD,  0xca }, /* CMD   Set Display Control - Driver Duty Selection */
	{ DATA, 0xff }, // can't find description of the values in the original
	{ DATA, 0x10 }, // display/ssd1783.c in my datasheet :-(
	{ DATA, 0x01 }, //
	{ CMD,  0xab }, /* CMD   Set Scroll Start */
	{ DATA, 0x00 }, /* DATA: Starting address at block 0 */
	{ CMD,  0x20 }, /* CMD   Set power control register */
	{ DATA, 0x0b }, /* DATA: booster 6x, reference gen. & int regulator */
	{ CMD,  0x81 }, /* CMD   Contrast Lvl & Int. Regul. Resistor Ratio */
	{ DATA, 0x29 }, /* DATA: contrast = 0x29 */
	{ DATA, 0x05 }, /* DATA: 0x05 = 0b101 -> 1+R2/R1 = 11.37 */
	{ CMD,  0xa7 }, /* CMD   Invert Display */
	{ CMD,  0x82 }, /* CMD   Set Temperature Compensation Coefficient */
	{ DATA, 0x00 }, /* DATA: Gradient is -0.10 % / degC */
	{ CMD,  0xfb }, /* CMD   Set Biasing Ratio */
	{ DATA, 0x03 }, /* DATA: 1/10 bias */
	{ CMD,  0xf2 }, /* CMD   Set Frame Frequency and N-line inversion */
	{ DATA, 0x08 }, /* DATA: 75 Hz (POR) */
	{ DATA, 0x06 }, /* DATA: n-line inversion: 6 lines */
	{ CMD,  0xf7 }, /* CMD   Select PWM/FRC Select Full Col./8col mode */
	{ DATA, 0x28 }, /* DATA: always 0x28 */
	{ DATA, 0x8c }, /* DATA: 4bit PWM + 2 bit FRC */
	{ DATA, 0x05 }, /* DATA: full color mode */
	{ CMD,  0xaf }, /* CMD   Display On */
	{ END,  0x00 }, /* MARKER: end of list */
};

static void
fb_ssd1783_send_cmdlist(const struct ssd1783_cmdlist *p){
	int i=0;
	while(p->is_cmd != END){
		uint16_t sendcmd = p->data;
		if(p->is_cmd == DATA)
			sendcmd |= 0x0100; /* 9th bit is cmd/data flag */
		uwire_xfer(SSD1783_DEV_ID, SSD1783_UWIRE_BITLEN, &sendcmd, NULL);
		p++;
		i++;
	}
}

static void
fb_ssd1783_init(void){
	printf("%s: initializing LCD.\n",__FUNCTION__);
	calypso_reset_set(RESET_EXT, 0);
	delay_ms(5);
	uwire_init();
	delay_ms(5);
	fb_ssd1783_send_cmdlist(ssd1783_initdata);
}

/* somehow the palette is messed up, RRR seems to have the
   bits reversed!  R0 R1 R2 G G G B B ---> R2 R1 R0 G G G B B */
static uint8_t fix_rrr(uint8_t v){
	return (v & 0x5f) | (v & 0x80) >> 2 | (v & 0x20) << 2;
}

static void
fb_ssd1783_flush(void){
	int x,y;
	uint8_t *p;
	struct ssd1783_cmdlist prepare_disp_write_cmds[] = {
		{ CMD,  0x15 },			 /*  set column address */
		{ DATA, fb_rgb332->damage_x1 },
		{ DATA, fb_rgb332->damage_x2-1 },
		{ CMD,  0x75 },			 /*  set page address (Y) */
		{ DATA, fb_rgb332->damage_y1 },
		{ DATA, fb_rgb332->damage_y2-1 },
		{ CMD,  0x5c },			 /* enter write display ram mode */
		{ END,  0x00 }
	};
	struct ssd1783_cmdlist nop[] = {
		{ CMD, 0x25 }, // NOP command
		{ END, 0x00 }
	};

	/* If everything's clean, just return */
	if(fb_rgb332->damage_x1 == fb_rgb332->damage_x2 ||
		fb_rgb332->damage_y1 == fb_rgb332->damage_y2){
			printf("%s: no damage\n",__FUNCTION__);
			return;
	}

	fb_ssd1783_send_cmdlist(prepare_disp_write_cmds);

	for(y=fb_rgb332->damage_y1;y<fb_rgb332->damage_y2;y++){
		p = & fb_rgb332->mem[y * framebuffer->width]; // start of line
		p += fb_rgb332->damage_x1; // start of damage area

		for(x=fb_rgb332->damage_x1;x<fb_rgb332->damage_x2;x++){
			uint16_t data = 0x0100 | fix_rrr(*p++); // dummy data
			uwire_xfer(SSD1783_DEV_ID, SSD1783_UWIRE_BITLEN,
					&data, NULL);
		}
	}
	fb_ssd1783_send_cmdlist(nop);

	fb_rgb332->damage_x1 = fb_rgb332->damage_x2 = 0;
	fb_rgb332->damage_y1 = fb_rgb332->damage_y2 = 0;
}

static struct framebuffer fb_ssd1783_framebuffer = {
	.name = "ssd1783",
	.init = fb_ssd1783_init,
	.clear = fb_rgb332_clear,
	.boxto = fb_rgb332_boxto,
	.lineto = fb_rgb332_lineto,
	.putstr = fb_rgb332_putstr,
	.flush = fb_ssd1783_flush,
	.width = SSD1783_WIDTH,
	.height = SSD1783_HEIGHT
};

static struct fb_rgb332 fb_ssd1783_rgb332 = {
	.mem = fb_ssd1783_mem
};

struct framebuffer *framebuffer = &fb_ssd1783_framebuffer;
struct fb_rgb332 *fb_rgb332 = &fb_ssd1783_rgb332;
