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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/gsmtap.h>
#include <osmocom/core/gsmtap_util.h>

#include "l1ctl_proto.h"
#include "gsmtap.h"

static struct gsmtap_inst *gti = NULL;

int gsmtap_init(const char *addr)
{
	gti = gsmtap_source_init(addr, GSMTAP_UDP_PORT, 0);
	if (!gti)
		return -EIO;

	gsmtap_source_add_sink(gti);
	return 0;
}

void gsmtap_send_rlcmac(uint8_t *msg, size_t len, uint8_t ts, bool ul)
{
	if (!gti)
		return;

	/* FIXME: explain params */
	gsmtap_send(gti,
		ul ? GSMTAP_ARFCN_F_UPLINK : 0,
		ts, GSMTAP_CHANNEL_PACCH, 0, 0, 0, 0, msg, len);
}

void gsmtap_send_llc(uint8_t *data, size_t len, bool ul)
{
	struct gsmtap_hdr *gh;
	struct msgb *msg;
	uint8_t *dst;

	if (!gti)
		return;

	/* Skip null frames */
	if ((data[0] == 0x43) &&
	    (data[1] == 0xc0) &&
	    (data[2] == 0x01))
		return;

	/* Allocate a new message buffer */
	msg = msgb_alloc(sizeof(*gh) + len, "gsmtap_tx");
	if (!msg)
	        return;

	/* Put header in front */
	gh = (struct gsmtap_hdr *) msgb_put(msg, sizeof(*gh));

	/* Fill in header */
	gh->version = GSMTAP_VERSION;
	gh->hdr_len = sizeof(*gh) / 4;
	gh->type = GSMTAP_TYPE_GB_LLC;
	gh->timeslot = 0;
	gh->sub_slot = 0;
	gh->arfcn = ul ? htons(GSMTAP_ARFCN_F_UPLINK) : 0;
	gh->snr_db = 0;
	gh->signal_dbm = 0;
	gh->frame_number = 0;
	gh->sub_type = 0;
	gh->antenna_nr = 0;

	/* Put and fill the payload */
	dst = msgb_put(msg, len);
	memcpy(dst, data, len);

	/* Finally, send to the sink */
	gsmtap_sendmsg(gti, msg);
}

