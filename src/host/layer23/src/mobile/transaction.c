/* GSM 04.07 Transaction handling */

/* (C) 2009 by Harald Welte <laforge@gnumonks.org>
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

#include <osmocom/core/talloc.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/msgb.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/transaction.h>

extern void *l23_ctx;

void _gsm48_cc_trans_free(struct gsm_trans *trans);
void _gsm480_ss_trans_free(struct gsm_trans *trans);
void _gsm411_sms_trans_free(struct gsm_trans *trans);

struct gsm_trans *trans_find_by_id(struct osmocom_ms *ms,
				   uint8_t proto, uint8_t trans_id)
{
	struct gsm_trans *trans;

	llist_for_each_entry(trans, &ms->trans_list, entry) {
		if (trans->protocol == proto &&
		    trans->transaction_id == trans_id)
			return trans;
	}
	return NULL;
}

struct gsm_trans *trans_find_by_callref(struct osmocom_ms *ms,
					uint32_t callref)
{
	struct gsm_trans *trans;

	llist_for_each_entry(trans, &ms->trans_list, entry) {
		if (trans->callref == callref)
			return trans;
	}
	return NULL;
}

struct gsm_trans *trans_alloc(struct osmocom_ms *ms,
			      uint8_t protocol, uint8_t trans_id,
			      uint32_t callref)
{
	struct gsm_trans *trans;

	trans = talloc_zero(l23_ctx, struct gsm_trans);
	if (!trans)
		return NULL;

	DEBUGP(DCC, "ms %s allocates transaction (proto %d trans_id %d "
		"callref %x mem %p)\n", ms->name, protocol, trans_id, callref,
		trans);

	trans->ms = ms;

	trans->protocol = protocol;
	trans->transaction_id = trans_id;
	trans->callref = callref;

	llist_add_tail(&trans->entry, &ms->trans_list);

	return trans;
}

void trans_free(struct gsm_trans *trans)
{
	switch (trans->protocol) {
	case GSM48_PDISC_CC:
		_gsm48_cc_trans_free(trans);
		break;
	case GSM48_PDISC_NC_SS:
		_gsm480_ss_trans_free(trans);
		break;
	case GSM48_PDISC_SMS:
		_gsm411_sms_trans_free(trans);
		break;
	}

	DEBUGP(DCC, "ms %s frees transaction (mem %p)\n", trans->ms->name,
		trans);

	llist_del(&trans->entry);

	talloc_free(trans);
}

/* allocate an unused transaction ID
 * in the given protocol using the ti_flag specified */
int trans_assign_trans_id(struct osmocom_ms *ms,
			  uint8_t protocol, uint8_t ti_flag)
{
	struct gsm_trans *trans;
	unsigned int used_tid_bitmask = 0;
	int i, j, h;

	if (ti_flag)
		ti_flag = 0x8;

	/* generate bitmask of already-used TIDs for this (proto) */
	llist_for_each_entry(trans, &ms->trans_list, entry) {
		if (trans->protocol != protocol ||
		    trans->transaction_id == 0xff)
			continue;
		used_tid_bitmask |= (1 << trans->transaction_id);
	}

	/* find a new one, trying to go in a 'circular' pattern */
	for (h = 6; h > 0; h--)
		if (used_tid_bitmask & (1 << (h | ti_flag)))
			break;
	for (i = 0; i < 7; i++) {
		j = ((h + i) % 7) | ti_flag;
		if ((used_tid_bitmask & (1 << j)) == 0)
			return j;
	}

	return -1;
}

