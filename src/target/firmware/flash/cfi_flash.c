/* NOR Flash Driver for Intel 28F160C3 NOR flash */

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

#include <debug.h>
#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <cfi_flash.h>

/* XXX: memdump_range() */
#include <calypso/misc.h>

enum flash_cmd {
	FLASH_CMD_RESET		= 0xff,
	FLASH_CMD_READ_ID	= 0x90,
	FLASH_CMD_CFI		= 0x98,
	FLASH_CMD_READ_STATUS	= 0x70,
	FLASH_CMD_CLEAR_STATUS	= 0x50,
	FLASH_CMD_WRITE		= 0x40,
	FLASH_CMD_BLOCK_ERASE	= 0x20,
	FLASH_CMD_ERASE_CONFIRM = 0xD0,
	FLASH_CMD_PROTECT = 0x60,
};

enum flash_prot_cmd {
	FLASH_PROT_LOCK = 0x01,
	FLASH_PROT_UNLOCK = 0xD0,
	FLASH_PROT_LOCKDOWN = 0x2F
};

enum flash_offset {
	FLASH_OFFSET_MANUFACTURER_ID	= 0x00,
	FLASH_OFFSET_DEVICE_ID		= 0x01,
	FLASH_OFFSET_INTEL_PROTECTION	= 0x81,
	FLASH_OFFSET_CFI_RESP		= 0x10
};

enum flash_block_offset {
	FLASH_OFFSET_BLOCK_LOCKSTATE = 0x02
};

enum flash_status {
	FLASH_STATUS_READY = 0x80,
	FLASH_STATUS_ERASE_SUSPENDED = 0x40,
	FLASH_STATUS_ERASE_ERROR = 0x20,
	FLASH_STATUS_PROGRAM_ERROR = 0x10,
	FLASH_STATUS_VPP_LOW = 0x08,
	FLASH_STATUS_PROGRAM_SUSPENDED = 0x04,
	FLASH_STATUS_LOCKED_ERROR = 0x02,
	FLASH_STATUS_RESERVED = 0x01
};

static inline void flash_write_cmd(const void *base_addr, uint16_t cmd)
{
	writew(cmd, base_addr);
}

static inline uint16_t flash_read16(const void *base_addr, uint32_t offset)
{
	return readw(base_addr + (offset << 1));
}

static char flash_protected(uint32_t block_offset) {
	return block_offset < 64*1024;
}

uint8_t flash_block_getlock(cfi_flash_t *flash, uint32_t block_offset) {
	const void *base_addr = flash->f_base;
	uint8_t lockstate;
	flash_write_cmd(base_addr, FLASH_CMD_READ_ID);
	lockstate = flash_read16(base_addr, block_offset + FLASH_OFFSET_BLOCK_LOCKSTATE);
	flash_write_cmd(base_addr, FLASH_CMD_RESET);
	return lockstate;
}

void flash_block_unlock(cfi_flash_t *flash, uint32_t block_offset) {
	const void *base_addr = flash->f_base;
	printf("Unlocking block at 0x%08x\n", block_offset);

	if(flash_protected(block_offset)) {
		puts("error: block is soft-protected\n");
		return;
	}

	flash_write_cmd(base_addr, FLASH_CMD_PROTECT);
	flash_write_cmd(base_addr + block_offset, FLASH_PROT_UNLOCK);
	flash_write_cmd(base_addr, FLASH_CMD_RESET);
}

void flash_block_lock(cfi_flash_t *flash, uint32_t block_offset) {
	const void *base_addr = flash->f_base;
	printf("Locking block at 0x%08x\n", block_offset);
	flash_write_cmd(base_addr, FLASH_CMD_PROTECT);
	flash_write_cmd(base_addr + block_offset, FLASH_PROT_LOCK);
	flash_write_cmd(base_addr, FLASH_CMD_RESET);
}

void flash_block_lockdown(cfi_flash_t *flash, uint32_t block_offset) {
	const void *base_addr = flash->f_base;
	printf("Locking down block at 0x%08x\n", block_offset);
	flash_write_cmd(base_addr, FLASH_CMD_PROTECT);
	flash_write_cmd(base_addr + block_offset, FLASH_PROT_LOCKDOWN);
	flash_write_cmd(base_addr, FLASH_CMD_RESET);
}

void flash_block_erase(cfi_flash_t *flash, uint32_t block_offset) {
	const void *base_addr = flash->f_base;
	printf("Erasing block 0x%08x...", block_offset);

	if(flash_protected(block_offset)) {
		puts("error: block is soft-protected\n");
		return;
	}

	void *block_addr = ((uint8_t*)base_addr) + block_offset;

	flash_write_cmd(base_addr, FLASH_CMD_CLEAR_STATUS);

	flash_write_cmd(block_addr, FLASH_CMD_BLOCK_ERASE);
	flash_write_cmd(block_addr, FLASH_CMD_ERASE_CONFIRM);

	flash_write_cmd(base_addr, FLASH_CMD_READ_STATUS);
	uint16_t status;
	do {
		status = flash_read16(base_addr, 0);
	} while(!(status&FLASH_STATUS_READY));

	if(status&FLASH_STATUS_ERASE_ERROR) {
		puts("error: ");
		if(status&FLASH_STATUS_VPP_LOW) {
			puts("vpp insufficient\n");
		}
		if(status&FLASH_STATUS_LOCKED_ERROR) {
			puts("block is lock-protected\n");
		}
	} else {
		puts("done\n");
	}

	flash_write_cmd(base_addr, FLASH_CMD_RESET);
}

