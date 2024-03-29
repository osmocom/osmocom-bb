#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <osmocom/core/endian.h>

#define OLD_TIME 2000

struct gprs_message {
	uint16_t arfcn;
	uint32_t fn;
	uint8_t tn;
	uint8_t rxl;
	uint8_t snr;
	uint8_t len;
	uint8_t msg[0];
};

struct gprs_lime {
#if OSMO_IS_LITTLE_ENDIAN
	uint8_t li:6,
		m:1,
		e:1;
	uint8_t used;
#elif OSMO_IS_BIG_ENDIAN
/* auto-generated from the little endian part above (libosmocore/contrib/struct_endianness.py) */
	uint8_t e:1, m:1, li:6;
	uint8_t used;
#endif
} __attribute__ ((packed));

struct gprs_frag {
	uint32_t fn;
	uint8_t last;
	uint8_t len;
	uint8_t data[53];
	uint8_t n_blocks;
	struct gprs_lime blocks[20];
} __attribute__ ((packed));

struct gprs_tbf {
	uint8_t last_bsn;
	uint8_t start_bsn;
	struct gprs_frag frags[128];
} __attribute__ ((packed));

void print_pkt(uint8_t *msg, size_t len);
void process_blocks(struct gprs_tbf *t, bool ul);
void rlc_data_handler(struct gprs_message *gm);
int rlc_type_handler(struct gprs_message *gm);
