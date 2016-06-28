#pragma once

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/utils.h>

#define GSM0464_CIPH_MAX_BLOCK	1523

/* 3GPP TS 24.008 § 10.5.5.3 */
enum gprs_ciph_algo {
	GPRS_ALGO_GEA0 = 0,
	GPRS_ALGO_GEA1 = 1,
	GPRS_ALGO_GEA2 = 2,
	GPRS_ALGO_GEA3 = 3,
	GPRS_ALGO_GEA4 = 4,
	_GPRS_ALGO_NUM
};

/* 3GPP TS 04.64 Table A.1: */
enum gprs_cipher_direction {
	GPRS_CIPH_MS2SGSN = 0,
	GPRS_CIPH_SGSN2MS = 1,
};

extern const struct value_string gprs_cipher_names[];

/* An implementation of a GPRS cipher */
struct gprs_cipher_impl {
	struct llist_head list;
	enum gprs_ciph_algo algo;
	const char *name;
	unsigned int priority;

	/* As specified in 04.64 Annex A.  Uses Kc, IV and direction
	 * to generate the 1523 bytes cipher stream that need to be
	 * XORed wit the plaintext for encrypt / ciphertext for decrypt */
	int (*run)(uint8_t *out, uint16_t len, uint8_t *kc, uint32_t iv,
		   enum gprs_cipher_direction direction);
};

/* register a cipher with the core (from a plugin) */
int gprs_cipher_register(struct gprs_cipher_impl *ciph);

/* load all available GPRS cipher plugins */
int gprs_cipher_load(const char *path);

/* function to be called by core code */
int gprs_cipher_run(uint8_t *out, uint16_t len, enum gprs_ciph_algo algo,
		    uint8_t *kc, uint32_t iv, enum gprs_cipher_direction dir);

/* Do we have an implementation for this cipher? */
int gprs_cipher_supported(enum gprs_ciph_algo algo);

/* Return key length for supported cipher, in bytes */
unsigned gprs_cipher_key_length(enum gprs_ciph_algo algo);

/* GSM TS 04.64 / Section A.2.1 : Generation of 'input' */
uint32_t gprs_cipher_gen_input_ui(uint32_t iov_ui, uint8_t sapi, uint32_t lfn, uint32_t oc);

/* GSM TS 04.64 / Section A.2.1 : Generation of 'input' */
uint32_t gprs_cipher_gen_input_i(uint32_t iov_i, uint32_t lfn, uint32_t oc);
