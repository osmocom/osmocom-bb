/* Calypso DBB internal PWT (Pulse Width / T) Buzzer Driver */

/* (C) 2010 by Jose Pereira <onaips@gmail.com>
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

#define BASE_ADDR_PWL	0xfffe8800
#define PWT_REG(m)	(BASE_ADDR_PWL + (m))

#define ASIC_CONF_REG		0xfffef008
#define BUZZ_LEVEL_REG		0xfffe480e

enum pwt_reg {
	FRC_REG	= 0,
	VRC_REG = 1,
	GCR_REG = 2,
};

#define ASCONF_PWT_ENA	(1 << 5)

void buzzer_mode_pwt(int on)
{
	uint16_t reg;

	reg = readw(ASIC_CONF_REG);

	if (on) {
		/* Enable pwt */
		writeb(0x01, PWT_REG(GCR_REG));
		/* Switch pin from LT to PWL */
		reg |= ASCONF_PWT_ENA;
		writew(reg, ASIC_CONF_REG);
	} else {
		/* Switch pin from PWT to BU */
		reg |= ~ASCONF_PWT_ENA;
		writew(reg, ASIC_CONF_REG);
		/* Disable pwt */
		writeb(0x00, PWT_REG(GCR_REG));
	}
}

void buzzer_volume(uint8_t level)
{

	if (readw(ASIC_CONF_REG) & ASCONF_PWT_ENA) {

	  if (level) {
		//scaling the volume as pwt only knows 0..63
		level = level >> 1;
		//if level > 0 buzzer is on
		level |= 0x01;
	  }

	  writeb(level,PWT_REG(VRC_REG));

	} else {
		/* we need to scale the buzz level, as the
		 * ARMIO buzz controller only knows 0..63 */
		writeb(level>>2, BUZZ_LEVEL_REG);
	}
}

void buzzer_note(uint8_t note)
{
  if ( (readw(ASIC_CONF_REG) & ASCONF_PWT_ENA) )
      writeb(note,PWT_REG(FRC_REG));
}
