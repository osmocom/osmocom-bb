#pragma once

#include "l1ctl_sock.h"
#include "virtual_um.h"

#define L1S_NUM_NEIGH_CELL	6
#define A5_KEY_LEN		8

struct l1_model_ms {
	struct l1ctl_sock_inst *lsi;
	struct virt_um_inst *vui;
	struct l1_state_ms *state;
	struct crypto_info_ms *crypto_inf;
};

/* structure representing L1 sync information about a cell */
struct l1_cell_info {
	/* on which ARFCN (+band) is the cell? */
	uint16_t	arfcn;
	/* what's the BSIC of the cell (from SCH burst decoding) */
	uint8_t		bsic;
	/* Combined or non-combined CCCH */
	uint8_t		ccch_mode; /* enum ccch_mode */
	/* whats the delta of the cells current GSM frame number
	 * compared to our current local frame number */
	int32_t		fn_offset;
	/* how much does the TPU need adjustment (delta) to synchronize
	 * with the cells burst */
	uint32_t	time_alignment;
};

struct crypto_info_ms {
	/* key is expected in the same format as in RSL
	 * Encryption information IE. */
	uint8_t key[A5_KEY_LEN];
	uint8_t algo;
};

struct l1_state_ms {

	uint8_t camping; // are we currently camping on a cell

	/* the cell on which we are camping right now */
	struct l1_cell_info serving_cell;
	/* neighbor cell sync info */
	struct l1_cell_info neigh_cell[L1S_NUM_NEIGH_CELL];

	/* TCH info */
	uint8_t tch_mode; // see enum gsm48_chan_mode in gsm_04_08.h
	uint8_t tch_sync; // needed for audio synchronization
	uint8_t audio_mode; // see l1ctl_proto.h, e.g. AUDIO_TX_MICROPHONE

	/* dedicated channel info */
	struct {
		uint8_t chan_type; // like rsl chantype 08.58 -> Chapter 9.3.1 */

		uint8_t tn; // timeslot number 1-7

		uint8_t scn; // single-hop cellular network? (ununsed in virtual um)
		uint8_t tsc; // training sequence code (ununsed in virtual um)
		uint8_t h; // hopping enabled flag (ununsed in virtual um)
	} dedicated;
};

struct l1_model_ms *l1_model_ms_init(void *ctx);

void l1_model_ms_destroy(struct l1_model_ms *model);

