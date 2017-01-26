#pragma once

#include <layer1/sync.h>
#include "l1ctl_sock.h"
#include "virtual_um.h"

struct l1_model_ms {
	struct l1ctl_sock_inst *lsi;
	struct virt_um_inst *vui;
	struct l1_state_ms *state;
};

//TODO: must contain logical channel information (fram number, ciphering mode, ...)
struct l1_state_ms {

	uint8_t camping;
	/* the cell on which we are camping right now */
	struct l1_cell_info serving_cell;

	/* neighbor cell sync info */
	struct l1_cell_info neigh_cell[L1S_NUM_NEIGH_CELL];

	/* TCH */
	uint8_t tch_mode;
	uint8_t tch_sync;
	uint8_t audio_mode;
};

struct l1_model_ms *l1_model_ms_init(void *ctx);

void l1_model_ms_destroy(struct l1_model_ms *model);

