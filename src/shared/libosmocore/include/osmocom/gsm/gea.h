/*
 * GEA3 header
 *
 * See gea.c for details
 */

#pragma once

#include <osmocom/crypt/gprs_cipher.h>

#include <stdint.h>

int gea3(uint8_t *out, uint16_t len, uint8_t *kc, uint32_t iv,
	 enum gprs_cipher_direction direct);

int gea4(uint8_t *out, uint16_t len, uint8_t *kc, uint32_t iv,
	 enum gprs_cipher_direction direct);
