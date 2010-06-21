/* RF Channel utilities */

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

#include <layer1/sync.h>


/* RF Channel parameters */
void rfch_get_params(struct gsm_time *t,
                     uint16_t *arfcn_p, uint8_t *tsc_p, uint8_t *tn_p)
{
	if (l1s.dedicated.type == GSM_DCHAN_NONE) {
		/* Serving cell only */
		*arfcn_p = l1s.serving_cell.arfcn;
		*tsc_p   = l1s.serving_cell.bsic & 0x7;
		*tn_p    = 0;
	} else {
		/* Dedicated channel */
		if (l1s.dedicated.h) {
			/* Not supported yet */
		} else {
			*arfcn_p = l1s.dedicated.h0.arfcn;
		}

		*tsc_p = l1s.dedicated.tsc;
		*tn_p  = l1s.dedicated.tn;
	}
}

