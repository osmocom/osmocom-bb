/* Lifecycle of an APN */
/*
 * (C) 2023 by sysmocom - s.m.f.c. GmbH <info@sysmocom.de>
 * All Rights Reserved
 *
 * SPDX-License-Identifier: AGPL-3.0+
 *
 * Author: Pau Espin Pedrol <pespin@sysmocom.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <errno.h>
#include <osmocom/core/tdef.h>
#include <osmocom/core/utils.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/apn_fsm.h>
#include <osmocom/bb/common/apn.h>
#define X(s) (1 << (s))

static struct osmo_tdef T_defs_apn[] = {
	{ .T=1, .default_val=30, .desc = "Activating timeout (s)" },
	{ 0 } /* empty item at the end */
};

static const struct osmo_tdef_state_timeout apn_fsm_timeouts[32] = {
	[APN_ST_DISABLED] = {},
	[APN_ST_INACTIVE] = {},
	[APN_ST_ACTIVATING] = { .T=1 },
	[APN_ST_ACTIVE] = {},
};

#define apn_fsm_state_chg(fi, NEXT_STATE) \
	osmo_tdef_fsm_inst_state_chg(fi, NEXT_STATE, apn_fsm_timeouts, T_defs_apn, -1)

static void st_apn_disabled_on_enter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct apn_fsm_ctx *ctx = (struct apn_fsm_ctx *)fi->priv;

	apn_stop(ctx->apn);
}

