/* Solomon SSD1783 LCD Driver (Epson S1D15G10D08B000 clone) */

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
//#define DEBUG
#include <debug.h>
#include <delay.h>
#include <uwire.h>
#include <display.h>
#include <display/ssd1783.h>
#include <calypso/clock.h>

#define LCD_COLUMNS		98
#define LCD_ROWS		67
#define LCD_TOP_FREE_ROWS	3
#define LCD_LEFT_FREE_COLS	0
#define	PIXEL_BYTES		3
#define SSD1783_UWIRE_BITLEN 	9
#define SSD1783_DEV_ID		0
#define FONT_HEIGHT		8
#define FONT_WIDTH		8

static const uint8_t rgb8_palette[] ={
	0x00,	//P01	Intermediate red tone 000
	0x03,	//P02	Intermediate red tone 001
	0x05,	//P03	Intermediate red tone 010
	0x07,	//P04	Intermediate red tone 011
	0x09,	//P05	Intermediate red tone 100
	0x0b,	//P06	Intermediate red tone 101
	0x0d,	//P07	Intermediate red tone 110
	0x0f,	//P08	Intermediate red tone 111
	0x00,	//P09	Intermediate green tone 000
	0x03,	//P10	Intermediate green tone 001
	0x05,	//P11	Intermediate green tone 010
	0x07,	//P12	Intermediate green tone 011
	0x09,	//P13	Intermediate green tone 100
	0x0b,	//P14	Intermediate green tone 101
	0x0d,	//P15	Intermediate green tone 110
	0x0f,	//P16	Intermediate green tone 111
	0x00,	//P17	Intermediate blue tone 00
	0x05,	//P18	Intermediate blue tone 01
	0x0a,	//P19	Intermediate blue tone 10
	0x0f,	//P20	Intermediate blue tone 11
};

static void ssd1783_cmd_write(const uint8_t cmd)
{
	uint16_t cmd_out = cmd;
	uwire_xfer(SSD1783_DEV_ID, SSD1783_UWIRE_BITLEN, &cmd_out, NULL);
}

static void ssd1783_data_write(const uint8_t data)
{
	uint16_t data_out = ((0x01 << 8) + data);
	uwire_xfer(SSD1783_DEV_ID, SSD1783_UWIRE_BITLEN, &data_out, NULL);
}

static void ssd1783_clrscr(void)
{
	uint16_t i;

	/* Select the whole display area for clearing */
	ssd1783_cmd_write(CMD_PASET);		/* Page address set [2] */
	ssd1783_data_write(0x00);		/* Start page: 0x00 */
	ssd1783_data_write(LCD_ROWS-1);		/* End page */
	ssd1783_cmd_write(CMD_CASET);		/* Column address set [2] */
	ssd1783_data_write(0x00);		/* Start column: 0x00 */
	ssd1783_data_write((LCD_COLUMNS/2)-1);	/* End column (2 pixels per column) */
	ssd1783_cmd_write(CMD_RAMWR);		/* Write to memory */

	/* Fill the display with white */
	for(i=0; i < (LCD_ROWS * (LCD_COLUMNS/2) * PIXEL_BYTES); i++){
		ssd1783_data_write(0xff);
	}
	ssd1783_cmd_write(CMD_NOP);		/* Terminate RAMWR with NOP */
}

