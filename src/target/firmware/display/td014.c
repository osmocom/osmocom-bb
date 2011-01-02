/* Toppoly TD014 LCD Driver, as used in the Motorola C139/C140 */

/* (C) 2010 by Steve Markgraf <steve@steve-m.de>
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
#define	PIXEL_BYTES		2
#define TD014_UWIRE_BITLEN 	9
#define TD014_DEV_ID		0
#define FONT_HEIGHT		8
#define FONT_WIDTH		8

#define BLACK		0x0000
#define WHITE		0xffff

static void td014_cmd_write(const uint8_t cmd)
{
	uint16_t cmd_out = cmd;
	uwire_xfer(TD014_DEV_ID, TD014_UWIRE_BITLEN, &cmd_out, NULL);
}

static void td014_data_write(const uint8_t data)
{
	uint16_t data_out = ((0x01 << 8) + data);
	uwire_xfer(TD014_DEV_ID, TD014_UWIRE_BITLEN, &data_out, NULL);
}

static void td014_clrscr(void)
{
	uint16_t i;

	/* Select the whole display area for clearing */
	td014_cmd_write(0x10);
	td014_data_write(0x00);
	td014_cmd_write(0x11);
	td014_data_write(0x00);
	td014_cmd_write(0x12);
	td014_data_write(LCD_COLUMNS-1);
	td014_cmd_write(0x13);
	td014_data_write(LCD_ROWS-1);
	td014_cmd_write(0x14);
	td014_data_write(0x00);
	td014_cmd_write(0x15);
	td014_data_write(0x00);

	/* Fill the display with white */
	for(i=0; i < (LCD_ROWS * LCD_COLUMNS * PIXEL_BYTES); i++) {
		td014_data_write(0xff);
	}
}

static void td014_init(void)
{
	calypso_reset_set(RESET_EXT, 0);
	uwire_init();
	delay_ms(3);

	td014_cmd_write(0x3f);
	td014_data_write(0x01);
	td014_cmd_write(0x20);
	td014_data_write(0x03);
	td014_cmd_write(0x31);
	td014_data_write(0x03);

	td014_clrscr();

}

extern const unsigned char fontdata_r8x8_horiz[];

static void td014_goto_xy(int xpos, int ypos)
{
	td014_cmd_write(0x10);
	td014_data_write(ypos);
	td014_cmd_write(0x11);
	td014_data_write(xpos);
	td014_cmd_write(0x12);
	td014_data_write(ypos + FONT_HEIGHT-1);
	td014_cmd_write(0x13);
	td014_data_write(xpos + FONT_WIDTH-1);
	td014_cmd_write(0x14);
	td014_data_write(ypos);
	td014_cmd_write(0x15);
	td014_data_write(xpos);

}

	/* RGB 556	  Byte 1 | Byte 2  *
	 * Pixel format: RRRRRGGG|GGBBBBBB */

static int td014_putc_col(unsigned char c, int fColor, int bColor)
{
	int i, j;
	uint8_t cols = FONT_WIDTH;
	uint8_t rows = FONT_HEIGHT;
	uint8_t row_slice;
	uint8_t rowmask;
	uint16_t pixel;

	for (i = 0; i < rows; i++) {
		row_slice = fontdata_r8x8_horiz[(FONT_WIDTH * c)+i];
		rowmask = 0x80;
		for (j = 0; j < cols; j++) {
			if (!(row_slice & rowmask))
				pixel = bColor;
			else
				pixel = fColor;
			rowmask = rowmask >> 1;
			/* Write the pixel data */
			td014_data_write((pixel >> 8) & 0xff);
			td014_data_write(pixel & 0xff);
		}
	}
	return c;
}

static int td014_puts_col(const char *str, int txtline, int fColor, int bColor)
{
	int i;
	for (i = 0; *str != 0x00; i += FONT_WIDTH) {
		td014_goto_xy(((txtline*FONT_HEIGHT)+LCD_TOP_FREE_ROWS),
				(i + LCD_LEFT_FREE_COLS));
		td014_putc_col(*str++, fColor, bColor);
	}

	return 0;
}

/* interface to display driver core */

static void td014_set_attr(unsigned long attr)
{
	/* FIXME */
}

static int td014_putc(unsigned int c)
{
	return td014_putc_col(c, BLACK, WHITE);
}

static int td014_puts(const char *str)
{
	return td014_puts_col(str, 0, BLACK, WHITE);
}

const struct display_driver td014_display = {
	.name = "td014",
	.init = &td014_init,
	.set_attr = &td014_set_attr,
	.unset_attr = &td014_set_attr,
	.clrscr = &td014_clrscr,
	.goto_xy = &td014_goto_xy,
	.putc = &td014_putc,
	.puts = &td014_puts,
};
