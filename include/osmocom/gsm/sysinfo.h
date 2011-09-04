#ifndef _OSMO_GSM_SYSINFO_H
#define _OSMO_GSM_SYSINFO_H

#include <osmocom/core/utils.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>

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

typedef uint8_t sysinfo_buf_t[GSM_MACBLOCK_LEN];

extern const struct value_string osmo_sitype_strs[_MAX_SYSINFO_TYPE];

uint8_t osmo_sitype2rsl(enum osmo_sysinfo_type si_type);
enum osmo_sysinfo_type osmo_rsl2sitype(uint8_t rsl_si);

#endif /* _OSMO_GSM_SYSINFO_H */
