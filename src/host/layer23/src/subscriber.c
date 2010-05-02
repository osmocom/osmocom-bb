/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
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
#include <errno.h>
#include <string.h>
#include <osmocore/talloc.h>

#include <osmocom/logging.h>
#include <osmocom/osmocom_data.h>
#include <osmocom/networks.h>


int gsm_subscr_init(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subscr = &ms->subscr;

	memset(subscr, 0, sizeof(*subscr));
	subscr->ms = ms;

	/* set key invalid */
	subscr->key_seq = 7;

	/* init lists */
	INIT_LLIST_HEAD(&subscr->plmn_list);
	INIT_LLIST_HEAD(&subscr->plmn_na);

	return 0;
}

int gsm_subscr_exit(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct llist_head *lh, *lh2;

	/* flush lists */
	llist_for_each_safe(lh, lh2, &subscr->plmn_list) {
		llist_del(lh);
		talloc_free(lh);
	}
	llist_for_each_safe(lh, lh2, &subscr->plmn_na) {
		llist_del(lh);
		talloc_free(lh);
	}

	return 0;
}

/* Attach test card, no sim must be present */
int gsm_subscr_testcard(struct osmocom_ms *ms, int mcc, int mnc, char *msin)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *msg;

	if (subscr->sim_valid) {
		LOGP(DMM, LOGL_ERROR, "Cannot insert card, until current card "
			"is detached.\n");
		return -EBUSY;
	}

	if (strlen(msin) != 10) {
		LOGP(DMM, LOGL_ERROR, "MSIN '%s' Error.\n", msin);
		return -EINVAL;
	}

	/* reset subscriber */
	gsm_subscr_exit(ms);
	gsm_subscr_init(ms);

	sprintf(subscr->sim_name, "test");
	// TODO: load / save SIM to file system
	subscr->sim_valid = 1;
	subscr->ustate = GSM_SIM_U2_NOT_UPDATED;
	subscr->acc_barr = 1; /* we may access any barred cell */
	subscr->acc_class = 0xfbff; /* we have any access class */
	subscr->mcc = mcc;
	subscr->mnc = mnc;
	subscr->always_search_hplmn = 1;
	subscr->t6m_hplmn = 1; /* try to find home network every 6 min */
	snprintf(subscr->imsi, 15, "%03d%02d%s", mcc, mnc, msin);

	LOGP(DMM, LOGL_INFO, "(ms %s) Inserting test card (mnc=%d mnc=%d "
		"(%s, %s) imsi=%s)\n", ms->name, mcc, mnc, gsm_get_mcc(mcc),
		gsm_get_mnc(mnc, mnc), subscr->imsi);

	/* insert card */
	msg = gsm48_mmr_msgb_alloc(GSM48_MMR_REG_REQ);
	if (!msg)
		return -ENOMEM;
	gsm48_mmr_downmsg(ms, msg);

	return 0;
}

/* Detach card */
int gsm_subscr_remove(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *msg;

	if (!subscr->sim_valid) {
		LOGP(DMM, LOGL_ERROR, "Cannot remove card, no card present\n");
		return -EINVAL;
	}

	/* remove card */
	msg = gsm48_mmr_msgb_alloc(GSM48_MMR_NREG_REQ);
	if (!msg)
		return -ENOMEM;
	gsm48_mmr_downmsg(ms, msg);

	return 0;
}

static const char *subscr_ustate_names[] = {
	"U0_NULL",
	"U1_UPDATED",
	"U2_NOT_UPDATED",
	"U3_ROAMING_NA"
};

/* change to new U state */
void new_sim_ustate(struct gsm_subscriber *subscr, int state)
{
	LOGP(DMM, LOGL_INFO, "(ms %s) new state %s -> %s\n", subscr->ms->name,
		subscr_ustate_names[subscr->ustate],
		subscr_ustate_names[state]);

	subscr->ustate = state;
}

