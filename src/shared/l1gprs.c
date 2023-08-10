/*
 * l1gprs - GPRS layer 1 implementation
 *
 * (C) 2022-2023 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * Author: Vadim Yanitskiy <vyanitskiy@sysmocom.de>
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
#include <stdint.h>
#include <stdbool.h>

#include <arpa/inet.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/msgb.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_44_060.h>
#include <osmocom/gsm/gsm0502.h>

#include <osmocom/bb/l1ctl_proto.h>
#include <osmocom/bb/l1gprs.h>

#define LOGP_GPRS(gprs, level, fmt, args...) \
	LOGP(l1gprs_log_cat, level, "%s" fmt, \
	     (gprs)->log_prefix, ## args)

#define LOGP_PDCH(pdch, level, fmt, args...) \
	LOGP_GPRS((pdch)->gprs, level, "(PDCH-%u) " fmt, \
		  (pdch)->tn, ## args)

#define LOG_TBF_CFG_REQ_FMT "tbf_ref=%u, slotmask=0x%02x, start_fn=%u"
#define LOG_TBF_CFG_REQ_ARGS(req) \
	(req)->tbf_ref, (req)->slotmask, ntohl((req)->start_fn)

#define LOG_TBF_FMT "%cL-TBF#%03d"
#define LOG_TBF_ARGS(tbf) \
	(tbf)->uplink ? 'U' : 'D', tbf->tbf_ref

#define TDMA_FN_INVALID 0xffffffff

static int l1gprs_log_cat = DLGLOBAL;

enum gprs_rlcmac_block_type {
	GPRS_RLCMAC_DATA_BLOCK		= 0x00,
	GPRS_RLCMAC_CONTROL_BLOCK	= 0x01,
	GPRS_RLCMAC_CONTROL_BLOCK_OPT	= 0x02,
	GPRS_RLCMAC_RESERVED		= 0x03,
};

static struct l1gprs_tbf_pending_req *l1gprs_tbf_pending_req_alloc(void *talloc_ctx,
						       bool uplink, uint8_t tbf_ref,
						       uint8_t slotmask, uint32_t start_fn)
{
	struct l1gprs_tbf_pending_req *preq;

	preq = talloc(talloc_ctx, struct l1gprs_tbf_pending_req);
	OSMO_ASSERT(preq != NULL);

	preq->uplink = uplink;
	preq->tbf_ref = tbf_ref;
	preq->slotmask = slotmask;
	preq->start_fn = start_fn;

	return preq;
}

static void l1gprs_tbf_pending_req_free(struct l1gprs_tbf_pending_req *preq)
{
	if (preq == NULL)
		return;
	llist_del(&preq->list);
	talloc_free(preq);
}


static struct l1gprs_tbf *l1gprs_tbf_alloc(void *talloc_ctx,
					   bool uplink, uint8_t tbf_ref,
					   uint8_t slotmask)
{
	struct l1gprs_tbf *tbf;

	tbf = talloc(talloc_ctx, struct l1gprs_tbf);
	OSMO_ASSERT(tbf != NULL);

	tbf->uplink = uplink;
	tbf->tbf_ref = tbf_ref;
	tbf->slotmask = slotmask;

	return tbf;
}

static void l1gprs_tbf_free(struct l1gprs_tbf *tbf)
{
	if (tbf == NULL)
		return;
	llist_del(&tbf->list);
	talloc_free(tbf);
}

static struct l1gprs_tbf *_l1gprs_find_tbf(const struct llist_head *tbf_list,
					   bool uplink, uint8_t tbf_ref)
{
	struct l1gprs_tbf *tbf;

	llist_for_each_entry(tbf, tbf_list, list) {
		if (tbf->uplink != uplink)
			continue;
		if (tbf->tbf_ref != tbf_ref)
			continue;
		return tbf;
	}

	return NULL;
}

static struct l1gprs_tbf *l1gprs_find_tbf(struct l1gprs_state *gprs,
					  bool uplink, uint8_t tbf_ref)
{
	struct l1gprs_tbf *tbf;

	if ((tbf = _l1gprs_find_tbf(&gprs->tbf_list, uplink, tbf_ref)) != NULL)
		return tbf;
	return NULL;
}

static void l1gprs_register_tbf(struct l1gprs_state *gprs,
				struct l1gprs_tbf *tbf)
{
	OSMO_ASSERT(tbf->slotmask != 0x00);

	/* Update the PDCH states */
	for (unsigned int tn = 0; tn < ARRAY_SIZE(gprs->pdch); tn++) {
		struct l1gprs_pdch *pdch = &gprs->pdch[tn];

		if (~tbf->slotmask & (1 << pdch->tn))
			continue;

		if (tbf->uplink) {
			pdch->ul_tbf_count++;
		} else {
			pdch->dl_tbf_count++;
			pdch->dl_tfi_mask |= (1 << tbf->dl_tfi);
		}

		LOGP_PDCH(pdch, LOGL_DEBUG,
			  "Linked " LOG_TBF_FMT "\n",
			  LOG_TBF_ARGS(tbf));

		/* If just got first use: */
		if (l1gprs_pdch_use_count(pdch) == 1) {
			if (gprs->pdch_changed_cb)
				gprs->pdch_changed_cb(pdch, true);
		}
	}

	llist_add_tail(&tbf->list, &gprs->tbf_list);

	LOGP_GPRS(gprs, LOGL_INFO,
		  LOG_TBF_FMT " is registered as active\n",
		  LOG_TBF_ARGS(tbf));
}

