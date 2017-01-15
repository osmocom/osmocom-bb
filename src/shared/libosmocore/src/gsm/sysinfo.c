/* GSM 04.08 System Information (SI) encoding and decoding
 * 3GPP TS 04.08 version 7.21.0 Release 1998 / ETSI TS 100 940 V7.21.0 */

/* (C) 2008-2010 by Harald Welte <laforge@gnumonks.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <osmocom/core/bitvec.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/sysinfo.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

/* verify the sizes of the system information type structs */

/* rest octets are not part of the struct */
osmo_static_assert(sizeof(struct gsm48_system_information_type_header) == 3, _si_header_size);
osmo_static_assert(sizeof(struct gsm48_rach_control) == 3, _si_rach_control);
osmo_static_assert(sizeof(struct gsm48_system_information_type_1) == 22, _si1_size);
osmo_static_assert(sizeof(struct gsm48_system_information_type_2) == 23, _si2_size);
osmo_static_assert(sizeof(struct gsm48_system_information_type_3) == 19, _si3_size);
osmo_static_assert(sizeof(struct gsm48_system_information_type_4) == 13, _si4_size);

/* bs11 forgot the l2 len, 0-6 rest octets */
osmo_static_assert(sizeof(struct gsm48_system_information_type_5) == 18, _si5_size);
osmo_static_assert(sizeof(struct gsm48_system_information_type_6) == 11, _si6_size);

osmo_static_assert(sizeof(struct gsm48_system_information_type_13) == 3, _si13_size);

static const uint8_t sitype2rsl[_MAX_SYSINFO_TYPE] = {
	[SYSINFO_TYPE_1]	= RSL_SYSTEM_INFO_1,
	[SYSINFO_TYPE_2]	= RSL_SYSTEM_INFO_2,
	[SYSINFO_TYPE_3]	= RSL_SYSTEM_INFO_3,
	[SYSINFO_TYPE_4]	= RSL_SYSTEM_INFO_4,
	[SYSINFO_TYPE_5]	= RSL_SYSTEM_INFO_5,
	[SYSINFO_TYPE_6]	= RSL_SYSTEM_INFO_6,
	[SYSINFO_TYPE_7]	= RSL_SYSTEM_INFO_7,
	[SYSINFO_TYPE_8]	= RSL_SYSTEM_INFO_8,
	[SYSINFO_TYPE_9]	= RSL_SYSTEM_INFO_9,
	[SYSINFO_TYPE_10]	= RSL_SYSTEM_INFO_10,
	[SYSINFO_TYPE_13]	= RSL_SYSTEM_INFO_13,
	[SYSINFO_TYPE_16]	= RSL_SYSTEM_INFO_16,
	[SYSINFO_TYPE_17]	= RSL_SYSTEM_INFO_17,
	[SYSINFO_TYPE_18]	= RSL_SYSTEM_INFO_18,
	[SYSINFO_TYPE_19]	= RSL_SYSTEM_INFO_19,
	[SYSINFO_TYPE_20]	= RSL_SYSTEM_INFO_20,
	[SYSINFO_TYPE_2bis]	= RSL_SYSTEM_INFO_2bis,
	[SYSINFO_TYPE_2ter]	= RSL_SYSTEM_INFO_2ter,
	[SYSINFO_TYPE_2quater]	= RSL_SYSTEM_INFO_2quater,
	[SYSINFO_TYPE_5bis]	= RSL_SYSTEM_INFO_5bis,
	[SYSINFO_TYPE_5ter]	= RSL_SYSTEM_INFO_5ter,
	[SYSINFO_TYPE_EMO]	= RSL_EXT_MEAS_ORDER,
	[SYSINFO_TYPE_MEAS_INFO]= RSL_MEAS_INFO,
};

static const uint8_t rsl2sitype[256] = {
	[RSL_SYSTEM_INFO_1] = SYSINFO_TYPE_1,
	[RSL_SYSTEM_INFO_2] = SYSINFO_TYPE_2,
	[RSL_SYSTEM_INFO_3] = SYSINFO_TYPE_3,
	[RSL_SYSTEM_INFO_4] = SYSINFO_TYPE_4,
	[RSL_SYSTEM_INFO_5] = SYSINFO_TYPE_5,
	[RSL_SYSTEM_INFO_6] = SYSINFO_TYPE_6,
	[RSL_SYSTEM_INFO_7] = SYSINFO_TYPE_7,
	[RSL_SYSTEM_INFO_8] = SYSINFO_TYPE_8,
	[RSL_SYSTEM_INFO_9] = SYSINFO_TYPE_9,
	[RSL_SYSTEM_INFO_10] = SYSINFO_TYPE_10,
	[RSL_SYSTEM_INFO_13] = SYSINFO_TYPE_13,
	[RSL_SYSTEM_INFO_16] = SYSINFO_TYPE_16,
	[RSL_SYSTEM_INFO_17] = SYSINFO_TYPE_17,
	[RSL_SYSTEM_INFO_18] = SYSINFO_TYPE_18,
	[RSL_SYSTEM_INFO_19] = SYSINFO_TYPE_19,
	[RSL_SYSTEM_INFO_20] = SYSINFO_TYPE_20,
	[RSL_SYSTEM_INFO_2bis] = SYSINFO_TYPE_2bis,
	[RSL_SYSTEM_INFO_2ter] = SYSINFO_TYPE_2ter,
	[RSL_SYSTEM_INFO_2quater] = SYSINFO_TYPE_2quater,
	[RSL_SYSTEM_INFO_5bis] = SYSINFO_TYPE_5bis,
	[RSL_SYSTEM_INFO_5ter] = SYSINFO_TYPE_5ter,
	[RSL_EXT_MEAS_ORDER] = SYSINFO_TYPE_EMO,
	[RSL_MEAS_INFO] = SYSINFO_TYPE_MEAS_INFO,
};

