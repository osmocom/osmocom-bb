/* Layer 1 Various primitive utilities */

/* (C) 2010 by Sylvain Munaut <tnt@246tNt.com>
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

#include <osmocom/core/msgb.h>
#include <layer1/sync.h>


static const uint8_t ubUui[23] = {
	/* dummy lapdm header */
	0x01, 0x03, 0x01,

	/* fill bytes */
	0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b,
	0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b,
	0x2b, 0x2b, 0x2b, 0x2b
};

static uint8_t ubMeas[23] = {
	/* L1 SAACH pseudo-header */
	0x0f, 0x00,

	/* lapdm header */
	0x01, 0x03, 0x49,

	/* Measurement report */
	0x06, 0x15, 0x36, 0x36, 0x01, 0xC0, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00
};


const uint8_t *pu_get_idle_frame(void)
{
	return ubUui;
}

void pu_update_rx_level(uint8_t rx_level)
{
	ubMeas[7] = ubMeas[8] = rx_level;
}

const uint8_t *pu_get_meas_frame(void)
{
	if (l1s.tx_meas) {
		return l1s.tx_meas->l3h;
	} else {
		/* Update L1 SAACH pseudo-header */
		ubMeas[0] = l1s.tx_power;
		ubMeas[1] = l1s.ta;

		return ubMeas;
	}
}
