#ifndef _L1_SYNC_H
#define _L1_SYNC_H

#include <osmocore/linuxlist.h>
#include <osmocore/gsm_utils.h>
#include <layer1/tdma_sched.h>
#include <l1a_l23_interface.h>

/* structure representing L1 sync information about a cell */
struct l1_cell_info {
	/* on which ARFCN (+band) is the cell? */
	uint16_t	arfcn;
	/* what's the BSIC of the cell (from SCH burst decoding) */
	uint8_t		bsic;
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
	_NUM_L1S_CHAN
};

#define L1S_NUM_NEIGH_CELL	6

struct l1s_state {
	struct gsm_time	current_time;	/* current GSM time */
	struct gsm_time	next_time;	/* GSM time at next TMDMA irq */

	/* the cell on which we are camping right now */
	struct l1_cell_info serving_cell;

	/* neighbor cell sync info */
	struct l1_cell_info neigh_cell[L1S_NUM_NEIGH_CELL];

	/* TDMA scheduler */
	struct tdma_scheduler tdma_sched;

	/* The current TPU offset register */
	uint32_t	tpu_offset;

	/* Transmit queues of pending packets for main DCCH and ACCH */
	struct llist_head tx_queue[_NUM_L1S_CHAN];

	/* bit-mask of multi-frame tasks that are currently active */
	uint32_t	mf_tasks;

	/* Structures below are for L1-task specific parameters, used
	 * to communicate between l1-sync and l1-async (l23_api) */
	struct {
		uint8_t mode;	/* FB_MODE 0/1 */
	} fb;

	struct {
		unsigned int count;
		unsigned int synced;
	} sb;

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
};

extern struct l1s_state l1s;

enum l1_sig_num {
	L1_SIG_PM,	/* Power Measurement */
	L1_SIG_NB,	/* Normal Burst */
};

struct l1s_meas_hdr {
	uint16_t snr;		/* signal/noise ratio */
	int16_t toa_qbit;	/* time of arrival (qbits) */
	int16_t pm_dbm8;	/* power level in dbm/8 */
	int16_t freq_err; 	/* Frequency error in Hz */
};

struct l1_signal {
	uint16_t signum;
	uint16_t arfcn;
	union {
		struct {
			int16_t dbm8[2];
		} pm;
		struct {
			struct l1s_meas_hdr meas[4];
			uint16_t crc;
			uint16_t fire;
			uint16_t num_biterr;
			uint8_t frame[24];
		} nb;
	};
};

typedef void (*l1s_cb_t)(struct l1_signal *sig);

void l1s_set_handler(l1s_cb_t handler);

int16_t l1s_snr_int(uint16_t snr);
uint16_t l1s_snr_fract(uint16_t snr);

void l1s_dsp_abort(void);

void l1s_fb_test(uint8_t base_fn, uint8_t fb_mode);
void l1s_sb_test(uint8_t base_fn);
void l1s_pm_test(uint8_t base_fn, uint16_t arfcn);
void l1s_nb_test(uint8_t base_fn);

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

extern l1s_cb_t l1s_cb;

void l1s_reset_hw(void);
void synchronize_tdma(struct l1_cell_info *cinfo);
void l1s_time_inc(struct gsm_time *time, uint32_t delta_fn);
void l1s_time_dump(const struct gsm_time *time);

#endif /* _L1_SYNC_H */