static void l1gprs_update_tbf(struct l1gprs_state *gprs, struct l1gprs_tbf *tbf, uint8_t slotmask)
{
	OSMO_ASSERT(tbf->slotmask != 0x00);
	OSMO_ASSERT(slotmask != 0x00);

	if (tbf->slotmask == slotmask)
		return; /* No change at all, skip */

	/* Update the PDCH states */
	for (unsigned int tn = 0; tn < ARRAY_SIZE(gprs->pdch); tn++) {
		struct l1gprs_pdch *pdch = &gprs->pdch[tn];

		if ((tbf->slotmask & (1 << tn)) == (slotmask & (1 << tn)))
			continue; /* No change, skip */

		if (tbf->slotmask & (1 << tn)) {
			/* slot previously set, remove it */
			if (tbf->uplink) {
				OSMO_ASSERT(pdch->ul_tbf_count > 0);
				pdch->ul_tbf_count--;
			} else {
				OSMO_ASSERT(pdch->dl_tbf_count > 0);
				pdch->dl_tbf_count--;
				pdch->dl_tfi_mask &= ~(1 << tbf->dl_tfi);
			}
			LOGP_PDCH(pdch, LOGL_DEBUG, "Unlinked " LOG_TBF_FMT "\n",
				  LOG_TBF_ARGS(tbf));
			/* If not more in use: */
			if (l1gprs_pdch_use_count(pdch) == 0) {
				if (gprs->pdch_changed_cb)
					gprs->pdch_changed_cb(pdch, false);
			}
		} else {
			/* Slot was not set, add it */
			if (tbf->uplink) {
				pdch->ul_tbf_count++;
			} else {
				pdch->dl_tbf_count++;
				pdch->dl_tfi_mask |= (1 << tbf->dl_tfi);
			}
			LOGP_PDCH(pdch, LOGL_DEBUG, "Linked " LOG_TBF_FMT "\n",
				  LOG_TBF_ARGS(tbf));
			/* If just got first use: */
			if (l1gprs_pdch_use_count(pdch) == 1) {
				if (gprs->pdch_changed_cb)
					gprs->pdch_changed_cb(pdch, true);
			}
		}
	}

	LOGP_GPRS(gprs, LOGL_INFO,
		  LOG_TBF_FMT " slotmask updated 0x%02x -> 0x%02x\n",
		  LOG_TBF_ARGS(tbf), tbf->slotmask, slotmask);

	tbf->slotmask = slotmask;
}

