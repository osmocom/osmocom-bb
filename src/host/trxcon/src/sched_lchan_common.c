/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: common routines for lchan handlers
 *
 * (C) 2017-2022 by Vadim Yanitskiy <axilirator@gmail.com>
 * Contributions by sysmocom - s.f.m.c. GmbH
 *
 * All Rights Reserved
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

#include <errno.h>
#include <string.h>
#include <talloc.h>
#include <stdint.h>
#include <stdbool.h>

#include <arpa/inet.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/bits.h>

#include <osmocom/codec/codec.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>

/* GSM 05.02 Chapter 5.2.3 Normal Burst (NB) */
const uint8_t l1sched_nb_training_bits[8][26] = {
	{
		0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0,
		0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1,
	},
	{
		0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1,
		1, 1, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1,
	},
	{
		0, 1, 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1,
		0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0,
	},
	{
		0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0,
		1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0,
	},
	{
		0, 0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0,
		1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1,
	},
	{
		0, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 1, 0,
		0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 1, 0,
	},
	{
		1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1,
		0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1,
	},
	{
		1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0,
		0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0,
	},
};

/* Get a string representation of the burst buffer's completeness.
 * Examples: "  ****.." (incomplete, 4/6 bursts)
 *           "    ****" (complete, all 4 bursts)
 *           "**.***.." (incomplete, 5/8 bursts) */
const char *l1sched_burst_mask2str(const uint8_t *mask, int bits)
{
	/* TODO: CSD is interleaved over 22 bursts, so the mask needs to be extended */
	static char buf[8 + 1];
	char *ptr = buf;

	OSMO_ASSERT(bits <= 8 && bits > 0);

	while (--bits >= 0)
		*(ptr++) = (*mask & (1 << bits)) ? '*' : '.';
	*ptr = '\0';

	return buf;
}

bool l1sched_lchan_amr_prim_is_valid(struct l1sched_lchan_state *lchan,
				     struct msgb *msg, bool is_cmr)
{
	enum osmo_amr_type ft_codec;
	uint8_t cmr_codec;
	int ft, cmr, len;

	len = osmo_amr_rtp_dec(msgb_l2(msg), msgb_l2len(msg),
			       &cmr_codec, NULL, &ft_codec, NULL, NULL);
	if (len < 0) {
		LOGP_LCHAND(lchan, LOGL_ERROR, "Cannot send invalid AMR payload (%u): %s\n",
			    msgb_l2len(msg), msgb_hexdump_l2(msg));
		return false;
	}
	ft = -1;
	cmr = -1;
	for (unsigned int i = 0; i < lchan->amr.codecs; i++) {
		if (lchan->amr.codec[i] == ft_codec)
			ft = i;
		if (lchan->amr.codec[i] == cmr_codec)
			cmr = i;
	}
	if (ft < 0) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Codec (FT = %d) of RTP frame not in list\n", ft_codec);
		return false;
	}
	if (is_cmr && lchan->amr.ul_ft != ft) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Codec (FT = %d) of RTP cannot be changed now, but in next frame\n",
			    ft_codec);
		return false;
	}
	lchan->amr.ul_ft = ft;
	if (cmr < 0) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Codec (CMR = %d) of RTP frame not in list\n", cmr_codec);
	} else {
		lchan->amr.ul_cmr = cmr;
	}

	return true;
}
