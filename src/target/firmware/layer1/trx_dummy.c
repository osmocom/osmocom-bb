/* Dummy TRX interface for BTS primitive of Layer 1 */

/* Note: In theory, it's the APP responsability to provide an
 * implementation. Here we have weak dummy funcs in case the app
 * doesn't require a functional BTS mode
 */

/* (C) 2012 by Sylvain Munaut <tnt@246tNt.com>
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

#include <layer1/trx.h>

int __attribute__((weak))
trx_get_burst(uint32_t fn, uint8_t tn, uint8_t *data)
{
	return 0;
}

int __attribute__((weak))
trx_put_burst(uint32_t fn, uint8_t tn, uint8_t type, uint8_t *data)
{
	return 0;
}

void
trx_init(void)
{
}
