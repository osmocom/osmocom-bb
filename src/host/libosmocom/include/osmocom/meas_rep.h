#ifndef _MEAS_REP_H
#define _MEAS_REP_H

#define MRC_F_PROCESSED	0x0001

/* extracted from a L3 measurement report IE */
struct gsm_meas_rep_cell {
	u_int8_t rxlev;
	u_int8_t bsic;
	u_int8_t neigh_idx;
	u_int16_t arfcn;
	unsigned int flags;
};

/* RX Level and RX Quality */
struct gsm_rx_lev_qual {
	u_int8_t rx_lev;
	u_int8_t rx_qual;
};

/* unidirectional measumrement report */
struct gsm_meas_rep_unidir {
	struct gsm_rx_lev_qual full;
	struct gsm_rx_lev_qual sub;
};

#define MEAS_REP_F_UL_DTX	0x01
#define MEAS_REP_F_DL_VALID	0x02
#define MEAS_REP_F_BA1		0x04
#define MEAS_REP_F_DL_DTX	0x08
#define MEAS_REP_F_MS_TO	0x10
#define MEAS_REP_F_MS_L1	0x20
#define MEAS_REP_F_FPC		0x40

/* parsed uplink and downlink measurement result */
struct gsm_meas_rep {
	/* back-pointer to the logical channel */
	struct gsm_lchan *lchan;

	/* number of the measurement report */
	u_int8_t nr;
	/* flags, see MEAS_REP_F_* */
	unsigned int flags;

	/* uplink and downlink rxlev, rxqual; full and sub */
	struct gsm_meas_rep_unidir ul;
	struct gsm_meas_rep_unidir dl;

	u_int8_t bs_power;
	u_int8_t ms_timing_offset;
	struct {
		int8_t pwr;	/* MS power in dBm */
		u_int8_t ta;	/* MS timing advance */
	} ms_l1;

	/* neighbor measurement reports for up to 6 cells */
	int num_cell;
	struct gsm_meas_rep_cell cell[6];
};

enum meas_rep_field {
	MEAS_REP_DL_RXLEV_FULL,
	MEAS_REP_DL_RXLEV_SUB,
	MEAS_REP_DL_RXQUAL_FULL,
	MEAS_REP_DL_RXQUAL_SUB,
	MEAS_REP_UL_RXLEV_FULL,
	MEAS_REP_UL_RXLEV_SUB,
	MEAS_REP_UL_RXQUAL_FULL,
	MEAS_REP_UL_RXQUAL_SUB,
};

/* obtain an average over the last 'num' fields in the meas reps */
int get_meas_rep_avg(const struct gsm_lchan *lchan,
		     enum meas_rep_field field, unsigned int num);

/* Check if N out of M last values for FIELD are >= bd */
int meas_rep_n_out_of_m_be(const struct gsm_lchan *lchan,
			enum meas_rep_field field,
			unsigned int n, unsigned int m, int be);

unsigned int calc_initial_idx(unsigned int array_size,
			      unsigned int meas_rep_idx,
			      unsigned int num_values);

#endif /* _MEAS_REP_H */
