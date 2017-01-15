#pragma once

#include <stdint.h>
/* the data structure stored in msgb->cb for libgb apps */
struct libgb_msgb_cb {
	unsigned char *bssgph;
	unsigned char *llch;

	/* Cell Identifier */
	unsigned char *bssgp_cell_id;

	/* Identifiers of a BTS, equal to 'struct bssgp_bts_ctx' */
	uint16_t nsei;
	uint16_t bvci;

	/* Identifier of a MS (inside BTS), equal to 'struct sgsn_mm_ctx' */
	uint32_t tlli;
} __attribute__((packed, may_alias));
#define LIBGB_MSGB_CB(__msgb)	((struct libgb_msgb_cb *)&((__msgb)->cb[0]))
#define msgb_tlli(__x)		LIBGB_MSGB_CB(__x)->tlli
#define msgb_nsei(__x)		LIBGB_MSGB_CB(__x)->nsei
#define msgb_bvci(__x)		LIBGB_MSGB_CB(__x)->bvci
#define msgb_gmmh(__x)		(__x)->l3h
#define msgb_bssgph(__x)	LIBGB_MSGB_CB(__x)->bssgph
#define msgb_bssgp_len(__x)	((__x)->tail - (uint8_t *)msgb_bssgph(__x))
#define msgb_bcid(__x)		LIBGB_MSGB_CB(__x)->bssgp_cell_id
#define msgb_llch(__x)		LIBGB_MSGB_CB(__x)->llch

/* logging contexts */
#define GPRS_CTX_NSVC	0
#define GPRS_CTX_BVC	1

#include <osmocom/core/logging.h>
int gprs_log_filter_fn(const struct log_context *ctx,
			struct log_target *tar);