static void l1gprs_unregister_tbf(struct l1gprs_state *gprs, struct l1gprs_tbf *tbf)
{
	OSMO_ASSERT(tbf->slotmask != 0x00);

	/* Update the PDCH states */
	for (unsigned int tn = 0; tn < ARRAY_SIZE(gprs->pdch); tn++) {
		struct l1gprs_pdch *pdch = &gprs->pdch[tn];

		if (~tbf->slotmask & (1 << pdch->tn))
			continue;

		if (tbf->uplink) {
			OSMO_ASSERT(pdch->ul_tbf_count > 0);
			pdch->ul_tbf_count--;
		} else {
			OSMO_ASSERT(pdch->dl_tbf_count > 0);
			pdch->dl_tbf_count--;
			pdch->dl_tfi_mask &= ~(1 << tbf->dl_tfi);
		}

		LOGP_PDCH(pdch, LOGL_DEBUG,
			  "Unlinked " LOG_TBF_FMT "\n",
			  LOG_TBF_ARGS(tbf));

		/* If not more in use: */
		if (l1gprs_pdch_use_count(pdch) == 0) {
			if (gprs->pdch_changed_cb)
				gprs->pdch_changed_cb(pdch, false);
		}
	}

	LOGP_GPRS(gprs, LOGL_INFO,
		  LOG_TBF_FMT " is unregistered and free()d\n",
		  LOG_TBF_ARGS(tbf));

	l1gprs_tbf_free(tbf);
}

static void l1gprs_add_tbf_pending_req(struct l1gprs_state *gprs, struct l1gprs_tbf_pending_req *preq)
{
	OSMO_ASSERT(preq->slotmask != 0x00);

	/* Update the PDCH states */
	for (unsigned int tn = 0; tn < ARRAY_SIZE(gprs->pdch); tn++) {
		struct l1gprs_pdch *pdch = &gprs->pdch[tn];

		if (~preq->slotmask & (1 << pdch->tn))
			continue;

		if (preq->uplink) {
			pdch->pending_ul_tbf_count++;
		} else {
			pdch->pending_dl_tbf_count++;
			/* We don't care about DL_TFI here, we don't want to activate it */
		}

		LOGP_PDCH(pdch, LOGL_DEBUG,
			  "Linked " LOG_TBF_FMT "\n",
			  LOG_TBF_ARGS(preq));
		/* If just got first use: */
		if (l1gprs_pdch_use_count(pdch) == 1) {
			if (gprs->pdch_changed_cb)
				gprs->pdch_changed_cb(pdch, true);
		}
	}

	llist_add_tail(&preq->list, &gprs->tbf_list_pending);

	LOGP_GPRS(gprs, LOGL_INFO,
		  LOG_TBF_FMT " is added as pending (fn=%u)\n",
		  LOG_TBF_ARGS(preq), preq->start_fn);
}

static void l1gprs_remove_tbf_pending_req(struct l1gprs_state *gprs, struct l1gprs_tbf_pending_req *preq)
{

	OSMO_ASSERT(preq->slotmask != 0x00);

	/* Update the PDCH states */
	for (unsigned int tn = 0; tn < ARRAY_SIZE(gprs->pdch); tn++) {
		struct l1gprs_pdch *pdch = &gprs->pdch[tn];

		if (~preq->slotmask & (1 << pdch->tn))
			continue;

		if (preq->uplink) {
			OSMO_ASSERT(pdch->pending_ul_tbf_count > 0);
			pdch->pending_ul_tbf_count--;
		} else {
			OSMO_ASSERT(pdch->pending_dl_tbf_count > 0);
			pdch->pending_dl_tbf_count--;
			/* We don't care about DL_TFI here, we didn't activate them in first place */
		}

		LOGP_PDCH(pdch, LOGL_DEBUG,
			  "Unlinked " LOG_TBF_FMT "\n",
			  LOG_TBF_ARGS(preq));
		/* Note: not calling gprs->pdch_changed_cb since no real
		 * activate / deactivate change can occur on lower layers as a
		 * consequence of moving a PDCH from pending to active, hence
		 * avoid triggering one active=false event here and immediately
		 * afterwards the opposite event when adding it as active: */
	}

	llist_del(&preq->list);

	LOGP_GPRS(gprs, LOGL_INFO,
		  LOG_TBF_FMT " is removed as pending (fn=%u)\n",
		  LOG_TBF_ARGS(preq), preq->start_fn);
}

/* Check if the current TDMA Fn is past the start TDMA Fn.
 * Based on fn_cmp() implementation from osmo-pcu.git, simplified. */
