/*
 * gea.c
 *
 * Implementation of GEA3 and GEA4
 *
 * Copyright (C) 2016 by Sysmocom s.f.m.c. GmbH
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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
 */

#include <osmocom/core/bits.h>
#include <osmocom/crypt/gprs_cipher.h>
#include <osmocom/crypt/auth.h>
#include <osmocom/gsm/kasumi.h>

#include <stdint.h>
#include <string.h>

/*! \brief Performs the GEA4 algorithm as in 3GPP TS 55.226 V9.0.0
 *  \param[in,out] out Buffer for gamma for encrypted/decrypted
 *  \param[in] len Length of out, in bytes
 *  \param[in] kc Buffer with the ciphering key
 *  \param[in] iv Init vector
 *  \param[in] direct Direction: 0 (MS -> SGSN) or 1 (SGSN -> MS)
 */
int gea4(uint8_t *out, uint16_t len, uint8_t *kc, uint32_t iv,
	 enum gprs_cipher_direction direction)
{
	_kasumi_kgcore(0xFF, 0, iv, direction, kc, out, len * 8);
	return 0;
}

/*! \brief Performs the GEA3 algorithm as in 3GPP TS 55.216 V6.2.0
 *  \param[in,out] out Buffer for gamma for encrypted/decrypted
 *  \param[in] len Length of out, in bytes
 *  \param[in] kc Buffer with the ciphering key
 *  \param[in] iv Init vector
 *  \param[in] direct Direction: 0 (MS -> SGSN) or 1 (SGSN -> MS)
 */
int gea3(uint8_t *out, uint16_t len, uint8_t *kc, uint32_t iv,
	 enum gprs_cipher_direction direction)
{
	uint8_t ck[gprs_cipher_key_length(GPRS_ALGO_GEA4)];
	osmo_c4(ck, kc);
	return gea4(out, len, ck, iv, direction);
}
