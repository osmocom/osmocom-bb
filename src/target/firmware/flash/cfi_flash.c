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
#include <errno.h>
#include <memory.h>
#include <defines.h>
#include <flash/cfi_flash.h>

/* XXX: strings must always be in ram */
#if 0
#define puts(...)
#define printf(...)
#endif

/* global definitions */
#define CFI_FLASH_MAX_ERASE_REGIONS 4

/* structure of erase region descriptor */
struct cfi_region {
	uint16_t b_count;
	uint16_t b_size;
} __attribute__ ((packed));

/* structure of cfi query response */
struct cfi_query {
	uint8_t qry[3];
	uint16_t p_id;
	uint16_t p_adr;
	uint16_t a_id;
	uint16_t a_adr;
	uint8_t vcc_min;
	uint8_t vcc_max;
	uint8_t vpp_min;
	uint8_t vpp_max;
	uint8_t word_write_timeout_typ;
	uint8_t buf_write_timeout_typ;
	uint8_t block_erase_timeout_typ;
	uint8_t chip_erase_timeout_typ;
	uint8_t word_write_timeout_max;
	uint8_t buf_write_timeout_max;
	uint8_t block_erase_timeout_max;
	uint8_t chip_erase_timeout_max;
	uint8_t dev_size;
	uint16_t interface_desc;
	uint16_t max_buf_write_size;
	uint8_t num_erase_regions;
	struct cfi_region erase_regions[CFI_FLASH_MAX_ERASE_REGIONS];
} __attribute__ ((packed));

/* manufacturer ids */
enum cfi_manuf {
	CFI_MANUF_ST    = 0x0020,
	CFI_MANUF_INTEL = 0x0089,
};

/* algorithm ids */
enum cfi_algo {
	CFI_ALGO_INTEL_3 = 0x03
};

/* various command bytes */
enum cfi_flash_cmd {
	CFI_CMD_RESET = 0xff,
	CFI_CMD_READ_ID = 0x90,
	CFI_CMD_CFI = 0x98,
	CFI_CMD_READ_STATUS = 0x70,
	CFI_CMD_CLEAR_STATUS = 0x50,
	CFI_CMD_WRITE = 0x40,
	CFI_CMD_BLOCK_ERASE = 0x20,
	CFI_CMD_ERASE_CONFIRM = 0xD0,
	CFI_CMD_PROTECT = 0x60,
};

/* protection commands */
enum flash_prot_cmd {
	CFI_PROT_LOCK = 0x01,
	CFI_PROT_UNLOCK = 0xD0,
	CFI_PROT_LOCKDOWN = 0x2F
};

/* offsets from base */
enum flash_offset {
	CFI_OFFSET_MANUFACTURER_ID = 0x00,
	CFI_OFFSET_DEVICE_ID = 0x01,
	CFI_OFFSET_INTEL_PROTECTION = 0x81,
	CFI_OFFSET_CFI_RESP = 0x10
};

/* offsets from block base */
enum flash_block_offset {
	CFI_OFFSET_BLOCK_LOCKSTATE = 0x02
};

/* status masks */
enum flash_status {
	CFI_STATUS_READY = 0x80,
	CFI_STATUS_ERASE_SUSPENDED = 0x40,
	CFI_STATUS_ERASE_ERROR = 0x20,
	CFI_STATUS_PROGRAM_ERROR = 0x10,
	CFI_STATUS_VPP_LOW = 0x08,
	CFI_STATUS_PROGRAM_SUSPENDED = 0x04,
	CFI_STATUS_LOCKED_ERROR = 0x02,
	CFI_STATUS_RESERVED = 0x01
};

__ramtext
static inline void flash_write_cmd(const void *base_addr, uint16_t cmd)
{
	writew(cmd, base_addr);
}

__ramtext
static inline uint16_t flash_read16(const void *base_addr, uint32_t offset)
{
	return readw(base_addr + (offset << 1));
}

__ramtext
static char flash_protected(uint32_t block_offset)
{
#ifdef CONFIG_FLASH_WRITE
#  ifdef CONFIG_FLASH_WRITE_LOADER
	return 0;
#  else
	return block_offset <= 0xFFFF;
#  endif
#else
	return 1;
#endif
}

