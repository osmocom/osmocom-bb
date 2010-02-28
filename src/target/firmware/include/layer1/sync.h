#ifndef _L1_SYNC_H
#define _L1_SYNC_H

#include <layer1/tdma_sched.h>
#include <l1a_l23_interface.h>

struct l1_cell_info {
	uint16_t	arfcn;
	uint32_t	bsic;
	uint32_t	fn_offset;
	uint32_t	time_alignment;
};

struct l1s_state {
	struct gsm_time	current_time;	/* current time */
	struct gsm_time	next_time;	/* time at next TMDMA irq */

	struct l1_cell_info serving_cell;

	struct tdma_scheduler tdma_sched;

	uint32_t	tpu_offset;

	int		task;

	/* bit-mask of multi-frame tasks that are currently active */
	uint32_t	mf_tasks;
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

void l1s_fb_test(uint8_t base_fn, uint8_t fb_mode);
void l1s_sb_test(uint8_t base_fn);
void l1s_pm_test(uint8_t base_fn, uint16_t arfcn);
void l1s_nb_test(uint8_t base_fn);

void l1s_init(void);

/* init.c */
void layer1_init(void);

#endif /* _L1_SYNC_H */
