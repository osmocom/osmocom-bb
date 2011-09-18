/* Solomon SSD1963 LCD Driver (probably not exactly the SSD1963)
 * as used in the Sony Ericsson J100i */

/* (C) 2010-11 by Steve Markgraf <steve@steve-m.de>
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
#include <debug.h>
#include <delay.h>
#include <uwire.h>
#include <display.h>
#include <calypso/clock.h>

#define LCD_COLUMNS		96
#define LCD_ROWS		64
#define LCD_TOP_FREE_ROWS	3
#define LCD_LEFT_FREE_COLS	0
#define	PIXEL_BYTES		3
#define SSD1963_UWIRE_BITLEN	9
#define SSD1963_DEV_ID		0
#define FONT_HEIGHT		8
#define FONT_WIDTH		8

#define BLACK		0x0000
#define WHITE		0x0fff

static void ssd1963_cmd_write(const uint8_t cmd)
{
	uint16_t cmd_out = cmd;
	uwire_xfer(SSD1963_DEV_ID, SSD1963_UWIRE_BITLEN, &cmd_out, NULL);
}

static void ssd1963_data_write(const uint8_t data)
{
	uint16_t data_out = ((0x01 << 8) + data);
	uwire_xfer(SSD1963_DEV_ID, SSD1963_UWIRE_BITLEN, &data_out, NULL);
}

static void ssd1963_clrscr(void)
{
	uint16_t i;

	/* Select the whole display area for clearing */
	ssd1963_cmd_write(0x2b);
	ssd1963_data_write(0x00);
	ssd1963_data_write(LCD_ROWS-1);

	ssd1963_cmd_write(0x2a);
	ssd1963_data_write(0x00);
	ssd1963_data_write(LCD_COLUMNS-1);

	ssd1963_cmd_write(0x2c);

	/* Fill the display with white */
	for(i=0; i < (LCD_ROWS * (LCD_COLUMNS/2) * PIXEL_BYTES); i++){
		ssd1963_data_write(0xff);
	}
}

static void ssd1963_init(void)
{
	unsigned int i;

	calypso_reset_set(RESET_EXT, 0);
	uwire_init();
	delay_ms(3);

	/* Begin SSD1963 initialization sequence */
	ssd1963_cmd_write(0xb6);	/* Set vertical period */
	ssd1963_data_write(0x4b);
	ssd1963_data_write(0xf1);
	ssd1963_data_write(0x40);
	ssd1963_data_write(0x40);
	ssd1963_data_write(0x00);
	ssd1963_data_write(0x8c);
	ssd1963_data_write(0x00);
	
	ssd1963_cmd_write(0x3a);	/* Set pixel format */
	ssd1963_data_write(0x03);	/* 0x03: 12 bit, 0x05: 16 Bit / pixel */
	ssd1963_cmd_write(0x11);

	/* Contrast/Electronic Volume Control */
	ssd1963_cmd_write(0xba);
	ssd1963_data_write(0x5b);
	ssd1963_data_write(0x84);

	ssd1963_cmd_write(0x36);
	ssd1963_data_write(0x00);

	ssd1963_cmd_write(0x13);	/* Enter normal mode */
	ssd1963_clrscr();

	ssd1963_cmd_write(0x29);	/* Display ON */
}

extern const unsigned char fontdata_r8x8_horiz[];

/*
 * Pixel format for 8-bit mode, 12-bit color, 2 Pixel per 3 byte
 * D7, D6, D5, D4, D3, D2, D1, D0: RRRRGGGG (8 bits) 1st write
 * D7, D6, D5, D4, D3, D2, D1, D0: BBBBRRRR (8 bits) 2nd write
 * D7, D6, D5, D4, D3, D2, D1, D0: GGGGBBBB (8 bits) 3rd write
*/

static void ssd1963_goto_xy(int xpos, int ypos)
{
	ssd1963_cmd_write(0x2b);
	ssd1963_data_write(xpos);
	ssd1963_data_write(xpos + FONT_HEIGHT-1);

	ssd1963_cmd_write(0x2a);
	ssd1963_data_write(ypos);
	ssd1963_data_write(ypos + FONT_WIDTH-1);
}

static int ssd1963_putc_col(unsigned char c, int fColor, int bColor)
{
	int i, j;
	uint8_t cols = FONT_WIDTH;
	uint8_t rows = FONT_HEIGHT;
	uint8_t row_slice;
	uint8_t rowmask;
	uint16_t pixel0;	/* left pixel */
	uint16_t pixel1;	/* right pixel */

	ssd1963_cmd_write(0x2c);

	for (i = 0; i < rows; i++) {
		row_slice = fontdata_r8x8_horiz[(FONT_WIDTH * c)+i];
		rowmask = 0x80;
		for (j = 0; j < cols; j += 2) {
			if (!(row_slice & rowmask))
				pixel0 = bColor;
			else
				pixel0 = fColor;
			rowmask = rowmask >> 1;
			if (!(row_slice & rowmask))
				pixel1 = bColor;
			else
				pixel1 = fColor;
			rowmask = rowmask >> 1;
			/* Write the RGB-RGB pixel data */
			ssd1963_data_write((pixel0 >> 4) & 0xff);
			ssd1963_data_write(((pixel0 & 0x00f) << 4) | ((pixel1 >> 8) & 0x00f));
			ssd1963_data_write(pixel1 & 0xff);
		}
	}
	ssd1963_cmd_write(0x00);

	return c;
}

static int ssd1963_puts_col(const char *str, int txtline, int fColor, int bColor)
{
	int i;
	for (i = 0; *str != 0x00; i += FONT_WIDTH) {
		ssd1963_goto_xy(((txtline*FONT_HEIGHT)+LCD_TOP_FREE_ROWS),
				(i + LCD_LEFT_FREE_COLS));
		ssd1963_putc_col(*str++, fColor, bColor);
	}

	return 0;
}

/* interface to display driver core */

static void ssd1963_set_attr(unsigned long attr)
{
	/* FIXME */
}

static int ssd1963_putc(unsigned int c)
{
	return ssd1963_putc_col(c, BLACK, WHITE);
}

static int ssd1963_puts(const char *str)
{
	return ssd1963_puts_col(str, 0, BLACK, WHITE);
}

const struct display_driver ssd1963_display = {
	.name = "ssd1963",
	.init = &ssd1963_init,
	.set_attr = &ssd1963_set_attr,
	.unset_attr = &ssd1963_set_attr,
	.clrscr = &ssd1963_clrscr,
	.goto_xy = &ssd1963_goto_xy,
	.putc = &ssd1963_putc,
	.puts = &ssd1963_puts,
};
