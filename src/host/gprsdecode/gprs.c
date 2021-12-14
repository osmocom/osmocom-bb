/*
 * (C) 2017-2018 by sysmocom - s.f.m.c. GmbH, Author: Max <msuraev@sysmocom.de>
 * (C) 2018 by Vadim Yanitskiy <axilirator@gmail.com>
 * (C) 2011-2012 by Luca Melette <luca@srlabs.de>
 *
 * All Rights Reserved
 *
 * SPDX-License-Identifier: GPL-2.0+
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>

#include <osmocom/core/bits.h>
#include <osmocom/core/gsmtap.h>
#include <osmocom/coding/gsm0503_coding.h>

#include "l1ctl_proto.h"
#include "rlcmac.h"
#include "gprs.h"

/**
 * We store both DL and UL burst buffers
 * for all possible timeslots...
 */
static struct burst_buf burst_buf_dl[8] = { 0 };
static struct burst_buf burst_buf_ul[8] = { 0 };

int process_pdch(struct l1ctl_burst_ind *bi, bool verbose)
{
	int n_errors, n_bits_total, rc, len, i;
	ubit_t buf[GSM_BURST_PL_LEN];
	struct gprs_message *gm;
	struct burst_buf *bb;
	uint8_t l2[200];
	uint16_t arfcn;
	uint32_t fn;
	uint8_t tn;
	bool ul;

	/* Get burst parameters */
	fn = ntohl(bi->frame_nr);
	arfcn = ntohs(bi->band_arfcn);
	ul = !!(arfcn & GSMTAP_ARFCN_F_UPLINK);
	tn = bi->chan_nr & 7;

	/* Select a proper DL / UL buffer */
	bb = ul ? &burst_buf_ul[tn] : &burst_buf_dl[tn];

	/* Align to first frame */
	if ((bb->count == 0) && (((fn % 13) % 4) != 0))
		return 0;

	/* Debug print */
	if (verbose)
		printf("Processing %s burst fn=%u, tn=%u\n",
			ul ? "UL" : "DL", fn, tn);

	/* Unpack hard-bits (1 or 0) */
	osmo_pbit2ubit_ext(buf,  0, bi->bits,  0, 57, 0);
	osmo_pbit2ubit_ext(buf, 59, bi->bits, 57, 57, 0);

	/* Set the stealing flags */
	buf[57] = bi->bits[14] & 0x10;
	buf[58] = bi->bits[14] & 0x20;

	/* Convert hard-bits (1 or 0) to soft-bits (-127..127) */
	for (i = 0; i < GSM_BURST_PL_LEN; i++)
		bb->bursts[GSM_BURST_PL_LEN * bb->count + i] = buf[i] ?
			-(bi->snr >> 1) : (bi->snr >> 1);

	/* Store the first frame number */
	if (bb->count == 0)
		bb->fn_first = fn;

	/* Collect the measurements */
	bb->rxl[bb->count] = bi->rx_level;
	bb->snr[bb->count] = bi->snr;

	/* Wait until complete set of bursts (4/4) */
	if (++bb->count < 4)
		return 0;

	/* Debug print */
	if (verbose)
		printf("Collected 4/4 bursts on tn=%u\n", tn);

	/* Flush the burst counter */
	bb->count = 0;

	/* Attempt to decode */
	len = gsm0503_pdtch_decode(l2, bb->bursts, NULL,
		&n_errors, &n_bits_total);

	/* Debug print */
	if (verbose)
		printf("GSM 05.03 decoding %s (%s=%d)\n",
			len <= 0 ? "failed" : "success",
			len <= 0 ? "rc" : "len", len);

	/* Skip bad blocks... */
	if (len <= 0)
		return -EIO;

	/**
	 * HACK: for some reason, the handler expects
	 * 53-byte messages, while libosmocoding
	 * generates 54 bytes ?? O_o ??
	 */
	len--;

	/* Handle decoded message */
	gm = (struct gprs_message *) malloc(sizeof(struct gprs_message) + len);
	if (!gm)
		return -ENOMEM;

	gm->fn = bb->fn_first;
	gm->arfcn = arfcn;
	gm->len = len;
	gm->tn = tn;

	/* Average the measurements */
	gm->rxl = MEAS_AVG(bb->rxl);
	gm->snr = MEAS_AVG(bb->snr);

	/* Copy the message payload */
	memcpy(gm->msg, l2, len);

	/* Handle the message */
	rc = rlc_type_handler(gm);
	free(gm);

	return rc;
}
