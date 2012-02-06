#ifndef _L1_SYNC_H
#define _L1_SYNC_H

#include <osmocom/core/linuxlist.h>
#include <osmocom/gsm/gsm_utils.h>
#include <layer1/tdma_sched.h>
#include <layer1/mframe_sched.h>
#include <l1ctl_proto.h>

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
	/* FIXME: should we also store the AFC value? */
};

enum l1s_chan {
	L1S_CHAN_MAIN,
	L1S_CHAN_SACCH,
	L1S_CHAN_TRAFFIC,
	_NUM_L1S_CHAN
};

enum l1_compl {
	L1_COMPL_FB,
	L1_COMPL_RACH,
	L1_COMPL_TX_NB,
	L1_COMPL_TX_TCH,
};

typedef void l1_compl_cb(enum l1_compl c);

#define L1S_NUM_COMPL		32
#define L1S_NUM_NEIGH_CELL	6

struct l1s_h0 {
	uint16_t arfcn;
};

struct l1s_h1 {
	uint8_t hsn;
	uint8_t maio;
	uint8_t n;
	uint16_t ma[64];
};

struct l1s_state {
	struct gsm_time	current_time;	/* current GSM time */
	struct gsm_time	next_time;	/* GSM time at next TMDMA irq */

	/* the cell on which we are camping right now */
	struct l1_cell_info serving_cell;

	/* neighbor cell sync info */
	struct l1_cell_info neigh_cell[L1S_NUM_NEIGH_CELL];

	/* TDMA scheduler */
	struct tdma_scheduler tdma_sched;

	/* Multiframe scheduler */
	struct mframe_scheduler mframe_sched;

	/* The current TPU offset register */
	uint32_t	tpu_offset;
	int32_t		tpu_offset_correction;

	/* TX parameters */
	int8_t		ta;
	uint8_t		tx_power;

	/* TCH */
	uint8_t		tch_mode;
	uint8_t		tch_sync;
	uint8_t		audio_mode;

	/* Transmit queues of pending packets for main DCCH and ACCH */
	struct llist_head tx_queue[_NUM_L1S_CHAN];
	struct msgb *tx_meas;

	/* Which L1A completions are scheduled right now */
	uint32_t scheduled_compl;
	/* callbacks for each of the completions */
	l1_compl_cb *completion[L1S_NUM_COMPL];

	/* Structures below are for L1-task specific parameters, used
	 * to communicate between l1-sync and l1-async (l23_api) */
	struct {
		uint8_t mode;	/* FB_MODE 0/1 */
	} fb;

	struct {
		/* power measurement l1 task */
		unsigned int mode;
		union {
			struct {
				uint16_t arfcn_start;
				uint16_t arfcn_next;
				uint16_t arfcn_end;
			} range;
		};
		struct msgb *msg;
	} pm;

	struct {
		uint8_t		ra;
	} rach;

	struct {
		enum {
			GSM_DCHAN_NONE = 0,
			GSM_DCHAN_SDCCH_4,
			GSM_DCHAN_SDCCH_8,
			GSM_DCHAN_TCH_H,
			GSM_DCHAN_TCH_F,
			GSM_DCHAN_UNKNOWN,
		} type;

		uint8_t scn;
		uint8_t tsc;
		uint8_t tn;
		uint8_t h;

		union {
			struct l1s_h0 h0;
			struct l1s_h1 h1;
		};

		uint8_t st_tsc;
		uint8_t st_tn;
		uint8_t st_h;

		union {
			struct l1s_h0 st_h0;
			struct l1s_h1 st_h1;
		};
	} dedicated;

	/* neighbour cell power measurement process */
	struct {
		uint8_t n, second;
		uint8_t pos;
		uint8_t running;
		uint16_t band_arfcn[64];
		uint8_t tn[64];
		uint8_t	level[64];
	} neigh_pm;
};

extern struct l1s_state l1s;

struct l1s_meas_hdr {
	uint16_t snr;		/* signal/noise ratio */
	int16_t toa_qbit;	/* time of arrival (qbits) */
	int16_t pm_dbm8;	/* power level in dbm/8 */
	int16_t freq_err; 	/* Frequency error in Hz */
};

int16_t l1s_snr_int(uint16_t snr);
uint16_t l1s_snr_fract(uint16_t snr);

void l1s_dsp_abort(void);

void l1s_tx_apc_helper(uint16_t arfcn);

/* schedule a completion */
void l1s_compl_sched(enum l1_compl c);

void l1s_init(void);

/* reset the layer1 as part of synchronizing to a new cell */
void l1s_reset(void);

/* init.c */
void layer1_init(void);

/* A debug macro to print every TDMA frame */
#ifdef DEBUG_EVERY_TDMA
#define putchart(x) putchar(x)
#else
#define putchart(x)
#endif

/* Convert an angle in fx1.15 notatinon into Hz */
#define BITFREQ_DIV_2PI		43104	/* 270kHz / 2 * pi */
#define BITFREQ_DIV_PI		86208	/* 270kHz / pi */
#define ANG2FREQ_SCALING	(2<<15)	/* 2^15 scaling factor for fx1.15 */
#define ANGLE_TO_FREQ(angle)	((int16_t)angle * BITFREQ_DIV_PI / ANG2FREQ_SCALING)

void l1s_reset_hw(void);
void synchronize_tdma(struct l1_cell_info *cinfo);
void l1s_time_inc(struct gsm_time *time, uint32_t delta_fn);
void l1s_time_dump(const struct gsm_time *time);

#endif /* _L1_SYNC_H */
