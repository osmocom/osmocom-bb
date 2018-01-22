#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GSM_BURST_PL_LEN	116
#define GPRS_BURST_PL_LEN	GSM_BURST_PL_LEN

#define MEAS_AVG(meas) \
	((meas[0] + meas[1] + meas[2] + meas[3]) / 4)

/* Burst decoder state */
struct burst_buf {
	unsigned snr[4];
	unsigned rxl[4];
	unsigned errors;
	unsigned count;

	sbit_t bursts[GSM_BURST_PL_LEN * 4];
	uint32_t fn_first;
};

int process_pdch(struct l1ctl_burst_ind *bi, bool verbose);