__ramtext
flash_lock_t flash_block_getlock(flash_t * flash, uint32_t block_offset)
{
	const void *base_addr = flash->f_base;

	uint8_t lockstate;
	flash_write_cmd(base_addr, CFI_CMD_READ_ID);
	lockstate =
		flash_read16(base_addr,
			     (block_offset >> 1) + CFI_OFFSET_BLOCK_LOCKSTATE);
	flash_write_cmd(base_addr, CFI_CMD_RESET);

	if (lockstate & 0x2) {
		return FLASH_LOCKED_DOWN;
	} else if (lockstate & 0x01) {
		return FLASH_LOCKED;
	} else {
		return FLASH_UNLOCKED;
	}
}

__ramtext
int flash_block_unlock(flash_t * flash, uint32_t block_offset)
{
	const void *base_addr = flash->f_base;

	if (block_offset >= flash->f_size) {
		return -EINVAL;
	}

	if (flash_protected(block_offset)) {
		return -EPERM;
	}

	printf("Unlocking block at 0x%08x, meaning %08x\n",
		   block_offset, base_addr + block_offset);

	flash_write_cmd(base_addr, CFI_CMD_PROTECT);
	flash_write_cmd(base_addr + block_offset, CFI_PROT_UNLOCK);
	flash_write_cmd(base_addr, CFI_CMD_RESET);

	return 0;
}

__ramtext
int flash_block_lock(flash_t * flash, uint32_t block_offset)
{
	const void *base_addr = flash->f_base;

	if (block_offset >= flash->f_size) {
		return -EINVAL;
	}

	printf("Locking block at 0x%08x\n", block_offset);

	flash_write_cmd(base_addr, CFI_CMD_PROTECT);
	flash_write_cmd(base_addr + block_offset, CFI_PROT_LOCK);
	flash_write_cmd(base_addr, CFI_CMD_RESET);

	return 0;
}

__ramtext
int flash_block_lockdown(flash_t * flash, uint32_t block_offset)
{
	const void *base_addr = flash->f_base;

	if (block_offset >= flash->f_size) {
		return -EINVAL;
	}

	printf("Locking down block at 0x%08x\n", block_offset);

	flash_write_cmd(base_addr, CFI_CMD_PROTECT);
	flash_write_cmd(base_addr + block_offset, CFI_PROT_LOCKDOWN);
	flash_write_cmd(base_addr, CFI_CMD_RESET);

	return 0;
}

__ramtext
int flash_block_erase(flash_t * flash, uint32_t block_offset)
{
	const void *base_addr = flash->f_base;

	if (block_offset >= flash->f_size) {
		return -EINVAL;
	}

	if (flash_protected(block_offset)) {
		return -EPERM;
	}

	printf("Erasing block 0x%08x...", block_offset);

	void *block_addr = ((uint8_t *) base_addr) + block_offset;

	flash_write_cmd(base_addr, CFI_CMD_CLEAR_STATUS);

	flash_write_cmd(block_addr, CFI_CMD_BLOCK_ERASE);
	flash_write_cmd(block_addr, CFI_CMD_ERASE_CONFIRM);

	flash_write_cmd(base_addr, CFI_CMD_READ_STATUS);
	uint16_t status;
	do {
		status = flash_read16(base_addr, 0);
	} while (!(status & CFI_STATUS_READY));

	int res = 0;
	if (status & CFI_STATUS_ERASE_ERROR) {
		puts("error: ");
		if (status & CFI_STATUS_VPP_LOW) {
			puts("vpp insufficient\n");
			res = -EFAULT;
		} else if (status & CFI_STATUS_LOCKED_ERROR) {
			puts("block is lock-protected\n");
			res = -EPERM;
		} else {
			puts("unknown fault\n");
			res = -EFAULT;
		}
	} else {
		puts("done\n");
	}

	flash_write_cmd(base_addr, CFI_CMD_RESET);

	return res;

}