static bool l1gprs_check_fn(uint32_t current, uint32_t start)
{
	const uint32_t thresh = GSM_TDMA_HYPERFRAME / 2;

	if ((current < start && (start - current) < thresh) ||
	    (current > start && (current - start) > thresh))
		return false;

	return true;
}

/* Check the list of pending TBFs and move those with expired Fn to the active list */
static void l1gprs_check_pending_tbfs(struct l1gprs_state *gprs, uint32_t fn)
{
	struct l1gprs_tbf_pending_req *preq, *tmp;
	struct l1gprs_tbf *tbf;

	llist_for_each_entry_safe(preq, tmp, &gprs->tbf_list_pending, list) {
		if (!l1gprs_check_fn(fn, preq->start_fn))
			continue;

		LOGP_GPRS(gprs, LOGL_INFO,
			  LOG_TBF_FMT " becomes active (current_fn=%u, start_fn=%u)\n",
			  LOG_TBF_ARGS(preq), fn, preq->start_fn);

		l1gprs_remove_tbf_pending_req(gprs, preq);

		/* If this tbf already exists in the main list, simply update its timeslot: */
		tbf = _l1gprs_find_tbf(&gprs->tbf_list, preq->uplink, preq->tbf_ref);
		if (tbf) {
			l1gprs_update_tbf(gprs, tbf, preq->slotmask);
			tbf->dl_tfi = preq->dl_tfi;
		} else {
			tbf = l1gprs_tbf_alloc(gprs, preq->uplink, preq->tbf_ref, preq->slotmask);
			tbf->dl_tfi = preq->dl_tfi;
			l1gprs_register_tbf(gprs, tbf);
		}
		talloc_free(preq);
	}
}

#define L1GPRS_L1CTL_MSGB_SIZE		256
#define L1GPRS_L1CTL_MSGB_HEADROOM	32

static struct msgb *l1gprs_l1ctl_msgb_alloc(uint8_t msg_type)
{
	struct l1ctl_hdr *l1h;
	struct msgb *msg;

	msg = msgb_alloc_headroom(L1GPRS_L1CTL_MSGB_SIZE,
				  L1GPRS_L1CTL_MSGB_HEADROOM,
				  "l1gprs_l1ctl_msg");
	if (msg == NULL)
		return NULL;

	msg->l1h = msgb_put(msg, sizeof(*l1h));
	l1h = (struct l1ctl_hdr *)msg->l1h;
	l1h->msg_type = msg_type;

	return msg;
}

static bool l1gprs_pdch_filter_dl_block(const struct l1gprs_pdch *pdch,
					const uint8_t *data)
{
	enum gprs_rlcmac_block_type block_type = data[0] >> 6;
	uint8_t dl_tfi;

	switch (block_type) {
	case GPRS_RLCMAC_DATA_BLOCK:
		/* see 3GPP TS 44.060, section 10.2.1 */
		dl_tfi = (data[1] >> 1) & 0x1f;
		break;
	case GPRS_RLCMAC_CONTROL_BLOCK_OPT:
		/* see 3GPP TS 44.060, section 10.3.1 */
		dl_tfi = (data[2] >> 1) & 0x1f;
		break;
	case GPRS_RLCMAC_CONTROL_BLOCK:
		/* no optional octets */
		return true;
	default:
		LOGP_PDCH(pdch, LOGL_NOTICE,
			  "Rx Downlink block with unknown payload (0x%0x)\n",
			  block_type);
		return false;
	}

	if (pdch->dl_tfi_mask & (1 << dl_tfi))
		return true;
	return false;
}

///////////////////////////////////////////////////////////////////////////////////

void l1gprs_logging_init(int logc)
{
	l1gprs_log_cat = logc;
}

struct l1gprs_state *l1gprs_state_alloc(void *ctx, const char *log_prefix, void *priv)
{
	struct l1gprs_state *gprs;

	gprs = talloc_zero(ctx, struct l1gprs_state);
	if (gprs == NULL)
		return NULL;

	for (unsigned int tn = 0; tn < ARRAY_SIZE(gprs->pdch); tn++) {
		struct l1gprs_pdch *pdch = &gprs->pdch[tn];

		pdch->tn = tn;
		pdch->gprs = gprs;
	}

	INIT_LLIST_HEAD(&gprs->tbf_list);
	INIT_LLIST_HEAD(&gprs->tbf_list_pending);

