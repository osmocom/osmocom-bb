/* Calypso DBB internal PWL (Pulse Width / Light) Driver */

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
#include <memory.h>

#define BASE_ADDR_PWL	0xfffe8000
#define PWL_REG(m)	(BASE_ADDR_PWL + (m))

#define ASIC_CONF_REG		0xfffef008
#define LIGHT_LEVEL_REG		0xfffe4810

enum pwl_reg {
	PWL_LEVEL	= 0,
	PWL_CTRL	= 1,
};

#define ASCONF_PWL_ENA	(1 << 4)

void bl_mode_pwl(int on)
{
	uint16_t reg;

	reg = readw(ASIC_CONF_REG);

	if (on) {
		/* Enable pwl */
		writeb(0x01, PWL_REG(PWL_CTRL));
		/* Switch pin from LT to PWL */
		reg |= ASCONF_PWL_ENA;
		writew(reg, ASIC_CONF_REG);
	} else {
		/* Switch pin from PWL to LT */
		reg &= ~ASCONF_PWL_ENA;
		writew(reg, ASIC_CONF_REG);
		/* Disable pwl */
		writeb(0x00, PWL_REG(PWL_CTRL));
	}
}

void bl_level(uint8_t level)
{
	if (readw(ASIC_CONF_REG) & ASCONF_PWL_ENA) {
		writeb(level, PWL_REG(PWL_LEVEL));
	} else {
		/* we need to scale the light level, as the
		 * ARMIO light controller only knows 0..63 */
		writeb(level>>2, LIGHT_LEVEL_REG);
	}
}
