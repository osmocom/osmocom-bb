#pragma once

#include <stdint.h>

/* RX Level and RX Quality */
struct gsm_rx_lev_qual {
	uint8_t rx_lev;
	uint8_t rx_qual;
};

/* unidirectional measumrement report */
struct gsm_meas_rep_unidir {
	struct gsm_rx_lev_qual full;
	struct gsm_rx_lev_qual sub;
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
