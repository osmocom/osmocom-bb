/*
 * L1CTL PHY info / features negotiation
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

#include <errno.h>
#include <stdint.h>

#include <arpa/inet.h>

#include <osmocom/core/negotiation.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/msgb.h>

#include "l1ctl_proto.h"

static const struct value_string phy_info[] = {
	{L1CTL_PHY_INFO_NAME, "trxcon"},
	{ 0, NULL }
};

int trxcon_compose_phy_info(struct msgb *msg)
{
	struct l1ctl_phy_nego_ind *hdr;
	uint8_t *features_tlv;
	int rc;

	/* Header first */
	hdr = (struct l1ctl_phy_nego_ind *) msgb_put(msg, sizeof(*hdr));
	if (!hdr)
		return -ENOMEM;

	/* Encode basic PHY info */
	rc = osmo_nego_enc_info(msg, phy_info);
	if (rc < (ARRAY_SIZE(phy_info) - 1))
		return -EINVAL;

	/* PHY features TLV, length will be set latter */
	features_tlv = msgb_put(msg, 2);
	if (!features_tlv)
		return -ENOMEM;

	/* Frequency band support */
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_BAND_400);
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_BAND_850);
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_BAND_900);
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_BAND_DCS);
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_BAND_PCS);

	/* Channel coding support */
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_CHAN_XCCH);
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_CHAN_TCHF);

	/* Voice codecs support */
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_CODEC_FR);
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_CODEC_EFR);

	/* A5/X ciphering support */
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_A5_1);
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_A5_2);
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_A5_3);

	/* Other features */
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_TX);
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_PM);
	osmo_nego_add_feature(msg, L1CTL_PHY_FEATURE_TRAFFIC);

	/* Finally, compute the length fields */
	features_tlv[0] = L1CTL_PHY_INFO_FEATURES;
	features_tlv[1] = msg->tail - features_tlv - 2;
	hdr->len = htons(msg->tail - hdr->data);

	return 0;
}
