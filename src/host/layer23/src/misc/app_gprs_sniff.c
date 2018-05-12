/*
 * GPRS packet sniffer
 *
 * (C) 2018 by Vadim Yanitskiy <axilirator@gmail.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdint.h>
#include <getopt.h>
#include <errno.h>
#include <stdio.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/signal.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/l1ctl.h>

#include <osmocom/bb/misc/layer3.h>
#include <osmocom/bb/gprs/rlcmac.h>
#include <osmocom/bb/gprs/gsmtap.h>

#include <l1ctl_proto.h>

static struct {
	uint8_t ts;
} app_data;

static int l1ctl_signal_cb(unsigned int subsys,
	unsigned int signal, void *handler_data, void *signal_data)
{
	struct osmobb_fbsb_res *fr;
	struct osmocom_ms *ms;
	uint8_t chan_nr, tsc;

	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_RESET:
		ms = (struct osmocom_ms *) signal_data;

		l1ctl_tx_fbsb_req(ms, ms->test_arfcn,
			L1CTL_FBSB_F_FB01SB, 300, 0,
			CCCH_MODE_NONE, dbm2rxlev(-85));
		break;
	case S_L1CTL_FBSB_RESP:
		fr = (struct osmobb_fbsb_res *) signal_data;
		chan_nr = (0x18 << 3) | app_data.ts;
		ms = fr->ms;
		tsc = 6;

		l1ctl_tx_dm_est_req_h0(ms, ms->test_arfcn,
			chan_nr, tsc, GSM48_CMODE_SIGN, 0x00);
		break;
	}

	return 0;
}

static int pdtch_msg_handler(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_dl *dl;
	struct gprs_message *gm;
	size_t packet_len;
	uint8_t *packet;

	/* Extract the packet and calculate its length */
	packet = (uint8_t *) msg->l3h;
	packet_len = msgb_l3len(msg);

	if (!packet_len) {
		msgb_free(msg);
		return 0;
	}

	printf("TRAFFIC IND (%s)\n", osmo_hexdump(packet, packet_len));

	/* Allocate an instance of 'gprs_message' */
	gm = (struct gprs_message *) malloc(sizeof(*gm) + packet_len);
	if (!gm) {
		msgb_free(msg);
		return -ENOMEM;
	}

	/* Fill in downlink info */
	dl = (struct l1ctl_info_dl *) msg->l1h;
	gm->arfcn = dl->band_arfcn;
	gm->fn = dl->frame_nr;
	gm->tn = dl->chan_nr & 0x07;
	gm->rxl = dl->rx_level;
	gm->snr = dl->snr;
	gm->len = packet_len;

	/* Copy the frame itself */
	memcpy(gm->msg, packet, packet_len);

	/* Distribute to RLC/MAC layers */
	rlc_type_handler(gm);

	/* We don't need the source message anymore */
	msgb_free(msg);
	free(gm);

	return 0;
}

int l23_app_init(struct osmocom_ms *ms)
{
	osmo_signal_register_handler(SS_L1CTL, &l1ctl_signal_cb, NULL);
	ms->l1_entity.l1_traffic_ind = &pdtch_msg_handler;

	gsmtap_init("127.0.0.1");

	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	return 0;
}

static int l23_cfg_supported()
{
	return L23_OPT_ARFCN | L23_OPT_TAP | L23_OPT_DBG;
}

static int l23_getopt_options(struct option **options)
{
	static struct option opts[] = {
		{"timeslot", 1, 0, 't'}
	};

	*options = opts;
	return ARRAY_SIZE(opts);
}

static int l23_cfg_handle(int c, const char *optarg)
{
	switch (c) {
	case 't':
		app_data.ts = atoi(optarg);
		break;
	}

	return 0;
}

static int l23_cfg_print_help()
{
	printf("\nApplication specific\n");
	printf("  -t --timeslot TS	Timeslot index to sniff\n");

	return 0;
}

static struct l23_app_info info = {
	.copyright = "Copyright (C) 2018 Vadim Yanitskiy <axilirator@gmail.com>\n",
	.getopt_string	= "t:",
	.cfg_supported	= l23_cfg_supported,
	.cfg_getopt_opt = l23_getopt_options,
	.cfg_handle_opt	= l23_cfg_handle,
	.cfg_print_help	= l23_cfg_print_help,
};

struct l23_app_info *l23_app_info()
{
	return &info;
}