__ramtext
int flash_program(flash_t * flash, uint32_t dst, void *src, uint32_t nbytes)
{
	const void *base_addr = flash->f_base;
	int res = 0;
	uint32_t i;

	/* check destination bounds */
	if (dst >= flash->f_size) {
		return -EINVAL;
	}
	if (dst + nbytes > flash->f_size) {
		return -EINVAL;
	}

	/* check alignments */
	if (((uint32_t) src) % 2) {
		return -EINVAL;
	}
	if (dst % 2) {
		return -EINVAL;
	}
	if (nbytes % 2) {
		return -EINVAL;
	}

	/* check permissions */
	if (flash_protected(dst)) {
		return -EPERM;
	}

	/* say something */
	printf("Programming %u bytes to 0x%08x from 0x%p...", nbytes, dst, src);

	/* clear status register */
	flash_write_cmd(base_addr, CFI_CMD_CLEAR_STATUS);

	/* write the words */
	puts("writing...");
	for (i = 0; i < nbytes; i += 2) {
		uint16_t *src_addr = (uint16_t *) (src + i);
		uint16_t *dst_addr = (uint16_t *) (base_addr + dst + i);

		uint16_t data = *src_addr;

		flash_write_cmd(dst_addr, CFI_CMD_WRITE);
		flash_write_cmd(dst_addr, data);

		flash_write_cmd(base_addr, CFI_CMD_READ_STATUS);
		uint16_t status;
		do {
			status = flash_read16(base_addr, 0);
		} while (!(status & CFI_STATUS_READY));

		if (status & CFI_STATUS_PROGRAM_ERROR) {
			puts("error: ");
			if (status & CFI_STATUS_VPP_LOW) {
				puts("vpp insufficient");
				res = -EFAULT;
			} else if (status & CFI_STATUS_LOCKED_ERROR) {
				puts("block is lock-protected");
				res = -EPERM;
			} else {
				puts("unknown fault");
				res = -EFAULT;
			}
			goto err_reset;
		}
	}

	flash_write_cmd(base_addr, CFI_CMD_RESET);

	/* verify the result */
	puts("verifying...");
	for (i = 0; i < nbytes; i += 2) {
		uint16_t *src_addr = (uint16_t *) (src + i);
		uint16_t *dst_addr = (uint16_t *) (base_addr + dst + i);
		if (*src_addr != *dst_addr) {
			puts("error: verification failed");
			res = -EFAULT;
			goto err;
		}
	}

	puts("done\n");

	return res;

 err_reset:
	flash_write_cmd(base_addr, CFI_CMD_RESET);

 err:
	printf(" at offset 0x%x\n", i);

	return res;
}

/* Internal: retrieve manufacturer and device id from id space */
__ramtext
static int get_id(void *base_addr,
		  uint16_t * manufacturer_id, uint16_t * device_id)
{
	flash_write_cmd(base_addr, CFI_CMD_READ_ID);

	*manufacturer_id = flash_read16(base_addr, CFI_OFFSET_MANUFACTURER_ID);
	*device_id = flash_read16(base_addr, CFI_OFFSET_DEVICE_ID);

	flash_write_cmd(base_addr, CFI_CMD_RESET);

	return 0;
}

/* Internal: retrieve cfi query response data */
__ramtext
static int get_query(void *base_addr, struct cfi_query *query)
{
	int res = 0;
	int i;

	flash_write_cmd(base_addr, CFI_CMD_CFI);

	for (i = 0; i < sizeof(struct cfi_query); i++) {
		uint16_t byte =
			flash_read16(base_addr, CFI_OFFSET_CFI_RESP + i);
		*(((volatile unsigned char *)query) + i) = byte;
	}

	if (query->qry[0] != 'Q' || query->qry[1] != 'R' || query->qry[2] != 'Y') {
		res = -ENOENT;
	}

	flash_write_cmd(base_addr, CFI_CMD_RESET);

	return res;
}

#if 0

/* Internal: retrieve intel protection data */
__ramtext
static int get_intel_protection(void *base_addr,
				uint16_t * lockp, uint8_t protp[8])
{
	int i;

	/* check args */
	if (!lockp) {
		return -EINVAL;
	}
	if (!protp) {
		return -EINVAL;
	}

	/* enter read id mode */
	flash_write_cmd(base_addr, CFI_CMD_READ_ID);

	/* get lock */
	*lockp = flash_read16(base_addr, CFI_OFFSET_INTEL_PROTECTION);

	/* get data */
	for (i = 0; i < 8; i++) {
		protp[i] = flash_read16(base_addr, CFI_OFFSET_INTEL_PROTECTION + 1 + i);
	}

	/* leave read id mode */
	flash_write_cmd(base_addr, CFI_CMD_RESET);

	return 0;
}

