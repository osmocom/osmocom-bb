/* Osmocom Authentication Protocol message encoder/decoder */

/* (C) 2015-2016 by sysmocom s.f.m.c. GmbH
 * All Rights Reserved
 *
 * Author: Neels Hofmeyr
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <stdint.h>
#include <osmocom/core/msgb.h>
#include <osmocom/gsm/protocol/gsm_04_08_gprs.h>

/*! \brief Information Element Identifiers for OAP IEs.
 * They match osmo_gsup_iei (so far). */
enum osmo_oap_iei {
	OAP_CAUSE_IE			= 0x02,
	OAP_RAND_IE			= 0x20,
	OAP_AUTN_IE			= 0x23,
	OAP_XRES_IE			= 0x24,
	OAP_AUTS_IE			= 0x25,
	OAP_CLIENT_ID_IE		= 0x30,
};

/*! \brief OAP message types */
enum osmo_oap_message_type {
	OAP_MSGT_REGISTER_REQUEST	= 0b00000100,
	OAP_MSGT_REGISTER_ERROR		= 0b00000101,
	OAP_MSGT_REGISTER_RESULT	= 0b00000110,

	OAP_MSGT_CHALLENGE_REQUEST	= 0b00001000,
	OAP_MSGT_CHALLENGE_ERROR	= 0b00001001,
	OAP_MSGT_CHALLENGE_RESULT	= 0b00001010,

	OAP_MSGT_SYNC_REQUEST		= 0b00001100,
	OAP_MSGT_SYNC_ERROR		= 0b00001101,
	OAP_MSGT_SYNC_RESULT		= 0b00001110,
};

/*! \brief Parsed/decoded OAP protocol message */
struct osmo_oap_message {
	enum osmo_oap_message_type message_type;
	enum gsm48_gmm_cause cause;
	uint16_t client_id;
	int rand_present;
	uint8_t rand[16];
	int autn_present;
	uint8_t autn[16];
	int xres_present;
	uint8_t xres[8];
	int auts_present;
	uint8_t auts[16];
};

int osmo_oap_decode(struct osmo_oap_message *oap_msg, const uint8_t *data,
		    size_t data_len);
void osmo_oap_encode(struct msgb *msg, const struct osmo_oap_message *oap_msg);
