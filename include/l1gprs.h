#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <osmocom/core/linuxlist.h>

struct l1gprs_state;
struct msgb;

struct l1gprs_tbf_pending_req {
	/*! Item in l1gprs_state->tbf_list_pending */
	struct llist_head list;
	/*! Uplink or Downlink */
	bool uplink;
	/*! TBF reference number (not index) */
	uint8_t tbf_ref;
	/*! PDCH timeslots used by this TBF */
	uint8_t slotmask;
	/*! (Downlink only) DL TFI (Temporary Flow Indentity): 0..31 */
	uint8_t dl_tfi;
	/*! TBF starting time (absolute TDMA Fn) */
	uint32_t start_fn;
};

struct l1gprs_tbf {
	/*! Item in l1gprs_state->tbf_list */
	struct llist_head list;
	/*! Uplink or Downlink */
	bool uplink;
	/*! TBF reference number (not index) */
	uint8_t tbf_ref;
	/*! PDCH timeslots used by this TBF */
	uint8_t slotmask;
	/*! (Downlink only) DL TFI (Temporary Flow Indentity): 0..31 */
	uint8_t dl_tfi;
};

struct l1gprs_pdch {
	/*! Timeslot number */
	uint8_t tn;
	/*! Backpointer to l1gprs_state we belong to */
	struct l1gprs_state *gprs;
	/*! UL TBF count */
	uint8_t ul_tbf_count;
	/*! DL TBF count */
	uint8_t dl_tbf_count;
	/*! DL TFI mask */
	uint32_t dl_tfi_mask;
	/*! Pending UL TBF count */
	uint8_t pending_ul_tbf_count;
	/*! Pending DL TBF count */
	uint8_t pending_dl_tbf_count;
};

static inline size_t l1gprs_pdch_use_count(const struct l1gprs_pdch *pdch)
{
	return pdch->ul_tbf_count + pdch->dl_tbf_count +
	       pdch->pending_ul_tbf_count + pdch->pending_dl_tbf_count;
}


typedef void (*l1gprs_pdch_changed_t)(struct l1gprs_pdch *pdch, bool active);

struct l1gprs_state {
	/*! PDCH state for each timeslot */
	struct l1gprs_pdch pdch[8];
	/*! Uplink and Downlink TBFs (active), struct l1gprs_pending_tbf */
	struct llist_head tbf_list;
	/*! Uplink and Downlink TBFs (pending), struct l1gprs_tbf_pending_req */
	struct llist_head tbf_list_pending;
	/*! Logging context (used as prefix for messages) */
	char *log_prefix;
	/*! Some private data for API user */
	void *priv;
	/*! Callback triggered to signal lower layers when a PDCH TS has to be activated/deactivated */
	l1gprs_pdch_changed_t pdch_changed_cb;
};

void l1gprs_logging_init(int logc);
struct l1gprs_state *l1gprs_state_alloc(void *ctx, const char *log_prefix, void *priv);
void l1gprs_state_free(struct l1gprs_state *gprs);
void l1gprs_state_set_pdch_changed_cb(struct l1gprs_state *gprs, l1gprs_pdch_changed_t pdch_changed_cb);

int l1gprs_handle_ul_tbf_cfg_req(struct l1gprs_state *gprs, const struct msgb *msg);
int l1gprs_handle_dl_tbf_cfg_req(struct l1gprs_state *gprs, const struct msgb *msg);

struct l1gprs_prim_block_hdr {
	uint32_t fn;
	uint8_t tn;
};

struct l1gprs_prim_ul_block_req {
	struct l1gprs_prim_block_hdr hdr;
	size_t data_len;
	const uint8_t *data;
};

struct l1gprs_prim_dl_block_ind {
	struct l1gprs_prim_block_hdr hdr;
	struct {
		uint16_t ber10k;
		int16_t ci_cb;
		uint8_t rx_lev;
	} meas;
	size_t data_len;
	const uint8_t *data;
};

int l1gprs_handle_ul_block_req(struct l1gprs_state *gprs,
			       struct l1gprs_prim_ul_block_req *req,
			       const struct msgb *msg);
struct msgb *l1gprs_handle_dl_block_ind(struct l1gprs_state *gprs,
					const struct l1gprs_prim_dl_block_ind *ind, uint8_t *usf);
struct msgb *l1gprs_handle_rts_ind(struct l1gprs_state *gprs, uint32_t fn, uint8_t tn, uint8_t usf);
