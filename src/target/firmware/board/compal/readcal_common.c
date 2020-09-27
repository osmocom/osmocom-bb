/*
 * This code was written by Mychaela Falconia <falcon@freecalypso.org>
 * who refuses to claim copyright on it and has released it as public domain
 * instead. NO rights reserved, all rights relinquished.
 *
 * Tweaked (coding style changes) by Vadim Yanitskiy <axilirator@gmail.com>
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include <rf/txcal.h>

struct record_hdr {
	uint16_t valid_flag;
	uint16_t type;
	uint16_t native_len;
	uint16_t rounded_len;
};

static void apply_levels(struct txcal_tx_level *levels_table,
			 uint16_t *compal_data, unsigned start_level,
			 unsigned num_levels)
{
	unsigned n;

	for (n = 0; n < num_levels; n++)
		levels_table[start_level + n].apc = *compal_data++;
}

void read_compal_factory_records(uint32_t flash_addr)
{
	struct record_hdr *hdr;
	void *p, *sector_end;
	unsigned p_incr;
	void *payload;

	printf("Analyzing factory records sector "
		"at 0x%" PRIx32 "\n", flash_addr);

	p = (void *) flash_addr;
	sector_end = p + 0x2000;
	for (; p < sector_end; p += p_incr) {
		if ((sector_end - p) < 12)
			break;
		hdr = (struct record_hdr *)p;
		if (hdr->valid_flag == 0xFFFF)	/* blank flash */
			break;
		if (hdr->native_len > hdr->rounded_len) {
			printf("Bad record at 0x%" PRIx32 ": native length "
				"> rounded length\n", (uint32_t) p);
			return;
		}
		if (hdr->rounded_len & 3) {
			printf("Bad record at 0x%" PRIx32 ": rounded length "
				"is not aligned to 4\n", (uint32_t) p);
			return;
		}
		p_incr = hdr->rounded_len + 8;
		if (p_incr > (sector_end - p)) {
			printf("Bad record at 0x%" PRIx32 ": rounded length "
				"spills past the end of the sector\n", (uint32_t) p);
			return;
		}
		if (hdr->valid_flag != 0x000C)
			continue;
		payload = (void *)(hdr + 1);
		switch (hdr->type) {
		case 0x0000:
			if (hdr->native_len != 0x94)
				break;
			if (*(uint32_t *)(payload + 0x5C) != 0xAA)
				break;
			printf("Found 900 MHz band calibration record at "
				"0x%" PRIx32 ", applying\n", (uint32_t) p);
			apply_levels(rf_tx_levels_900, payload + 0x60, 5, 15);
			break;
		case 0x0001:
			if (hdr->native_len != 0xC8)
				break;
			if (*(uint32_t *)(payload + 0x7C) != 0xAA)
				break;
			printf("Found 1800 MHz band calibration record at "
				"0x%" PRIx32 ", applying\n", (uint32_t) p);
			apply_levels(rf_tx_levels_1800, payload + 0x80, 0, 16);
			break;
		case 0x0002:
			if (hdr->native_len != 0xB4)
				break;
			if (*(uint32_t *)(payload + 0x70) != 0xAA)
				break;
			printf("Found 1900 MHz band calibration record at "
				"0x%" PRIx32 ", applying\n", (uint32_t) p);
			apply_levels(rf_tx_levels_1900, payload + 0x74, 0, 16);
			break;
		case 0x0018:
			if (hdr->native_len != 0x88)
				break;
			if (*(uint32_t *)(payload + 0x54) != 0xAA)
				break;
			printf("Found 850 MHz band calibration record at "
				"0x%" PRIx32 ", applying\n", (uint32_t) p);
			apply_levels(rf_tx_levels_850, payload + 0x58, 5, 15);
			break;
		}
	}
}