static void dump_intel_protection(uint16_t lock, uint8_t data[8])
{
	printf
		("  protection lock 0x%4.4x data 0x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x\n",
		 lock, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
}

static void dump_query_algorithms(struct cfi_query *qry)
{
	printf("  primary algorithm 0x%4.4x\n", qry->p_id);
	printf("  primary extended query 0x%4.4x\n", qry->p_adr);
	printf("  alternate algorithm 0x%4.4x\n", qry->a_id);
	printf("  alternate extended query 0x%4.4x\n", qry->a_adr);
}

static void dump_query_timing(struct cfi_query *qry)
{
	uint32_t block_erase_typ = 1 << qry->block_erase_timeout_typ;
	uint32_t block_erase_max =
		(1 << qry->block_erase_timeout_max) * block_erase_typ;
	uint32_t word_program_typ = 1 << qry->word_write_timeout_typ;
	uint32_t word_program_max =
		(1 << qry->word_write_timeout_max) * word_program_typ;
	printf("  block erase typ %u ms\n", block_erase_typ);
	printf("  block erase max %u ms\n", block_erase_max);
	printf("  word program typ %u us\n", word_program_typ);
	printf("  word program max %u us\n", word_program_max);
}

void flash_dump_info(flash_t * flash)
{
	int i;
	printf("flash at 0x%p of %d bytes with %d regions\n",
		   flash->f_base, flash->f_size, flash->f_nregions);

	uint16_t m_id, d_id;
	if (get_id(flash->f_base, &m_id, &d_id)) {
		puts("  failed to get id\n");
	} else {
		printf("  manufacturer 0x%4.4x device 0x%4.4x\n", m_id, d_id);
	}

	uint16_t plock;
	uint8_t pdata[8];
	if (get_intel_protection(flash->f_base, &plock, pdata)) {
		puts("  failed to get protection data\n");
	} else {
		dump_intel_protection(plock, pdata);
	}

	struct cfi_query qry;
	if (get_query(flash->f_base, &qry)) {
		puts("  failed to get cfi query response\n");
	} else {
		dump_query_algorithms(&qry);
		dump_query_timing(&qry);
	}

	for (i = 0; i < flash->f_nregions; i++) {
		flash_region_t *fr = &flash->f_regions[i];
		printf("  region %d: %d blocks of %d bytes at 0x%p\n",
			   i, fr->fr_bnum, fr->fr_bsize, fr->fr_base);
	}
}

#endif

__ramtext
int flash_init(flash_t * flash, void *base_addr)
{
	int res;
	unsigned u;
	uint16_t m_id, d_id;
	uint32_t base;
	struct cfi_query qry;

	/* retrieve and check manufacturer and device id */
	res = get_id(base_addr, &m_id, &d_id);
	if (res) {
		return res;
	}
	if (m_id != CFI_MANUF_INTEL && m_id != CFI_MANUF_ST) {
		return -ENOTSUP;
	}

	/* retrieve and check query response */
	res = get_query(base_addr, &qry);
	if (res) {
		return res;
	}
	if (qry.p_id != CFI_ALGO_INTEL_3) {
		/* we only support algo 3 */
		return -ENOTSUP;
	}
	if (qry.num_erase_regions > FLASH_MAX_REGIONS) {
		/* we have a hard limit on the number of regions */
		return -ENOTSUP;
	}

	/* fill in basic information */
	flash->f_base = base_addr;
	flash->f_size = 1 << qry.dev_size;

	/* determine number of erase regions */
	flash->f_nregions = qry.num_erase_regions;

	/* compute actual erase region info from cfi junk */
	base = 0;
	for (u = 0; u < flash->f_nregions; u++) {
		flash_region_t *fr = &flash->f_regions[u];

		fr->fr_base = (void *)base;
		fr->fr_bnum = qry.erase_regions[u].b_count + 1;
		fr->fr_bsize = qry.erase_regions[u].b_size * 256;

		base += fr->fr_bnum * fr->fr_bsize;
	}

	return 0;
}
