#pragma once

#include <stdbool.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>

#define OSMO_EARFCN_INVALID 666
#define OSMO_EARFCN_MEAS_INVALID 0xff

enum osmo_sysinfo_type {
	SYSINFO_TYPE_NONE,
	SYSINFO_TYPE_1,
	SYSINFO_TYPE_2,
	SYSINFO_TYPE_3,
	SYSINFO_TYPE_4,
	SYSINFO_TYPE_5,
	SYSINFO_TYPE_6,
	SYSINFO_TYPE_7,
	SYSINFO_TYPE_8,
	SYSINFO_TYPE_9,
	SYSINFO_TYPE_10,
	SYSINFO_TYPE_13,
	SYSINFO_TYPE_16,
	SYSINFO_TYPE_17,
	SYSINFO_TYPE_18,
	SYSINFO_TYPE_19,
	SYSINFO_TYPE_20,
	SYSINFO_TYPE_2bis,
	SYSINFO_TYPE_2ter,
	SYSINFO_TYPE_2quater,
	SYSINFO_TYPE_5bis,
	SYSINFO_TYPE_5ter,
	SYSINFO_TYPE_EMO,
	SYSINFO_TYPE_MEAS_INFO,
	/* FIXME all the various bis and ter */
	_MAX_SYSINFO_TYPE
};

struct osmo_earfcn_si2q {
	/* EARFCN (16 bits) array */
	uint16_t *arfcn;
	/* Measurement Bandwidth (3 bits), might be absent
	(OSMO_EARFCN_MEAS_INVALID is stored in this case) */
	uint8_t *meas_bw;
	/* length of arfcn and meas_bw arrays (got to be the same) */
	size_t length;
	/* THRESH_E-UTRAN_high (5 bits) */
	uint8_t thresh_hi;
	/* THRESH_E-UTRAN_low (5 bits) */
	uint8_t thresh_lo;
	/* E-UTRAN_PRIORITY (3 bits) */
	uint8_t prio;
	/* E-UTRAN_QRXLEVMIN */
	uint8_t qrxlm;
	/* indicates whether thresh_lo value is valid
	thresh_hi is mandatory and hence always considered valid */
	bool thresh_lo_valid;
	/* indicates whether prio value is valid */
	bool prio_valid;
	/* indicates whether qrxlm value is valid */
	bool qrxlm_valid;
};

typedef uint8_t sysinfo_buf_t[GSM_MACBLOCK_LEN];

extern const struct value_string osmo_sitype_strs[_MAX_SYSINFO_TYPE];
int osmo_earfcn_add(struct osmo_earfcn_si2q *e, uint16_t arfcn, uint8_t meas_bw);
int osmo_earfcn_del(struct osmo_earfcn_si2q *e, uint16_t arfcn);
size_t osmo_earfcn_bit_size(const struct osmo_earfcn_si2q *e);
void osmo_earfcn_init(struct osmo_earfcn_si2q *e);
uint8_t osmo_sitype2rsl(enum osmo_sysinfo_type si_type);
enum osmo_sysinfo_type osmo_rsl2sitype(uint8_t rsl_si);