	if (log_prefix == NULL)
		gprs->log_prefix = talloc_asprintf(gprs, "l1gprs[0x%p]: ", gprs);
	else
		gprs->log_prefix = talloc_strdup(gprs, log_prefix);
	gprs->priv = priv;

	return gprs;
}

void l1gprs_state_free(struct l1gprs_state *gprs)
{
	if (gprs == NULL)
		return;

	while (!llist_empty(&gprs->tbf_list)) {
		struct l1gprs_tbf *tbf;

		tbf = llist_first_entry(&gprs->tbf_list, struct l1gprs_tbf, list);
		LOGP_GPRS(gprs, LOGL_DEBUG,
			  "%s(): " LOG_TBF_FMT " is free()d\n",
			  __func__, LOG_TBF_ARGS(tbf));
		l1gprs_tbf_free(tbf);
	}

	while (!llist_empty(&gprs->tbf_list_pending)) {
		struct l1gprs_tbf_pending_req *preq;

		preq = llist_first_entry(&gprs->tbf_list_pending, struct l1gprs_tbf_pending_req, list);
		LOGP_GPRS(gprs, LOGL_DEBUG,
			  "%s(): " LOG_TBF_FMT " is free()d\n",
			  __func__, LOG_TBF_ARGS(preq));
		l1gprs_tbf_pending_req_free(preq);
	}

	talloc_free(gprs->log_prefix);
	talloc_free(gprs);
}

void l1gprs_state_set_pdch_changed_cb(struct l1gprs_state *gprs, l1gprs_pdch_changed_t pdch_changed_cb)
{
	gprs->pdch_changed_cb = pdch_changed_cb;
}

int l1gprs_handle_ul_tbf_cfg_req(struct l1gprs_state *gprs, const struct msgb *msg)
{
	const struct l1ctl_gprs_ul_tbf_cfg_req *req = (void *)msg->l1h;
	struct l1gprs_tbf *tbf = NULL;

	OSMO_ASSERT(req != NULL);

	if (msgb_l1len(msg) < sizeof(*req)) {
		LOGP_GPRS(gprs, LOGL_ERROR,
			  "Rx malformed Uplink TBF config (len=%u < %zu)\n",
			  msgb_l1len(msg), sizeof(*req));
		return -EINVAL;
	}

	LOGP_GPRS(gprs, LOGL_INFO,
		  "Rx UL TBF config: " LOG_TBF_CFG_REQ_FMT "\n",
		  LOG_TBF_CFG_REQ_ARGS(req));

	if (req->slotmask != 0x00) {
		uint32_t start_fn = ntohl(req->start_fn);
		if (start_fn != TDMA_FN_INVALID) {
			/* Create a temporary tbf and keep it in a separate
			 * list. It will be moved/merged into the main list at
			 * start_fn time. */
			struct l1gprs_tbf_pending_req *preq;
			preq = l1gprs_tbf_pending_req_alloc(gprs, true, req->tbf_ref,
							     req->slotmask, start_fn);
			l1gprs_add_tbf_pending_req(gprs, preq);
			return 0;
		}

		tbf = l1gprs_find_tbf(gprs, true, req->tbf_ref);
		if (tbf) {
			l1gprs_update_tbf(gprs, tbf, req->slotmask);
		} else {
			tbf = l1gprs_tbf_alloc(gprs, true, req->tbf_ref, req->slotmask);
			l1gprs_register_tbf(gprs, tbf);
		}
	} else {
		tbf = l1gprs_find_tbf(gprs, true, req->tbf_ref);
		if (tbf == NULL) {
			LOGP_GPRS(gprs, LOGL_ERROR, "%s(): " LOG_TBF_FMT " not found\n",
				  __func__, 'U', req->tbf_ref);
			return -ENOENT;
		}
		l1gprs_unregister_tbf(gprs, tbf);
	}

	return 0;
}