const struct value_string osmo_sitype_strs[_MAX_SYSINFO_TYPE] = {
	{ SYSINFO_TYPE_1,	"1" },
	{ SYSINFO_TYPE_2,	"2" },
	{ SYSINFO_TYPE_3,	"3" },
	{ SYSINFO_TYPE_4,	"4" },
	{ SYSINFO_TYPE_5,	"5" },
	{ SYSINFO_TYPE_6,	"6" },
	{ SYSINFO_TYPE_7,	"7" },
	{ SYSINFO_TYPE_8,	"8" },
	{ SYSINFO_TYPE_9,	"9" },
	{ SYSINFO_TYPE_10,	"10" },
	{ SYSINFO_TYPE_13,	"13" },
	{ SYSINFO_TYPE_16,	"16" },
	{ SYSINFO_TYPE_17,	"17" },
	{ SYSINFO_TYPE_18,	"18" },
	{ SYSINFO_TYPE_19,	"19" },
	{ SYSINFO_TYPE_20,	"20" },
	{ SYSINFO_TYPE_2bis,	"2bis" },
	{ SYSINFO_TYPE_2ter,	"2ter" },
	{ SYSINFO_TYPE_2quater,	"2quater" },
	{ SYSINFO_TYPE_5bis,	"5bis" },
	{ SYSINFO_TYPE_5ter,	"5ter" },
	{ SYSINFO_TYPE_EMO,	"EMO" },
	{ SYSINFO_TYPE_MEAS_INFO, "MI" },
	{ 0, NULL }
};

/*! \brief Add pair of arfcn and measurement bandwith value to earfcn struct
 *  \param[in,out] e earfcn struct
 *  \param[in] arfcn EARFCN value, 16 bits
 *  \param[in] meas_bw measurement bandwith value
 *  \returns 0 on success, error otherwise
 */
int osmo_earfcn_add(struct osmo_earfcn_si2q *e, uint16_t arfcn, uint8_t meas_bw)
{
	size_t i;
	for (i = 0; i < e->length; i++) {
		if (OSMO_EARFCN_INVALID == e->arfcn[i]) {
			e->arfcn[i] = arfcn;
			e->meas_bw[i] = meas_bw;
			return 0;
		}
	}
	return -ENOMEM;
}

/*! \brief Return number of bits necessary to represent earfcn struct as
 *  Repeated E-UTRAN Neighbour Cells IE from 3GPP TS 44.018 Table 10.5.2.33b.1
 *  \param[in,out] e earfcn struct
 *  \returns number of bits
 */
size_t osmo_earfcn_bit_size(const struct osmo_earfcn_si2q *e)
{
	/* 1 stop bit + 5 bits for THRESH_E-UTRAN_high */
	size_t i, bits = 6;
	for (i = 0; i < e->length; i++) {
		if (e->arfcn[i] != OSMO_EARFCN_INVALID) {
			bits += 17;
			if (OSMO_EARFCN_MEAS_INVALID == e->meas_bw[i])
				bits++;
			else
				bits += 4;
		}
	}
	bits += (e->prio_valid) ? 4 : 1;
	bits += (e->thresh_lo_valid) ? 6 : 1;
	bits += (e->qrxlm_valid) ? 6 : 1;
	return bits;
}

/*! \brief Delete arfcn (and corresponding measurement bandwith) from earfcn
 *  struct
 *  \param[in,out] e earfcn struct
 *  \param[in] arfcn EARFCN value, 16 bits
 *  \returns 0 on success, error otherwise
 */
int osmo_earfcn_del(struct osmo_earfcn_si2q *e, uint16_t arfcn)
{
	size_t i;
	for (i = 0; i < e->length; i++) {
		if (arfcn == e->arfcn[i]) {
			e->arfcn[i] = OSMO_EARFCN_INVALID;
			e->meas_bw[i] = OSMO_EARFCN_MEAS_INVALID;
			return 0;
		}
	}
	return -ENOENT;
}

/*! \brief Initialize earfcn struct
 *  \param[in,out] e earfcn struct
 */
void osmo_earfcn_init(struct osmo_earfcn_si2q *e)
{
	size_t i;
	for (i = 0; i < e->length; i++) {
		e->arfcn[i] = OSMO_EARFCN_INVALID;
		e->meas_bw[i] = OSMO_EARFCN_MEAS_INVALID;
	}
}

uint8_t osmo_sitype2rsl(enum osmo_sysinfo_type si_type)
{
	return sitype2rsl[si_type];
}

enum osmo_sysinfo_type osmo_rsl2sitype(uint8_t rsl_si)
{
	return rsl2sitype[rsl_si];
}