void flash_program(cfi_flash_t *flash, uint32_t dst, void *src, uint32_t nbytes) {
	const void *base_addr = flash->f_base;
	uint32_t i;

	printf("Programming %u bytes to 0x%08x from 0x%p...", nbytes, dst, src);

	if(dst%2) {
		puts("error: unaligned destination\n");
		return;
	}

	if(nbytes%2) {
		puts("error: unaligned count\n");
		return;
	}

	if(flash_protected(dst)) {
		puts("error: block is soft-protected\n");
		return;
	}

	flash_write_cmd(base_addr, FLASH_CMD_CLEAR_STATUS);

	puts("writing...");
	for(i = 0; i < nbytes; i += 2) {
		uint16_t *src_addr = (uint16_t*)(src + i);
		uint16_t *dst_addr = (uint16_t*)(base_addr + dst + i);

		uint16_t data = *src_addr;

		flash_write_cmd(dst_addr, FLASH_CMD_WRITE);
		flash_write_cmd(dst_addr, data);

		flash_write_cmd(base_addr, FLASH_CMD_READ_STATUS);
		uint16_t status;
		do {
			status = flash_read16(base_addr, 0);
		} while(!(status&FLASH_STATUS_READY));

		if(status&FLASH_STATUS_PROGRAM_ERROR) {
			puts("error: ");
			if(status&FLASH_STATUS_VPP_LOW) {
				puts("vpp insufficient");
			}
			if(status&FLASH_STATUS_LOCKED_ERROR) {
				puts("block is lock-protected");
			}
			goto err_reset;
		}
	}

	flash_write_cmd(base_addr, FLASH_CMD_RESET);

	puts("verifying...");
	for(i = 0; i < nbytes; i += 2) {
		uint16_t *src_addr = (uint16_t*)(src + i);
		uint16_t *dst_addr = (uint16_t*)(base_addr + dst + i);
		if(*src_addr != *dst_addr) {
			puts("error: verification failed");
			goto err;
		}
	}

	puts("done\n");

	return;

 err_reset:
	flash_write_cmd(base_addr, FLASH_CMD_RESET);

 err:
	printf(" at offset 0x%x\n", i);
}

typedef void (*flash_block_cb_t)(cfi_flash_t *flash,
								 uint32_t block_offset,
								 uint32_t block_size);

void flash_iterate_blocks(cfi_flash_t *flash, struct cfi_query *qry,
						  uint32_t start_offset, uint32_t end_offset,
						  flash_block_cb_t callback)
{
	int region, block;

	uint32_t block_start = 0;
	for(region = 0; region < qry->num_erase_regions; region++) {
		uint16_t actual_count = qry->erase_regions[region].b_count + 1;
		uint32_t actual_size = qry->erase_regions[region].b_size * 256;
		for(block = 0; block < actual_count; block++) {
			uint32_t block_end = block_start + actual_size;
			if(block_start >= start_offset && block_end-1 <= end_offset) {
				callback(flash, block_start, actual_size);
			}
			block_start = block_end;
		}
	}
}

static void get_id(void *base_addr, uint16_t *manufacturer_id, uint16_t *device_id) {
	flash_write_cmd(base_addr, FLASH_CMD_READ_ID);

	*manufacturer_id = flash_read16(base_addr, FLASH_OFFSET_MANUFACTURER_ID);
	*device_id = flash_read16(base_addr, FLASH_OFFSET_DEVICE_ID);

	flash_write_cmd(base_addr, FLASH_CMD_RESET);
}

static void get_query(void *base_addr, struct cfi_query *query) {
	unsigned int i;

	flash_write_cmd(base_addr, FLASH_CMD_CFI);

	for(i = 0; i < sizeof(struct cfi_query); i++) {
		uint16_t byte = flash_read16(base_addr, FLASH_OFFSET_CFI_RESP+i);
		*(((unsigned char*)query)+i) = byte;
	}

	if(query->qry[0] != 'Q' || query->qry[1] != 'R' || query->qry[2] != 'Y') {
		puts("Error: CFI query signature not found\n");
	}

	flash_write_cmd(base_addr, FLASH_CMD_RESET);
}

static void dump_query(void *base_addr, struct cfi_query *query) {
	unsigned int i;

	flash_write_cmd(base_addr, FLASH_CMD_CFI);

	for(i = 0; i < sizeof(struct cfi_query); i++) {
		uint8_t byte = *(((uint8_t*)query)+i);
		printf("%04X: %02X\n", FLASH_OFFSET_CFI_RESP+i, byte);
	}

	flash_write_cmd(base_addr, FLASH_CMD_RESET);
}