int l1gprs_handle_dl_tbf_cfg_req(struct l1gprs_state *gprs, const struct msgb *msg)
{
	const struct l1ctl_gprs_dl_tbf_cfg_req *req = (void *)msg->l1h;
	struct l1gprs_tbf *tbf = NULL;

	OSMO_ASSERT(req != NULL);

	if (msgb_l1len(msg) < sizeof(*req)) {
		LOGP_GPRS(gprs, LOGL_ERROR,
			  "Rx malformed Downlink TBF config (len=%u < %zu)\n",
			  msgb_l1len(msg), sizeof(*req));
		return -EINVAL;
	}

	LOGP_GPRS(gprs, LOGL_INFO,
		  "Rx DL TBF config: " LOG_TBF_CFG_REQ_FMT ", dl_tfi=%u\n",
		  LOG_TBF_CFG_REQ_ARGS(req), req->dl_tfi);

	if (req->dl_tfi > 31) {
		LOGP_GPRS(gprs, LOGL_ERROR,
			  "Invalid DL TFI %u (shall be in range 0..31)\n",
			  req->dl_tfi);
		return -EINVAL;
	}

	if (req->slotmask != 0x00) {
		uint32_t start_fn = ntohl(req->start_fn);
		if (start_fn != TDMA_FN_INVALID) {
			/* Create a temporary tbf and keep it in a separate
			 * list. It will be moved/merged into the main list at
			 * start_fn time. */
			struct l1gprs_tbf_pending_req *preq;
			preq = l1gprs_tbf_pending_req_alloc(gprs, false, req->tbf_ref,
							    req->slotmask, start_fn);
			preq->dl_tfi = req->dl_tfi;
			l1gprs_add_tbf_pending_req(gprs, preq);
			return 0;
		}

		tbf = l1gprs_find_tbf(gprs, false, req->tbf_ref);
		if (tbf) {
			l1gprs_update_tbf(gprs, tbf, req->slotmask);
		} else {
			tbf = l1gprs_tbf_alloc(gprs, false, req->tbf_ref,
					       req->slotmask);
			tbf->dl_tfi = req->dl_tfi;
			l1gprs_register_tbf(gprs, tbf);
		}
	} else {
		tbf = l1gprs_find_tbf(gprs, false, req->tbf_ref);
		if (tbf == NULL) {
			LOGP_GPRS(gprs, LOGL_ERROR, "%s(): " LOG_TBF_FMT " not found\n",
				  __func__, 'D', req->tbf_ref);
			return -ENOENT;
		}
		l1gprs_unregister_tbf(gprs, tbf);
	}

	return 0;
}

int l1gprs_handle_ul_block_req(struct l1gprs_state *gprs,
			       struct l1gprs_prim_ul_block_req *req,
			       const struct msgb *msg)
{
	const struct l1ctl_gprs_ul_block_req *l1br = (void *)msg->l1h;
	const struct l1gprs_pdch *pdch = NULL;
	size_t data_len;
	uint32_t fn;

	OSMO_ASSERT(l1br != NULL);

	if (OSMO_UNLIKELY(msgb_l1len(msg) < sizeof(*l1br))) {
		LOGP_GPRS(gprs, LOGL_ERROR,
			  "Rx malformed UL BLOCK.req (len=%u < %zu)\n",
			  msgb_l1len(msg), sizeof(*l1br));
		return -EINVAL;
	}
	fn = ntohl(l1br->hdr.fn);
	if (OSMO_UNLIKELY(l1br->hdr.tn >= ARRAY_SIZE(gprs->pdch))) {
		LOGP_GPRS(gprs, LOGL_ERROR,
			  "Rx malformed UL BLOCK.req (fn=%u, tn=%u)\n",
			  fn, l1br->hdr.tn);
		return -EINVAL;
	}

	pdch = &gprs->pdch[l1br->hdr.tn];
	data_len = msgb_l1len(msg) - sizeof(*l1br);

	LOGP_PDCH(pdch, LOGL_DEBUG,
		  "Rx UL BLOCK.req (fn=%u, len=%zu): %s\n",
		  fn, data_len, osmo_hexdump(l1br->data, data_len));

	if ((pdch->ul_tbf_count == 0) && (pdch->dl_tbf_count == 0)) {
		LOGP_PDCH(pdch, LOGL_ERROR,
			  "Rx UL BLOCK.req (fn=%u, len=%zu), but this PDCH has no configured TBFs\n",
			  fn, data_len);
		return -EINVAL;
	}

	*req = (struct l1gprs_prim_ul_block_req) {
		.hdr = {
			.fn = fn,
			.tn = l1br->hdr.tn,
		},
		.data = &l1br->data[0],
		.data_len = data_len,
	};

	return 0;
}

