/* Sitronix ST7558 LCD Driver */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
#include <memory.h>
#include <i2c.h>
#include <display.h>
#include <calypso/clock.h>

#define MORE_CONTROL	0x80
#define CONTROL_RS_RAM	0x40
#define CONTROL_RS_CMD	0x00
#define Y_ADDR(n)	(0x40|((n)&0xf))
#define X_ADDR(n)	(0x80|((n)&0x3f))

static const uint8_t setup[] = { CONTROL_RS_CMD, 0x2e, 0x21, 0x12, 0xc0, 0x0b,
						 0x20, 0x11, 0x00, 0x40, 0x80 };
static const uint8_t home[] = { CONTROL_RS_CMD, Y_ADDR(0), X_ADDR(0) };

/* video modes */
static const uint8_t invert[] = { CONTROL_RS_CMD, 0x20, 0x0d };
static const uint8_t normal[] = { CONTROL_RS_CMD, 0x20, 0x0c };
static const uint8_t off[] = { CONTROL_RS_CMD, 0x20, 0x08 };

#define ST7558_SLAVE_ADDR	0x3c
static int st7558_write(const uint8_t *data, int len)
{
	int rc = i2c_write(ST7558_SLAVE_ADDR, data[0], 1, data+1, len-1);
	return rc;
}

static const uint8_t zero16[] = { CONTROL_RS_RAM,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0 };
static void st7558_clrscr(void)
{
	int i;

	st7558_write(home, sizeof(home));

	for (i = 0; i < 102*9; i += 16)
		st7558_write(zero16, sizeof(zero16));

	st7558_write(home, sizeof(home));
}

static void st7558_init(void)
{
	/* Release nRESET */
	calypso_reset_set(RESET_EXT, 0);

	i2c_init(0,0);

	st7558_write(setup, sizeof(setup));
	st7558_clrscr();
}

static void st7558_set_attr(unsigned long attr)
{
	if (attr & DISP_ATTR_INVERT)
		st7558_write(invert, sizeof(invert));
}

static void st7558_unset_attr(unsigned long attr)
{
	if (attr & DISP_ATTR_INVERT)
		st7558_write(normal, sizeof(normal));
}

/* FIXME: we need a mini-libc */
static void *mcpy(uint8_t *dst, const uint8_t *src, int len)
{
	while (len--)
		*dst++ = *src++;

	return dst;
}

extern const unsigned char fontdata_r8x8[];

static void st7558_putc(unsigned char c)
{
	uint8_t putc_buf[16];
	uint8_t bytes_per_char = 8;

	putc_buf[0] = CONTROL_RS_RAM;
	mcpy(putc_buf+1, fontdata_r8x8+(c*bytes_per_char), bytes_per_char);
	st7558_write(putc_buf, 1+bytes_per_char);
}

const struct display_driver st7558_display = {
	.name = "st7558",
	.init = &st7558_init,
	.clrscr = &st7558_clrscr,
	.set_attr = &st7558_set_attr,
	.unset_attr = &st7558_unset_attr,
	.putc = &st7558_putc,
};
