/* TIFFS (TI Flash File System) reader implementation */

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

#include <tiffs.h>
#include "globals.h"

/* Each flash sector used for TIFFS begins with this 6-byte signature */
static const uint8_t ffs_sector_signature[6] = {
	'F', 'f', 's', '#', 0x10, 0x02
};

static int find_indexblk(void)
{
	uint32_t sector_addr;
	uint8_t *sector_ptr;
	unsigned i;

	sector_addr = tiffs_base_addr;
	for (i = 0; i < tiffs_nsectors; i++) {
		sector_ptr = (uint8_t *) sector_addr;
		if (!memcmp(sector_ptr, ffs_sector_signature, 6)
		    && sector_ptr[8] == 0xAB) {
			printf("Found TIFFS active index block "
				"at 0x%" PRIx32 "\n", sector_addr);
			tiffs_active_index = (struct tiffs_inode *) sector_addr;
			return 0;
		}
		sector_addr += tiffs_sector_size;
	}

	puts("TIFFS error: no active index block found\n");
	return -1;
}

static int find_rootino(void)
{
	struct tiffs_inode *irec;
	unsigned ino = 0;

	while (++ino < tiffs_sector_size >> 4) {
		irec = tiffs_active_index + ino;
		if (irec->type != TIFFS_OBJTYPE_DIR)
			continue;
		if (*INODE_TO_DATAPTR(irec) != '/')
			continue;

		printf("Found TIFFS root inode at #%x\n", ino);
		tiffs_root_ino = ino;
		return 0;
	}

	puts("TIFFS error: no root inode found\n");
	return -1;
}

int tiffs_init(uint32_t base_addr, uint32_t sector_size, unsigned nsectors)
{
	int rc;

	printf("Looking for TIFFS (TI Flash File System) header at "
		"0x%" PRIx32 ", %u sectors of 0x%" PRIx32 " bytes\n",
		base_addr, nsectors, sector_size);

	tiffs_base_addr = base_addr;
	tiffs_sector_size = sector_size;
	tiffs_nsectors = nsectors;

	rc = find_indexblk();
	if (rc < 0)
		return rc;
	rc = find_rootino();
	if (rc < 0)
		return rc;

	tiffs_init_done = 1;
	return 0;
}
