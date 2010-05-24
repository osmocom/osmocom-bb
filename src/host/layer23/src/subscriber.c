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

void *l23_ctx;

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
int gsm_subscr_testcard(struct osmocom_ms *ms)
{
	struct gsm_settings *set = &ms->settings;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *msg;
	uint16_t mcc, mnc;
	char *error;

	if (subscr->sim_valid) {
		LOGP(DMM, LOGL_ERROR, "Cannot insert card, until current card "
			"is detached.\n");
		return -EBUSY;
	}

	error = gsm_check_imsi(set->test_imsi, &mcc, &mnc);
	if (error) {
		LOGP(DMM, LOGL_ERROR, "%s\n", error);
		return -EINVAL;
	}

	/* reset subscriber */
	gsm_subscr_exit(ms);
	gsm_subscr_init(ms);

	sprintf(subscr->sim_name, "test");
	// TODO: load / save SIM to file system
	subscr->sim_valid = 1;
	subscr->ustate = GSM_SIM_U2_NOT_UPDATED;
	subscr->acc_barr = set->test_barr; /* we may access barred cell */
	subscr->acc_class = 0xffff; /* we have any access class */
	subscr->mcc = mcc;
	subscr->mnc = mnc;
	subscr->plmn_valid = set->test_rplmn_valid;
	subscr->plmn_mcc = set->test_rplmn_mcc;
	subscr->plmn_mnc = set->test_rplmn_mnc;
	subscr->always_search_hplmn = set->test_always;
	subscr->t6m_hplmn = 1; /* try to find home network every 6 min */
	strcpy(subscr->imsi, set->test_imsi);

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

/* del forbidden PLMN */
int gsm_subscr_del_forbidden_plmn(struct gsm_subscriber *subscr, uint16_t mcc,
	uint16_t mnc)
{
	struct gsm_sub_plmn_na *na;

	llist_for_each_entry(na, &subscr->plmn_na, entry) {
		if (na->mcc == mcc && na->mnc == mnc) {
			LOGP(DPLMN, LOGL_INFO, "Delete from list of forbidden "
				"PLMNs (mcc=%03d, mnc=%02d)\n", mcc, mnc);
			llist_del(&na->entry);
			talloc_free(na);
#ifdef TODO
			update plmn not allowed list on sim
#endif
			return 0;
		}
	}

	return -EINVAL;
}

/* add forbidden PLMN */
int gsm_subscr_add_forbidden_plmn(struct gsm_subscriber *subscr, uint16_t mcc,
					uint16_t mnc, uint8_t cause)
{
	struct gsm_sub_plmn_na *na;

	/* don't add Home PLMN */
	if (subscr->sim_valid && mcc == subscr->mcc && mnc == subscr->mnc)
		return -EINVAL;

	LOGP(DPLMN, LOGL_INFO, "Add to list of forbidden PLMNs "
		"(mcc=%03d, mnc=%02d)\n", mcc, mnc);
	na = talloc_zero(l23_ctx, struct gsm_sub_plmn_na);
	if (!na)
		return -ENOMEM;
	na->mcc = mcc;
	na->mnc = mnc;
	na->cause = cause;
	llist_add_tail(&na->entry, &subscr->plmn_na);

#ifdef TODO
	update plmn not allowed list on sim
#endif

	return 0;
}

/* search forbidden PLMN */
int gsm_subscr_is_forbidden_plmn(struct gsm_subscriber *subscr, uint16_t mcc,
					uint16_t mnc)
{
	struct gsm_sub_plmn_na *na;

	llist_for_each_entry(na, &subscr->plmn_na, entry) {
		if (na->mcc == mcc && na->mnc == mnc)
			return 1;
	}

	return 0;
}

/* dump subscriber */
void gsm_subscr_dump(struct gsm_subscriber *subscr,
			void (*print)(void *, const char *, ...), void *priv)
{
	int i;
	struct gsm_sub_plmn_list *plmn_list;
	struct gsm_sub_plmn_na *plmn_na;

	print(priv, "Mobile Subscriber of MS '%s':\n", subscr->ms->name);

	if (!subscr->sim_valid) {
		print(priv, " No SIM present.\n");
		return;
	}

	print(priv, " IMSI: %s  MCC %d  MNC %d  (%s, %s)\n", subscr->imsi,
		subscr->mcc, subscr->mnc, gsm_get_mcc(subscr->mcc),
		gsm_get_mnc(subscr->mcc, subscr->mnc));
	print(priv, " Status: %s  IMSI %s", subscr_ustate_names[subscr->ustate],
		(subscr->imsi_attached) ? "attached" : "detached");
	if (subscr->tmsi_valid)
		print(priv, "  TSMI  %08x", subscr->tmsi);
	if (subscr->lai_valid)
		print(priv, "  LAI: MCC %d  MNC %d  LAC 0x%04x  (%s, %s)\n",
			subscr->lai_mcc, subscr->lai_mnc, subscr->lai_lac,
			gsm_get_mcc(subscr->lai_mcc),
			gsm_get_mnc(subscr->lai_mcc, subscr->lai_mnc));
	else
		print(priv, "  LAI: invalid\n");
	if (subscr->key_seq != 7) {
		print(priv, " Key: sequence %d ");
		for (i = 0; i < sizeof(subscr->key); i++)
			print(priv, " %02x", subscr->key[i]);
		print(priv, "\n");
	}
	if (subscr->plmn_valid)
		print(priv, " Current PLMN: MCC %d  MNC %d  (%s, %s)\n",
			subscr->plmn_mcc, subscr->plmn_mnc,
			gsm_get_mcc(subscr->plmn_mcc),
			gsm_get_mnc(subscr->plmn_mcc, subscr->plmn_mnc));
	print(priv, " Access barred cells: %s\n",
		(subscr->acc_barr) ? "yes" : "no");
	print(priv, " Access classes:");
	for (i = 0; i < 16; i++)
		if ((subscr->acc_class & (1 << i)))
			print(priv, " C%d", i);
	print(priv, "\n");
	if (!llist_empty(&subscr->plmn_list)) {
		print(priv, " List of preferred PLMNs:\n");
		print(priv, "        MCC    |MNC\n");
		print(priv, "        -------+-------\n");
		llist_for_each_entry(plmn_list, &subscr->plmn_list, entry)
			print(priv, "        %03d    |%02d\n", plmn_list->mcc,
				plmn_list->mnc);
	}
	if (!llist_empty(&subscr->plmn_na)) {
		print(priv, " List of forbidden PLMNs:\n");
		print(priv, "        MCC    |MNC    |cause\n");
		print(priv, "        -------+-------+-------\n");
		llist_for_each_entry(plmn_na, &subscr->plmn_na, entry)
			print(priv, "        %03d    |%02d     |#%d\n",
				plmn_na->mcc, plmn_na->mnc, plmn_na->cause);
	}
}

char *gsm_check_imsi(const char *imsi, uint16_t *mcc, uint16_t *mnc)
{
	int i;

	if (!imsi || strlen(imsi) != 15)
		return "IMSI must have 15 digits!";

	for (i = 0; i < strlen(imsi); i++) {
		if (imsi[i] < '0' || imsi[i] > '9')
			return "IMSI must have digits 0 to 9 only!";
	}

	*mcc = imsi[0] * 100 + imsi[1] * 10 + imsi[2] - 111 * '0';
	*mnc = imsi[3] * 10 + imsi[4] - 11 * '0';

	return NULL;
}


