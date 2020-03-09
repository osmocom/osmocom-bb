#pragma once

/* Per-MS specific state, closely attached to the L1CTL user progran */

#include <virtphy/virtual_um.h>
#include <virtphy/l1ctl_sock.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/core/timer.h>

#define L1S_NUM_NEIGH_CELL	6
#define A5_KEY_LEN		8

enum ms_state {
	MS_STATE_IDLE_SEARCHING = 0,
	MS_STATE_IDLE_SYNCING,
	MS_STATE_IDLE_CAMPING,
	MS_STATE_DEDICATED,
	MS_STATE_TBF
};


/* structure representing L1 sync information about a cell */
struct l1_cell_info {
	/* on which ARFCN (+band) is the cell? */
	uint16_t arfcn;
	/* what's the BSIC of the cell (from SCH burst decoding) */
	uint8_t bsic;
	/* Combined or non-combined CCCH */
	uint8_t ccch_mode; /* enum ccch_mode */
	/* what's the delta of the cells current GSM frame number
	 * compared to our current local frame number */
	int32_t fn_offset;
	/* how much does the TPU need adjustment (delta) to synchronize
	 * with the cells burst */
	uint32_t time_alignment;
};

struct crypto_info_ms {
	/* key is expected in the same format as in RSL
	 * Encryption information IE. */
	uint8_t key[A5_KEY_LEN];
	uint8_t algo;
};

struct l1_state_ms {

	struct gsm_time	downlink_time;	/* current GSM time received on downlink */
	struct gsm_time current_time; /* GSM time used internally for scheduling */
	struct {
		uint32_t last_exec_fn;
		struct llist_head mframe_items;
	} sched;

	enum ms_state state;

	/* the cell on which we are camping right now */
	struct l1_cell_info serving_cell;
	/* neighbor cell sync info */
	struct l1_cell_info neigh_cell[L1S_NUM_NEIGH_CELL];
	struct crypto_info_ms crypto_inf;

	/* TCH info */
	uint8_t tch_mode; // see enum gsm48_chan_mode in gsm_04_08.h
	uint8_t tch_sync; // needed for audio synchronization
	uint8_t audio_mode; // see l1ctl_proto.h, e.g. AUDIO_TX_MICROPHONE

	/* dedicated channel info */
	struct {
		uint8_t chan_type; // like rsl chantype 08.58 -> Chapter 9.3.1 */

		uint16_t band_arfcn;
		uint8_t tn; // timeslot number 1-7
		uint8_t subslot; // subslot of the dedicated channel, SDCCH/4:[0-3], SDCCH/8:[0-7]

		uint8_t scn; // single-hop cellular network? (unused in virtual um)
		uint8_t tsc; // training sequence code (unused in virtual um)
		uint8_t h; // hopping enabled flag (unused in virtual um)
	} dedicated;
	struct {
		struct {
			uint8_t usf[8];
			struct llist_head tx_queue;
		} ul;
		struct {
			uint8_t tfi[8];
		} dl;
	} tbf;

	/* fbsb state */
	struct {
		uint32_t arfcn;
	} fbsb;

	/* power management state */
	struct {
		uint32_t timeout_us;
		uint32_t timeout_s;
		struct {
			int16_t arfcn_sig_lev_dbm[1024];
			uint8_t arfcn_sig_lev_red_dbm[1024];
			struct osmo_timer_list arfcn_sig_lev_timers[1024];
		} meas;
	} pm;
};

struct l1_model_ms {
	uint32_t nr;
	/* pointer to the L1CTL socket client associated with this specific MS */
	struct l1ctl_sock_client *lsc;
	/* pointer to the (shared) GSMTAP/VirtUM socket to talk to BTS(s) */
	struct virt_um_inst *vui;
	/* actual per-MS state */
	struct l1_state_ms state;
};


struct l1_model_ms *l1_model_ms_init(void *ctx, struct l1ctl_sock_client *lsc, struct virt_um_inst *vui);

void l1_model_ms_destroy(struct l1_model_ms *model);