static void ssd1783_init(void)
{
	unsigned int i;

	calypso_reset_set(RESET_EXT, 0);
	uwire_init();

	/* Begin SSD1783 initialization sequence */
	ssd1783_cmd_write(CMD_OSCON);		/* Internal OSC on */
	ssd1783_cmd_write(CMD_SLPOUT);		/* Sleep out (Leave sleep mode) */

	ssd1783_cmd_write(CMD_COMSCN);		/* Common scan direction [1] */
	ssd1783_data_write(0x01);		/* Scan 1 -> 68, 132 <- 69 */
	ssd1783_cmd_write(CMD_DATCTL);		/* Data Scan Direction [3] */
	ssd1783_data_write(0x00);		/* Normal page address, normal rotation,
						 * scan direction in column direction */
	ssd1783_data_write(0x00);		/* RGB arrangement: RGB-RGB */
	ssd1783_data_write(0x02);		/* Gray-scale setup: 16 gray-scale Type A, 8-bit mode */

	/* Initialize RGB8 palette for 8-Bit color mode */
	ssd1783_cmd_write(CMD_RGBSET8);		/* 256-color position set [20] */
	for(i=0; i < sizeof(rgb8_palette); i++){
		ssd1783_data_write(rgb8_palette[i]);
	}

	ssd1783_cmd_write(CMD_DISCTL);		/* Display control [3] */
	ssd1783_data_write(0xff);		/* no clock division, F1, F2 switching period = field */
	ssd1783_data_write(0x10);		/* Drive duty, P24 = 1 */
	ssd1783_data_write(0x01);		/* FR inverse set, P30=1 */
	ssd1783_cmd_write(CMD_SCSTART);		/* Scroll start set [1] */
	ssd1783_data_write(0x00);		/* Start block address 0x00 */

	/* Turn on the power regulator which generates VLCD */
	ssd1783_cmd_write(CMD_PWRCTR);		/* Power Control [1] */
	ssd1783_data_write(0x0b);		/* Booster, follower and regulator circuit on */

	/* FIXME: put this in a separate function (ssd1783_set_contrast) */
	ssd1783_cmd_write(CMD_VOLCTR);		/* Electronic Volume Control [2] */
	ssd1783_data_write(0x29);		/* Set contrast */
	ssd1783_data_write(0x05);		/* Set contrast */

	ssd1783_cmd_write(CMD_DISINV);		/* Invert Display */
	ssd1783_cmd_write(CMD_TMPGRD);		/* Temperature gradient set */
	ssd1783_data_write(0x00);		/* default temperature gradient (-0.05% / °C) */
	ssd1783_cmd_write(CMD_BIASSET);		/* Set biasing ratio [1] */
	ssd1783_data_write(0x03);		/* 1/10 bias */
	ssd1783_cmd_write(CMD_FREQSET);		/* Set frequency and n-line inversion [2] */
	ssd1783_data_write(0x08);		/* frequency: 75Hz (POR) */
	ssd1783_data_write(0x06);		/* n-line inversion: 6 lines */
	ssd1783_cmd_write(CMD_RESCMD);		/* reserved command in datasheet? */
	ssd1783_cmd_write(CMD_PWMSEL);		/* Select PWM/FRC, Full/8 color mode [3] */
	ssd1783_data_write(0x28);		/* fixed */
	ssd1783_data_write(0x2c);		/* 5 bits PWM + 1 bit FRC (POR) */
	ssd1783_data_write(0x05);		/* Full color mode (0x45 would be 8 color powersaving) */

	ssd1783_cmd_write(CMD_DISON);		/* Display ON */
	ssd1783_clrscr();			/* Clear the display */
}

extern const unsigned char fontdata_r8x8_horiz[];

/*
 * Pixel format for 8-bit mode, 12-bit color, 2 Pixel per 3 byte
 * D7, D6, D5, D4, D3, D2, D1, D0: RRRRGGGG (8 bits) 1st write
 * D7, D6, D5, D4, D3, D2, D1, D0: BBBBRRRR (8 bits) 2nd write
 * D7, D6, D5, D4, D3, D2, D1, D0: GGGGBBBB (8 bits) 3rd write
*/

static void ssd1783_goto_xy(int xpos, int ypos)
{
	ssd1783_cmd_write(CMD_PASET);
	ssd1783_data_write(xpos);
	ssd1783_data_write(xpos + (FONT_HEIGHT-1));

	ssd1783_cmd_write(CMD_CASET);
	ssd1783_data_write(ypos);
	ssd1783_data_write(ypos + ((FONT_WIDTH/2)-1));

	ssd1783_cmd_write(CMD_NOP);
}

static int ssd1783_putc_col(unsigned char c, int fColor, int bColor)
{
	int i, j;
	uint8_t cols = FONT_WIDTH;
	uint8_t rows = FONT_HEIGHT;
	uint8_t row_slice;
	uint8_t rowmask;
	uint16_t pixel0;	/* left pixel */
	uint16_t pixel1;	/* right pixel */

	ssd1783_cmd_write(CMD_RAMWR);

	for (i = 0; i < rows; i++) {
		row_slice = fontdata_r8x8_horiz[(FONT_WIDTH * c)+i];
		printd("\nSSD1783 FontData=0x%02hx", row_slice);
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
			ssd1783_data_write((pixel0 >> 4) & 0xff);
			ssd1783_data_write(((pixel0 & 0x00f) << 4) | ((pixel1 >> 8) & 0x00f));
			ssd1783_data_write(pixel1 & 0xff);
		}
	}
	ssd1783_cmd_write(CMD_NOP);

	return c;
}

static int ssd1783_puts_col(const char *str, int txtline, int fColor, int bColor)
{
	int i;
	for (i = 0; *str != 0x00; i += (FONT_WIDTH/2)) {
		ssd1783_goto_xy(((txtline*FONT_HEIGHT)+LCD_TOP_FREE_ROWS),
				(i + LCD_LEFT_FREE_COLS));
		ssd1783_putc_col(*str++, fColor, bColor);
	}

	return 0;
}

/* interface to display driver core */

static void ssd1783_set_attr(unsigned long attr)
{
	/* FIXME */
}

static int ssd1783_putc(unsigned int c)
{
	return ssd1783_putc_col(c, BLACK, WHITE);
}

static int ssd1783_puts(const char *str)
{
	return ssd1783_puts_col(str, 0, BLACK, WHITE);
}

const struct display_driver ssd1783_display = {
	.name = "ssd1783",
	.init = &ssd1783_init,
	.set_attr = &ssd1783_set_attr,
	.unset_attr = &ssd1783_set_attr,
	.clrscr = &ssd1783_clrscr,
	.goto_xy = &ssd1783_goto_xy,
	.putc = &ssd1783_putc,
	.puts = &ssd1783_puts,
};
