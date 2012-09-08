/* GSM utility functions, e.g. coding and decoding */
/*
 * (C) 2008 by Daniel Willmann <daniel@totalueberwachung.de>
 * (C) 2009 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
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

#ifndef GSM_UTILS_H
#define GSM_UTILS_H

#include <stdint.h>

#define ADD_MODULO(sum, delta, modulo) do {	\
	if ((sum += delta) >= modulo)		\
		sum -= modulo;			\
	} while (0)

#define GSM_MAX_FN	(26*51*2048)

struct gsm_time {
	uint32_t	fn;	/* FN count */
	uint16_t	t1;	/* FN div (26*51) */
	uint8_t		t2;	/* FN modulo 26 */
	uint8_t		t3;	/* FN modulo 51 */
	uint8_t		tc;
};

enum gsm_band {
	GSM_BAND_850	= 1,
	GSM_BAND_900	= 2,
	GSM_BAND_1800	= 4,
	GSM_BAND_1900	= 8,
	GSM_BAND_450	= 0x10,
	GSM_BAND_480	= 0x20,
	GSM_BAND_750	= 0x40,
	GSM_BAND_810	= 0x80,
};

const char *gsm_band_name(enum gsm_band band);
enum gsm_band gsm_band_parse(const char *mhz);

int gsm_7bit_decode(char *decoded, const uint8_t *user_data, uint8_t length);
int gsm_7bit_decode_hdr(char *decoded, const uint8_t *user_data, uint8_t length, uint8_t ud_hdr_ind);
int gsm_7bit_encode(uint8_t *result, const char *data);

int gsm_septets2octets(uint8_t *result, uint8_t *rdata, uint8_t septet_len, uint8_t padding);
int gsm_septet_encode(uint8_t *result, const char *data);
uint8_t gsm_get_octet_len(const uint8_t sept_len);

unsigned int ms_class_gmsk_dbm(enum gsm_band band, int ms_class);

int ms_pwr_ctl_lvl(enum gsm_band band, unsigned int dbm);
int ms_pwr_dbm(enum gsm_band band, uint8_t lvl);

/* According to TS 08.05 Chapter 8.1.4 */
int rxlev2dbm(uint8_t rxlev);
uint8_t dbm2rxlev(int dbm);

/* According to GSM 04.08 Chapter 10.5.1.6 */
static inline int ms_cm2_a5n_support(uint8_t *cm2, int n) {
	switch (n) {
		case 0: return 1;
		case 1: return (cm2[0] & (1<<3)) ? 0 : 1;
		case 2: return (cm2[2] & (1<<0)) ? 1 : 0;
		case 3: return (cm2[2] & (1<<1)) ? 1 : 0;
		default:
			return 0;
	}
}

/* According to GSM 04.08 Chapter 10.5.2.29 */
static inline int rach_max_trans_val2raw(int val) { return (val >> 1) & 3; }
static inline int rach_max_trans_raw2val(int raw) {
	const int tbl[4] = { 1, 2, 4, 7 };
	return tbl[raw & 3];
}

#define	ARFCN_PCS	0x8000
#define	ARFCN_UPLINK	0x4000
#define	ARFCN_FLAG_MASK	0xf000	/* Reserve the upper 5 bits for flags */

enum gsm_band gsm_arfcn2band(uint16_t arfcn);

/* Convert an ARFCN to the frequency in MHz * 10 */
uint16_t gsm_arfcn2freq10(uint16_t arfcn, int uplink);

/* Convert from frame number to GSM time */
void gsm_fn2gsmtime(struct gsm_time *time, uint32_t fn);

/* Convert from GSM time to frame number */
uint32_t gsm_gsmtime2fn(struct gsm_time *time);

/* GSM TS 03.03 Chapter 2.6 */
enum gprs_tlli_type {
	TLLI_LOCAL,
	TLLI_FOREIGN,
	TLLI_RANDOM,
	TLLI_AUXILIARY,
	TLLI_RESERVED,
};

/* TS 03.03 Chapter 2.6 */
int gprs_tlli_type(uint32_t tlli);

uint32_t gprs_tmsi2tlli(uint32_t p_tmsi, enum gprs_tlli_type type);

/* Osmocom internal, not part of any gsm spec */
enum gsm_phys_chan_config {
	GSM_PCHAN_NONE,
	GSM_PCHAN_CCCH,
	GSM_PCHAN_CCCH_SDCCH4,
	GSM_PCHAN_TCH_F,
	GSM_PCHAN_TCH_H,
	GSM_PCHAN_SDCCH8_SACCH8C,
	GSM_PCHAN_PDCH,		/* GPRS PDCH */
	GSM_PCHAN_TCH_F_PDCH,	/* TCH/F if used, PDCH otherwise */
	GSM_PCHAN_UNKNOWN,
	_GSM_PCHAN_MAX
};

/* Osmocom internal, not part of any gsm spec */
enum gsm_chan_t {
	GSM_LCHAN_NONE,
	GSM_LCHAN_SDCCH,
	GSM_LCHAN_TCH_F,
	GSM_LCHAN_TCH_H,
	GSM_LCHAN_UNKNOWN,
	GSM_LCHAN_CCCH,
	GSM_LCHAN_PDTCH,
	_GSM_LCHAN_MAX
};


#endif
