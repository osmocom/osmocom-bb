/* Calypso DU (Debug Unit) Driver */

/* (C) 2010 by Ingo Albrecht <prom@berlin.ccc.de>
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

#include <memory.h>
#include <stdint.h>
#include <stdio.h>

#include <calypso/du.h>

#define BASE_ADDR_DU	0x03c00000
#define DU_REG(m)	(BASE_ADDR_DU+(m))

void calypso_du_init() {
	unsigned char c;
	calypso_debugunit(1);
	for(c = 0; c < 64; c++) {
		writew(DU_REG(c), 0x00000000);
	}
}

void calypso_du_stop() {
	calypso_debugunit(0);
}

void calypso_du_dump() {
	unsigned char c;
	puts("Debug unit traceback:\n");
	for(c = 0; c < 64; c++) {
		uint32_t w = readw(DU_REG(c));
		printf("t-%2x: 0x%8x\n", c, (unsigned int)w);
	}
}