/* Check if a Downlink block is a PTCCH/D (see 3GPP TS 45.002, table 6) */
#define BLOCK_IND_IS_PTCCH(ind) \
	(((ind)->hdr.fn % 104) == 12)

struct msgb *l1gprs_handle_dl_block_ind(struct l1gprs_state *gprs,
					const struct l1gprs_prim_dl_block_ind *ind)
{
	const struct l1gprs_pdch *pdch = NULL;
	struct l1ctl_gprs_dl_block_ind *l1bi;
	enum osmo_gprs_cs cs;
	struct msgb *msg;

	if (OSMO_UNLIKELY(ind->hdr.tn >= ARRAY_SIZE(gprs->pdch))) {
		LOGP_GPRS(gprs, LOGL_ERROR,
			  "Rx malformed DL BLOCK.ind (tn=%u)\n",
			  ind->hdr.tn);
		return NULL;
	}

	pdch = &gprs->pdch[ind->hdr.tn];

	LOGP_PDCH(pdch, LOGL_DEBUG,
		  "Rx DL BLOCK.ind (%s, fn=%u, len=%zu): %s\n",
		  BLOCK_IND_IS_PTCCH(ind) ? "PTCCH" : "PDTCH",
		  ind->hdr.fn, ind->data_len, osmo_hexdump(ind->data, ind->data_len));

	l1gprs_check_pending_tbfs(gprs, ind->hdr.fn);

	if (pdch->ul_tbf_count + pdch->dl_tbf_count == 0) {
		if (pdch->pending_ul_tbf_count + pdch->pending_dl_tbf_count > 0)
			LOGP_PDCH(pdch, LOGL_DEBUG,
				  "Rx DL BLOCK.ind (fn=%u), but this PDCH has no active TBFs yet\n",
				  ind->hdr.fn);
		else
			LOGP_PDCH(pdch, LOGL_ERROR,
				  "Rx DL BLOCK.ind (fn=%u), but this PDCH has no configured TBFs\n",
				  ind->hdr.fn);
		return NULL;
	}

	msg = l1gprs_l1ctl_msgb_alloc(L1CTL_GPRS_DL_BLOCK_IND);
	if (OSMO_UNLIKELY(msg == NULL)) {
		LOGP_GPRS(gprs, LOGL_ERROR, "l1gprs_l1ctl_msgb_alloc() failed\n");
		return NULL;
	}

	l1bi = (void *)msgb_put(msg, sizeof(*l1bi));
	*l1bi = (struct l1ctl_gprs_dl_block_ind) {
		.hdr = {
			.fn = htonl(ind->hdr.fn),
			.tn = ind->hdr.tn,
		},
		.meas = {
			.ber10k = htons(ind->meas.ber10k),
			.ci_cb = htons(ind->meas.ci_cb),
			.rx_lev = ind->meas.rx_lev,
		},
		.usf = 0xff,
	};

	if (ind->data_len == 0)
		return msg;
	if (BLOCK_IND_IS_PTCCH(ind)) {
		memcpy(msgb_put(msg, ind->data_len), ind->data, ind->data_len);
		return msg;
	}

	cs = osmo_gprs_dl_cs_by_block_bytes(ind->data_len);
	switch (cs) {
	case OSMO_GPRS_CS1:
	case OSMO_GPRS_CS2:
	case OSMO_GPRS_CS3:
	case OSMO_GPRS_CS4:
		l1bi->usf = ind->data[0] & 0x07;
		/* Determine whether to include the payload or not */
		if (l1gprs_pdch_filter_dl_block(pdch, ind->data))
			memcpy(msgb_put(msg, ind->data_len), ind->data, ind->data_len);
		break;
	case OSMO_GPRS_CS_NONE:
		LOGP_PDCH(pdch, LOGL_ERROR,
			  "Failed to determine Coding Scheme (len=%zu)\n", ind->data_len);
		break;
	default:
		LOGP_PDCH(pdch, LOGL_NOTICE, "Coding Scheme %d is not supported\n", cs);
		break;
	}

	return msg;
}