static void st_apn_disabled(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	switch (event) {
	case APN_EV_GPRS_ALLOWED:
		if (*((bool *)data) == true)
			apn_fsm_state_chg(fi, APN_ST_INACTIVE);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void st_apn_inactive_on_enter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct apn_fsm_ctx *ctx = (struct apn_fsm_ctx *)fi->priv;

	int rc = apn_start(ctx->apn);
	if (rc < 0)
		apn_fsm_state_chg(fi, APN_ST_DISABLED);

	/* FIXME: Here once we find a way to store whether the ms object is GMM
	attached, we can transition directly to ACTIVATING. */
}

static void st_apn_inactive(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	switch (event) {
	case APN_EV_GPRS_ALLOWED:
		if (*((bool *)data) == false)
			apn_fsm_state_chg(fi, APN_ST_DISABLED);
		break;
	case APN_EV_GMM_ATTACHED:
		apn_fsm_state_chg(fi, APN_ST_ACTIVATING);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void st_apn_activating_on_enter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	/* FIXME: We could send SMREG-PDP_ACT.req from here. Right now that's done by the app. */
}

static void st_apn_activating(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	switch (event) {
	case APN_EV_GPRS_ALLOWED:
		/* TODO: Tx PDP DEACT ACC */
		apn_fsm_state_chg(fi, APN_ST_DISABLED);
		break;
	case APN_EV_GMM_DETACHED:
		apn_fsm_state_chg(fi, APN_ST_INACTIVE);
		break;
	case APN_EV_RX_SM_ACT_PDP_CTX_REJ:
		apn_fsm_state_chg(fi, APN_ST_INACTIVE);
		break;
	case APN_EV_RX_SM_ACT_PDP_CTX_ACC:
		apn_fsm_state_chg(fi, APN_ST_ACTIVE);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void st_apn_active_on_enter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct apn_fsm_ctx *ctx = (struct apn_fsm_ctx *)fi->priv;
	struct osmo_netdev *netdev;

	netdev = osmo_tundev_get_netdev(ctx->apn->tun);
	osmo_netdev_ifupdown(netdev, true);
}

static void st_apn_active(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	switch (event) {
	case APN_EV_GPRS_ALLOWED:
		/* TODO: Tx PDP DEACT ACC */
		apn_fsm_state_chg(fi, APN_ST_DISABLED);
		break;
	case APN_EV_GMM_DETACHED:
		apn_fsm_state_chg(fi, APN_ST_INACTIVE);
		break;
	case APN_EV_RX_SM_DEACT_PDP_CTX_ACC:
		apn_fsm_state_chg(fi, APN_ST_INACTIVE);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static int apn_fsm_timer_cb(struct osmo_fsm_inst *fi)
{
	switch (fi->T) {
	case 1:
		apn_fsm_state_chg(fi, APN_ST_INACTIVE);
		break;
	default:
		OSMO_ASSERT(0);
	}
	return 0;
}

static struct osmo_fsm_state apn_fsm_states[] = {
	[APN_ST_DISABLED] = {
		.in_event_mask =
			X(APN_EV_GPRS_ALLOWED),
		.out_state_mask =
			X(APN_ST_INACTIVE),
		.name = "DISABLED",
		.onenter = st_apn_disabled_on_enter,
		.action = st_apn_disabled,
	},
	[APN_ST_INACTIVE] = {
		.in_event_mask =
			X(APN_EV_GPRS_ALLOWED) |
			X(APN_EV_GMM_ATTACHED),
		.out_state_mask =
			X(APN_ST_ACTIVATING),
		.name = "INACTIVE",
		.onenter = st_apn_inactive_on_enter,
		.action = st_apn_inactive,
	},
	[APN_ST_ACTIVATING] = {
		.in_event_mask =
			X(APN_EV_GPRS_ALLOWED) |
			X(APN_EV_GMM_DETACHED) |
			X(APN_EV_RX_SM_ACT_PDP_CTX_REJ) |
			X(APN_EV_RX_SM_ACT_PDP_CTX_ACC),
		.out_state_mask =
			X(APN_ST_DISABLED) |
			X(APN_ST_INACTIVE) |
			X(APN_ST_ACTIVE),
		.name = "ACTIVATING",
		.onenter = st_apn_activating_on_enter,
		.action = st_apn_activating,
	},
	[APN_ST_ACTIVE] = {
		.in_event_mask =
			X(APN_EV_GPRS_ALLOWED) |
			X(APN_EV_GMM_DETACHED)|
			X(APN_EV_RX_SM_DEACT_PDP_CTX_ACC),
		.out_state_mask =
			X(APN_ST_DISABLED) |
			X(APN_ST_INACTIVE),
		.name = "ACTIVE",
		.onenter = st_apn_active_on_enter,
		.action = st_apn_active,
	},
};

const struct value_string apn_fsm_event_names[] = {
	{ APN_EV_GPRS_ALLOWED,		"GPRS_ALLOWED" },
	{ APN_EV_GMM_ATTACHED,		"GMM_ATTACHED" },
	{ APN_EV_GMM_DETACHED,		"GMM_DETACHED" },
	{ APN_EV_RX_SM_ACT_PDP_CTX_REJ,	"ACT_PDP_CTX_REJ" },
	{ APN_EV_RX_SM_ACT_PDP_CTX_ACC,	"ACT_PDP_CTX_ACC" },
	{ APN_EV_RX_SM_DEACT_PDP_CTX_ACC, "DEACT_PDP_CTX_ACC" },
	{ 0, NULL }
};

struct osmo_fsm apn_fsm = {
	.name = "APN",
	.states = apn_fsm_states,
	.num_states = ARRAY_SIZE(apn_fsm_states),
	.timer_cb = apn_fsm_timer_cb,
	.event_names = apn_fsm_event_names,
	.log_subsys = DTUN,
	.timer_cb = apn_fsm_timer_cb,
};

int apn_fsm_ctx_init(struct apn_fsm_ctx *ctx, struct osmobb_apn *apn)
{
	ctx->apn = apn;
	ctx->fi = osmo_fsm_inst_alloc(&apn_fsm, apn, ctx, LOGL_INFO, NULL);
	if (!ctx->fi)
		return -ENODATA;

	return 0;
}

void apn_fsm_ctx_release(struct apn_fsm_ctx *ctx)
{
	osmo_fsm_inst_free(ctx->fi);
}

static __attribute__((constructor)) void apn_fsm_init(void)
{
	OSMO_ASSERT(osmo_fsm_register(&apn_fsm) == 0);
	osmo_tdefs_reset(T_defs_apn);
}
