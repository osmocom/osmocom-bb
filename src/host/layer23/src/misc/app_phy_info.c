/*
 * PHY info / features negotiation test
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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <osmocom/core/negotiation.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/msgb.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/l1ctl.h>

#include <l1ctl_proto.h>

extern int quit;

static struct value_string fdesc[] = {
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_BAND_400),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_BAND_850),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_BAND_900),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_BAND_DCS),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_BAND_PCS),

	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_CODEC_HR),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_CODEC_FR),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_CODEC_EFR),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_CODEC_HR_AMR),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_CODEC_FR_AMR),

	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_CHAN_XCCH),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_CHAN_TCHF),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_CHAN_TCHH),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_CHAN_CBCH),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_CHAN_PTCH),

	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_A5_1),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_A5_2),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_A5_3),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_A5_4),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_A5_5),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_A5_6),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_A5_7),

	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_TX),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_FH),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_PM),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_NEIGH_PM),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_SIM),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_MEAS_REP),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_BURST_TRX),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_BURST_IND),
	OSMO_VALUE_STRING(L1CTL_PHY_FEATURE_TRAFFIC),

	{ 0, NULL }
};

static void parse_nego_info(struct msgb *msg)
{
	struct l1ctl_phy_nego_ind *hdr;
	const uint8_t *features;
	size_t features_len;
	const char *info;
	bool present;
	int i;

	hdr = (struct l1ctl_phy_nego_ind *) msg->l1h;

	info = osmo_nego_get_info(hdr->data, hdr->len, L1CTL_PHY_INFO_NAME);
	printf("[i] PHY name: %s\n", info);

	features = (uint8_t *) osmo_nego_get_info(hdr->data, hdr->len,
		L1CTL_PHY_INFO_FEATURES);
	if (!features) {
		printf("[!] PHY features: none\n");
		quit = 1;
		return;
	}

	/* HACK, modify the API */
	features_len = msg->tail - features;

	for (i = 0; i < _L1CTL_PHY_FEATURE_MAX; i++) {
		present = osmo_nego_check_feature(features, features_len, i);
		if (present)
			printf("[i] PHY feature: %s\n", get_value_string(fdesc, i));
	}

	/* We are done */
	quit = 1;
}

static int signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms = (struct osmocom_ms *) signal_data;

	/* FIXME: ignore other signals? */
	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_RESET:
		printf("RESET received, requesting info negotiation again...\n");
		l1ctl_tx_nego_req(ms);
		break;
	case S_L1CTL_NEGO_IND:
		printf("Received negotiation response, parsing...\n\n");
		parse_nego_info(ms->phy_info);
	}

	return 0;
}

int l23_app_init(struct osmocom_ms *ms)
{
	/* Register L1CTL handler */
	osmo_signal_register_handler(SS_L1CTL, &signal_cb, NULL);

	/* Send negotiation request and wait for response */
	printf("Sending negotiation request...\n");
	l1ctl_tx_nego_req(ms);

	return 0;
}

static struct l23_app_info info = {
	.copyright = "(C) 2018 by Vadim Yanitskiy <axilirator@gmail.com>\n",
};

struct l23_app_info *l23_app_info()
{
	return &info;
}