static void dump_layout(void *base_addr, const struct cfi_query *qry) {
	int region;

	flash_write_cmd(base_addr, FLASH_CMD_READ_ID);
	for(region = 0; region < qry->num_erase_regions; region++) {
		uint16_t actual_count = qry->erase_regions[region].b_count + 1;
		uint32_t actual_size = qry->erase_regions[region].b_size * 256;
		printf("Region of 0x%04x times 0x%6x bytes\n", actual_count,
			actual_size);
	}
	flash_write_cmd(base_addr, FLASH_CMD_RESET);
}

static void dump_locks(void *base_addr, const struct cfi_query *qry) {
	int region, block;

	uint32_t block_addr = 0;
	flash_write_cmd(base_addr, FLASH_CMD_READ_ID);
	for(region = 0; region < qry->num_erase_regions; region++) {
		uint16_t actual_count = qry->erase_regions[region].b_count + 1;
		uint32_t actual_size = qry->erase_regions[region].b_size * 256;
		for(block = 0; block < actual_count; block++) {
			uint8_t lock = flash_read16(base_addr, block_addr+2);
			printf("Block 0x%08x lock 0x%02x\n", block_addr*2, lock);
			block_addr += actual_size / 2;
		}
	}
	flash_write_cmd(base_addr, FLASH_CMD_RESET);
}

static void dump_protection(void *base_addr) {
	flash_write_cmd(base_addr, FLASH_CMD_READ_ID);

	uint16_t lock = flash_read16(base_addr, FLASH_OFFSET_INTEL_PROTECTION);
	printf("Protection Lock: 0x%04x\n", lock);

	puts("Protection Data: ");
	int i;
	for(i = 0; i < 8; i++) {
		printf("%04x", flash_read16(base_addr, FLASH_OFFSET_INTEL_PROTECTION + 1 + i));
	}
	putchar('\n');

	flash_write_cmd(base_addr, FLASH_CMD_RESET);
}

static void dump_timing(void *base_addr, struct cfi_query *qry) {
	uint32_t block_erase_typ = 1<<qry->block_erase_timeout_typ;
	uint32_t block_erase_max = (1<<qry->block_erase_timeout_max) * block_erase_typ;
	uint32_t word_program_typ = 1<<qry->word_write_timeout_typ;
	uint32_t word_program_max = (1<<qry->word_write_timeout_max) * word_program_typ;
	printf("Block Erase Typical: %u ms\n", block_erase_typ);
	printf("Block Erase Maximum: %u ms\n", block_erase_max);
	printf("Word Program Typical: %u us\n", word_program_typ);
	printf("Word Program Maximum: %u us\n", word_program_max);
}

static void dump_algorithms(void *base_addr, struct cfi_query *qry) {
	printf("Primary Algorithm ID: %04x\n", qry->p_id);
	printf("Primary Extended Query: %04x\n", qry->p_adr);

	printf("Alternate Algorithm ID: %04x\n", qry->a_id);
	printf("Alternate Extended Query: %04x\n", qry->a_adr);
}

void
lockdown_block_cb(cfi_flash_t *flash,
				  uint32_t block_offset,
				  uint32_t block_size)
{
	flash_block_lockdown(flash, block_offset);
}

void
print_block_cb(cfi_flash_t *flash,
			   uint32_t block_offset,
			   uint32_t block_size)
{
	printf("%08x size %08x\n", block_offset, block_size);
}

void flash_dump_info(cfi_flash_t *flash) {
	void *base_addr = flash->f_base;
	struct cfi_query *qry = &flash->f_query;

	printf("Flash Manufacturer ID: %04x\n", flash->f_manuf_id);
	printf("Flash Device ID: %04x\n", flash->f_dev_id);

	printf("Flash Size: 0x%08x bytes\n", flash->f_size);

	dump_algorithms(base_addr, qry);

	dump_timing(base_addr, qry);

	dump_protection(base_addr);

	dump_layout(base_addr, qry);

	dump_locks(base_addr, qry);
}

void flash_init(cfi_flash_t *flash, void *base_addr) {
	printd("Initializing CFI flash at 0x%p\n", base_addr);

	flash->f_base = base_addr;

	get_id(base_addr, &flash->f_manuf_id, &flash->f_dev_id);

	get_query(base_addr, &flash->f_query);

	flash->f_size = 1<<flash->f_query.dev_size;
}

void flash_test() {
	/* block iterator test */
#if 0
	flash_iterate_blocks(flash, qry, 0x0000, 0xFFFF, &lockdown_block_cb);
#endif

	/* programming test */
#if 0
	static uint8_t magic[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xDE, 0xAD, 0xBE, 0xEF};

	memdump_range(&magic, sizeof(magic));

#if 0
#define ADDR 0x001E0000
	flash_block_unlock(flash, ADDR);
	memdump_range(ADDR, 16);
	flash_block_erase(flash, ADDR);
	memdump_range(ADDR, 16);
	flash_program(flash, ADDR, &magic, sizeof(magic));
	memdump_range(ADDR, 16);
#undef ADDR
#endif

#endif
}
