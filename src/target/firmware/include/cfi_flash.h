
#ifndef _CFI_FLASH_H
#define _CFI_FLASH_H

#include <stdint.h>


#define CFI_FLASH_MAX_ERASE_REGIONS 4

/* structure of erase region descriptor */
struct cfi_region {
	uint16_t b_count;
	uint16_t b_size;
} __attribute__((packed));


/* structure of cfi query response */
struct cfi_query {
	uint8_t	qry[3];
	uint16_t	p_id;
	uint16_t	p_adr;
	uint16_t	a_id;
	uint16_t	a_adr;
	uint8_t	vcc_min;
	uint8_t	vcc_max;
	uint8_t	vpp_min;
	uint8_t	vpp_max;
	uint8_t	word_write_timeout_typ;
	uint8_t	buf_write_timeout_typ;
	uint8_t	block_erase_timeout_typ;
	uint8_t	chip_erase_timeout_typ;
	uint8_t	word_write_timeout_max;
	uint8_t	buf_write_timeout_max;
	uint8_t	block_erase_timeout_max;
	uint8_t chip_erase_timeout_max;
	uint8_t	dev_size;
	uint16_t	interface_desc;
	uint16_t	max_buf_write_size;
	uint8_t	num_erase_regions;
	struct cfi_region  erase_regions[CFI_FLASH_MAX_ERASE_REGIONS];
} __attribute__((packed));

typedef struct {
	void *f_base;

	uint32_t f_size;

	uint16_t f_manuf_id;
	uint16_t f_dev_id;

	struct cfi_query f_query;
} cfi_flash_t;

typedef uint8_t flash_lock;

void flash_init(cfi_flash_t *flash, void *base_addr);

void flash_dump_info(cfi_flash_t *flash);

flash_lock flash_block_getlock(cfi_flash_t *flash, uint32_t block_offset);

void flash_block_unlock(cfi_flash_t *flash, uint32_t block_offset);
void flash_block_lock(cfi_flash_t *flash, uint32_t block_offset);
void flash_block_lockdown(cfi_flash_t *flash, uint32_t block_offset);

void flash_block_erase(cfi_flash_t *flash, uint32_t block_addr);

void flash_program(cfi_flash_t *flash, uint32_t dst, void *src, uint32_t nbytes);

#endif
