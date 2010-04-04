/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
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

/* GSM 04.08 4.1.2.2 SIM update status */
#define GSM_SIM_U0_NULL		0
#define GSM_SIM_U1_UPDATED	1
#define GSM_SIM_U2_NOT_UPDATED	2
#define GSM_SIM_U3_ROAMING_NA	3

struct gsm_plmn_na {
	struct llist_head	entry;
	uint16_t		mcc, mnc;
}

struct gsm_subsriber {
	/* status */
	uint8_t			sim_valid;
	uint8_t			ustate;

	/* LAI */
	uint8_t			lai_valid;
	u_int16_t		lai_mcc, lai_mnc, lai_lac;

	/* IMSI */
	uint16_t		mcc, mnc;
	char 			imsi[GSM_IMSI_LENGTH];

	/* TMSI */
	uint8_t			tmsi_valid;
	u_int32_t		tmsi;

	/* key */
	u_int8_t		key_seq; /* ciphering key sequence number */
	u_int8_t		key[32]; /* up to 256 bit */

	/* PLMN last registered */
	uint8_t			plmn_valid;
	uint16_t		plmn_mcc, plmn_mnc, plmn_lac;
	struct llist_head	plmn_na;

	/* our access */
	uint8_t			barred_access;
	uint8_t			class_access[16];

}


