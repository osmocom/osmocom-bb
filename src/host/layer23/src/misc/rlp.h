#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <osmocom/core/utils.h>

/* 3GPP TS 24.002 Section 5.2.1 */
enum rlp_ftype {
	RLP_FT_U,
	RLP_FT_S,
	RLP_FT_IS,
};

static const struct value_string rlp_ftype_vals[] = {
	{ RLP_FT_U,	"U" },
	{ RLP_FT_S,	"S" },
	{ RLP_FT_IS,	"IS" },
	{ 0, NULL }
};

/* 3GPP TS 24.002 Section 5.2.1 */
enum rlp_u_ftype {
	RLP_U_FT_SABM	= 0x07,
	RLP_U_FT_UA	= 0x0c,
	RLP_U_FT_DISC	= 0x08,
	RLP_U_FT_DM	= 0x03,
	RLP_U_FT_NULL	= 0x0f,
	RLP_U_FT_UI	= 0x00,
	RLP_U_FT_XID	= 0x17,
	RLP_U_FT_TEST	= 0x1c,
	RLP_U_FT_REMAP	= 0x11,
};
static const struct value_string rlp_ftype_u_vals[] = {
	{ RLP_U_FT_SABM,	"SABM" },
	{ RLP_U_FT_UA,		"UA" },
	{ RLP_U_FT_DISC,	"DISC" },
	{ RLP_U_FT_DM,		"DM" },
	{ RLP_U_FT_NULL,	"NULL" },
	{ RLP_U_FT_UI,		"UI" },
	{ RLP_U_FT_XID,		"XID" },
	{ RLP_U_FT_TEST,	"TEST" },
	{ RLP_U_FT_REMAP,	"REMAP" },
	{ 0, NULL }
};

/* 3GPP TS 24.002 Section 5.2.1 */
enum rlp_s_ftype {
	RLP_S_FT_RR	= 0,
	RLP_S_FT_REJ	= 2,
	RLP_S_FT_RNR	= 1,
	RLP_S_FT_SREJ	= 3,
};
static const struct value_string rlp_ftype_s_vals[] = {
	{ RLP_S_FT_RR,		"RR" },
	{ RLP_S_FT_REJ,		"REJ" },
	{ RLP_S_FT_RNR,		"RNR" },
	{ RLP_S_FT_SREJ,	"SREJ" },
	{ 0, NULL }
};

struct rlp_frame_decoded {
	uint8_t version;
	enum rlp_ftype ftype;
	enum rlp_u_ftype u_ftype;
	enum rlp_s_ftype s_ftype;
	bool c_r;
	bool p_f;
	uint8_t s_bits;
	uint16_t n_s;
	uint16_t n_r;
	uint32_t fcs;
	uint8_t info[536/8];
	uint16_t info_len;
};


int rlp_decode(struct rlp_frame_decoded *out, uint8_t version, const uint8_t *data, size_t data_len);
uint32_t rlp_fcs_compute(const uint8_t *in, size_t in_len);
