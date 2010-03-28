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

/*
 * initialization
 */

/* initialize the idle mode process */
gsm322_init(struct osmocom_ms *ms)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm322_locreg *lr = &ms->locreg;
	OSMOCOM_FILE *fp;
	struct msgb *nmsg;
	char suffix[] = ".plmn"
	char filename[sizeof(ms->name) + strlen(suffix) + 1];

	memset(plmn, 0, sizeof(*plmn));

	plmn->ms = ms;

	/* set initial state */
	plmn->state = 0;
	cs->state = 0;
	lr->state = 0;
	plmn->mode = PLMN_MODE_AUTO;

	/* init lists */
	INIT_LLIST_HEAD(&plmn->event_queue);
	INIT_LLIST_HEAD(&plmn->nplmn_list);
	INIT_LLIST_HEAD(&plmn->splmn_list);
	INIT_LLIST_HEAD(&plmn->la_list);
	INIT_LLIST_HEAD(&plmn->ba_list);

	/* read PLMN list */
	strcpy(filename, ms->name);
	strcat(filename, suffix);
	fp = osmocom_fopen(filename, "r");
	if (fp) {
		** read list
		osmocom_close(fp);
	}

	/* enqueue power on message */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_SWITCH_ON);
	if (!nmsg)
		return -ENOMEM;
	gsm322_sendmsg(ms, nmsg);
}

/*
 * event messages
 */

/* allocate a 03.22 event message */
static struct msgb *gsm322_msgb_alloc(int msg_type)
{
	struct msgb *msg;
	struct gsm322_msg *gm;

	msg = msgb_alloc_headroom(GSM322_ALLOC_SIZE, GSM322_ALLOC_HEADROOM,
		"GSM 03.22");
	if (!msg)
		return NULL;

	gm = (struct gsm322_msg *)msgb_put(msg, sizeof(*gm));
	gm->msg_type = msg_type;
	return msg;
}

/* queue received message */
int gsm322_sendmsg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	msgb_enqueue(&plmn->event_queue, msg);
}

/*
 * support
 */

/* search for PLMN in BA list */
static struct gsm322_ba_list *gsm322_find_ba_list(struct gsm322_plmn *plmn, uint16_t mcc, uint16_t mnc)
{
	struct gsm322_ba_list *ba, *ba_found = NULL;

	/* search for BA list */
	llist_for_each_entry(ba, &plmn->ba_list, entry) {
		if (ba->mcc == mcc
		 && ba->mnc == mnc) {
		 	ba_found = ba;
			break;
		}
	}

	return ba_found;
}

/*
 * state change
 */

/* new automatic PLMN search state */
static void new_a_state(struct gsm322_plmn *plmn, int state)
{
	if (state < 0 || state >= (sizeof(plmn_a_state_names) / sizeof(char *)))
		return;

	DEBUGP(DRR, "new state %s -> %s\n",
		plmn_a_state_names[rr->state], plmn_a_state_names[state]);

	plmn->state = state;
}

/* new manual PLMN search state */
static void new_m_state(struct gsm322_plmn *plmn, int state)
{
	if (state < 0 || state >= (sizeof(plmn_m_state_names) / sizeof(char *)))
		return;

	DEBUGP(DRR, "new state %s -> %s\n",
		plmn_m_state_names[rr->state], plmn_m_state_names[state]);

	plmn->state = state;
}

/* new Cell selection state */
static void new_c_state(struct gsm322_plmn *plmn, int state)
{
	if (state < 0 || state >= (sizeof(cellsel_state_names) / sizeof(char *)))
		return;

	DEBUGP(DRR, "new state %s -> %s\n",
		cellsel_state_names[rr->state], cellsel_state_names[state]);

	plmn->state = state;
}

/* new Location registration state */
static void new_l_state(struct gsm322_plmn *plmn, int state)
{
	if (state < 0 || state >= (sizeof(locreg_state_names) / sizeof(char *)))
		return;

	DEBUGP(DRR, "new state %s -> %s\n",
		locreg_state_names[rr->state], locreg_state_names[state]);

	plmn->state = state;
}

/*
 * timer
 */

static void gsm322_timer_timeout(void *arg)
{
	struct gsm322_plmn *plmn = arg;
	struct msgb *nmsg;

	/* indicate PLMN selection T timeout */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_HPLMN_SEARCH);
	if (!nmsg)
		return -ENOMEM;
	gsm322_sendmsg(ms, nmsg);
}

static void gsm322_timer_start(struct gsm322_plmn *plmn, int secs)
{
	DEBUGP(DRR, "starting HPLMN search timer with %d minutes\n", secs / 60);
	plmn->timer.cb = gsm322_timer_timeout;
	plmn->timer.data = plmn;
	bsc_schedule_timer(&plmn->timer, secs, 0);
}

static void gsm322_timer_stop(struct gsm322_plmn *plmn)
{
	if (timer_pending(&plmn->timer)) {
		DEBUGP(DRR, "stopping pending timer\n");
		bsc_del_timer(&plmn->timer);
	}
}

/*
 * sort list of PLMNs
 */

/* sort the list by priority */
static int gsm322_sort_list(struct osmocom_ms *ms)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_plmn_list *plmn_entry, *sim_entry;
	struct gsm322_plmn_list *plmn_found;
	struct llist_head *lh, lh2;
	int i = 0;

	/* clear sorted list */
	llist_for_each_safe(lh, lh2, &plmn->splmn_list) {
		llist_del(lh);
		talloc_free(lh);
	}

	/* move Home PLMN */
	if (subscr->imsi) {
		plmn_found = NULL;
		llist_for_each_entry(plmn_entry, &plmn->nplmn_list, entry) {
			if (plmn_entry->mcc == subscr->mcc
			 && plmn_entry->mnc == subscr->mnc) {
			 	plmn_found = plmn_entry;
				break;
			}
		}
		if (plmn_found) {
			llist_del(&plmn_found->entry);
			llist_add_tail(&plmn_found->entry, &plmn->splmn_list);
		}
	}

	/* move entries in SIM list */
	llist_for_each_entry(sim_entry, &subscr->nplmn_list, entry) {
		plmn_found = NULL;
		llist_for_each_entry(plmn_entry, &plmn->nplmn_list, entry) {
			if (plmn_entry->mcc == sim_entry->mcc
			 && plmn_entry->mnc == sim_entry->mnc) {
			 	plmn_found = plmn_entry;
				break;
			}
		}
		if (plmn_found) {
			llist_del(&plmn_found->entry);
			llist_add_tail(&plmn_found->entry, &plmn->splmn_list);
		}
	}

	/* move PLMN above -85 dBm in random order */
	entries = 0;
	llist_for_each_entry(plmn_entry, &plmn->nplmn_list, entry) {
		if (plmn_entry->rxleveldbm > -85)
			entries++;
	}
	if (entries)
		move = random() % entries;
	while(entries) {
		i = 0;
		llist_for_each_entry(plmn_entry, &plmn->nplmn_list, entry) {
			if (plmn_entry->rxleveldbm > -85) {
				if (i == move) {
					llist_del(&plmn_found->entry);
					llist_add_tail(&plmn_found->entry,
						&plmn->splmn_list);
					break;
				}
				i++;
			}
		}
		entries--;
	}

	/* move PLMN below -85 dBm in decreasing order */
	while(1) {
		plmn_found = NULL;
		llist_for_each_entry(plmn_entry, &plmn->nplmn_list, entry) {
			if (!plmn_found
			 || plmn_entry->rxleveldbm > search_db) {
			 	search_db = plmn_entry->rxleveldbm;
				plmn_found = plmn_entry;
			}
		}
		if (!plmn_found)
			break;
		llist_del(&plmn_found->entry);
		llist_add_tail(&plmn_found->entry, &plmn->splmn_list);
	}

	return 0;
}

/*
 * handler for automatic search
 */

/* go On PLMN state */
static int gsm322_a_go_on_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;

#ifdef TODO
	indicate selected plmn to user
#endif

	new_a_state(plmn, GSM_A2_ON_PLMN);

	/* start timer, if on VPLMN of home country */
	if (plmn->mcc == subscr->mcc
	 && plmn->mcc != subscr->mnc) {
	 	if (subscr->sim_valid && subscr->sim_t6m_hplmn)
		 	gsm322_timer_start(plmn, subscr->sim_t6m_hplmn * 360);
		else
		 	gsm322_timer_start(plmn, 30 * 360);
	} else
		gsm322_timer_stop(plmn);

	/* indicate On PLMN */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_ON_PLMN);
	if (!nmsg)
		return -ENOMEM;
	gsm322_sendmsg(ms, nmsg);

	return 0;
}

/* indicate selected PLMN */
static int gsm322_a_indicate_selected(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

#ifdef TODO
	indicate selected plmn to user
#endif

	return gsm322_a_go_on_plmn(ms, msg);
}

/* select first PLMN in list */
static int gsm322_a_sel_first_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;
	struct gsm322_plmn_list *plmn_entry;

	/* if no PLMN in list */
	if (llist_empty(&plmn->splmn_list)) {
		gsm322_a_no_more_plmn(ms, msg);
		return 0;
	}

	/* select first entry */
	plmn->plmn_curr = 0;
	plmn_entry = llist_entry(plmn->splmn_list.next, struct gsm322_plmn_list, entry);

	/* select first PLMN in list */
	plmn->mcc = plmn_entry->mcc;
	plmn->mnc = plmn_entry->mnc;

	new_a_state(plmn, GSM_A3_TRYING_PLMN);

	/* indicate New PLMN */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
	if (!nmsg)
		return -ENOMEM;
	gsm322_sendmsg(ms, nmsg);

	return 0;
}

/* select next PLMN in list */
static int gsm322_a_sel_next_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;
	struct gsm322_plmn_list *plmn_entry;
	struct gsm322_plmn_list *plmn_next = NULL;
	int i;

	/* select next entry from list */
	plmn->plmn_curr++;
	i = 0;
	llist_for_each_entry(plmn_entry, &plmn->splmn_list, entry) {
		if (i == plmn_curr) {
			plmn_next = plmn_entry;
			break;
		i++;
	}

	/* if no more PLMN in list */
	if (!plmn_next) {
		gsm322_a_no_more_plmn(ms, msg);
		return 0;
	}

	/* select next PLMN in list */
	plmn->mcc = plmn_next->mcc;
	plmn->mnc = plmn_next->mnc;

	new_a_state(plmn, GSM_A3_TRYING_PLMN);

	/* indicate New PLMN */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
	if (!nmsg)
		return -ENOMEM;
	gsm322_sendmsg(ms, nmsg);

	return 0;
}

/* User re-selection event */
static int gsm322_a_user_reselection(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;
	struct gsm322_plmn_list *plmn_entry;
	struct gsm322_plmn_list *plmn_found = NULL;

	/* search current PLMN in list */
	llist_for_each_entry(plmn_entry, &plmn->splmn_list, entry) {
		if (plmn_entry->mcc == plmn->mcc
		 && lmn_entry->mnc == plmn->mnc)
			plmn_found = plmn_entry;
			break;
	}

	/* abort if list is empty */
	if (!plmn_found)
		return 0;

	/* move entry to end of list */
	llist_del(&plmn_found->entry);
	llist_add_tail(&plmn_found->entry, &plmn->splmn_list);

	/* select first PLMN in list */
	return gsm322_a_sel_first_plmn(ms, msg);
}

/* PLMN becomes available */
static int gsm322_a_plmn_avail(struct osmocom_ms *ms, struct msgb *msg)
{
	** must be available
	** must be registered
	** must be allowable

	if ** availale plmn is rplmn {
		/* select first PLMN in list */
		return gsm322_a_sel_first_plmn(ms, msg);
	} else {
		/* go On PLMN */
		return gsm322_a_go_on_plmn(ms, msg);
	}
}
		
/* no (more) PLMN in list */
static int gsm322_a_no_more_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_plmn_list *plmn_entry;

	/* if no PLMN in list */
	if (llist_empty(&plmn->splmn_list)) {
		if (subscr->plmn_valid) {
			plmn->mcc = subscr->plmn_mcc;
			plmn->mnc = subscr->plmn_mnc;
		} else if (subscr->sim_valid) {
			plmn->mcc = subscr->mcc;
			plmn->mnc = subscr->mnc;
		} else
			plmn->mcc = plmn->mnc = 0;

		new_a_state(plmn, GSM_A4_WAIT_FOR_PLMN);

** do we have to indicate?:
		/* indicate New PLMN */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
		if (!nmsg)
			return -ENOMEM;
		gsm322_sendmsg(ms, nmsg);

		return 0;
	}

	/* select first PLMN in list */
	plmn_entry = llist_entry(plmn->splmn_list.next, struct gsm322_plmn_list, entry);
	plmn->mcc = plmn_entry->mcc;
	plmn->mnc = plmn_entry->mnc;

	/* indicate New PLMN */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
	if (!nmsg)
		return -ENOMEM;
	gsm322_sendmsg(ms, nmsg);

	/* go On PLMN */
	return gsm322_a_indicate_selected(ms, msg);
}

/* loss of radio coverage */
static int gsm322_a_loss_of_radio(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_plmn_list *plmn_entry;

	/* if PLMN in list */
	if (!llist_empty(&plmn->splmn_list))
		return gsm322_a_sel_first_plmn(ms, msg);

	plmn->mcc = plmn->mnc = 0;

	new_a_state(plmn, GSM_A4_WAIT_FOR_PLMN);

	return 0;
}

/* flush list of forbidden LAs */
static int gsm322_flush_forbidden_la(struct osmocom_ms *ms)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct llist_head *lh, lh2;

	/* clear sorted list */
	llist_for_each_safe(lh, lh2, &plmn->forbidden_list) {
		llist_del(lh);
		talloc_free(lh);
	}

	return 0;
}

/* del forbidden PLMN */
static int gsm322_del_forbidden_plmn(struct osmocom_ms *ms, uint16_t mcc,
	uint16_t mnc)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm_plmn_na *na;

	llist_for_each(na, &plmn->forbidden_list, entry) {
		if (na->mcc = mcc && na->mnc = mnc) {
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
static int gsm322_add_forbidden_plmn(struct osmocom_ms *ms, uint16_t mcc,
	uint16_t mnc)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm_plmn_na *na;

	/* don't add Home PLMN */
	if (subscr->sim_valid && mcc == subscr->mcc && mnc == subscr->mnc)
		return -EINVAL;

	na = talloc_zero(NULL, struct gsm_plmn_na);
	if (!na)
		return -ENOMEM;
	na->mcc = mcc;
	na->mnc = mnc;
	llist_add_tail(na, &subscr->plmn_na);

#ifdef TODO
	update plmn not allowed list on sim
#endif

	return 0;
}

/* add forbidden LA */
static int gsm322_add_forbidden_la(struct osmocom_ms *ms, uint16_t mcc,
	uint16_t mnc, uint16_t lac)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_la_list *la;

	la = talloc_zero(NULL, struct gsm322_la_list);
	if (!la)
		return -ENOMEM;
	la->mcc = mcc;
	la->mnc = mnc;
	la->lac = lac;
	llist_add_tail(la, &plmn->forbidden_list);

	return 0;
}

/* MS is switched on OR SIM is inserted OR removed */
static int gsm322_a_switch_on(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscr *subscr = &ms->subscr;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;

	if (!subscr->sim_valid) {
		new_a_state(plmn, GSM_A6_NO_SIM);

		return 0;
	}

	/* if there is a registered PLMN */
	if (subscr->plmn_valid) {
		/* select the registered PLMN */
		plmn->mcc = subscr->plmn_mcc;
		plmn->mnc = subscr->plmn_mnc;

		new_a_state(plmn, GSM_A1_TRYING_RPLMN);

		/* indicate New PLMN */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
		if (!nmsg)
			return -ENOMEM;
		gsm322_sendmsg(ms, nmsg);
		return 0;
	}

	/* select first PLMN in list */
	return gsm322_a_sel_first_plmn(ms, msg);
}

/* MS is switched off */
static int gsm322_a_switch_off(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	gsm322_flush_forbidden_la(ms);

	gsm322_timer_stop(plmn);

	new_a_state(plmn, GSM_A0_NULL);

	return 0;
}

/* SIM is removed */
static int gsm322_a_sim_removed(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	gsm322_flush_forbidden_la(ms);

	gsm322_timer_stop(plmn);

	return gsm322_a_switch_on(ms, msg);
}

/* location update response: "Roaming not allowed" */
static int gsm322_a_lu_reject(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_hdr *gh = msgb->data;

	/* change state only if roaming is not allowed */
	if (gh->reject != GSM48_REJECT_ROAMING_NOT_ALLOWED)
		return 0;

	/* store in list of forbidden LAs */
	gsm322_add_forbidden_la(ms, ** current plmn: mcc mnc lac);

	return gsm322_a_sel_first_plmn(ms, msg);
}

/* On VPLMN of home country and timeout occurs */
static int gsm322_a_hplmn_search(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm48_rr *rr = &ms->rr;

	/* try again later, if not idle */
	if (rr->state != GSM48_RRSTATE_IDLE) {
		gsm322_timer_start(plmn, 60);

		return 0;
	}

	new_a_state(plmn, GSM_HPLMN_SEARCH);

	/* initiate search */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_HPLMN_SEAR);
	if (!nmsg)
		return -ENOMEM;
	gsm322_sendmsg(ms, nmsg);

	return 0;
}

/* manual mode selected */
static int gsm322_a_sel_manual(struct osmocom_ms *ms, struct msgb *msg)
{
	/* restart state machine */
	gsm322_a_switch_off(ms, msg);
	plmn->mode = PLMN_MODE_MANUAL;
	gsm322_m_switch_on(ms, msg);

	return 0;
}

/*
 * handler for manual search
 */

/* go Not on PLMN state */
static int gsm322_m_go_not_on_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	new_m_state(plmn, GSM_M3_NOT_ON_PLMN);

	return 0;
}

/* display PLMNs and to Not on PLMN */
static int gsm322_m_display_plmns(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

#ifdef TODO
	display PLMNs to user
#endif
	
	/* go Not on PLMN state */
	return gsm322_m_go_not_on_plmn(ms, msg);
}

/* MS is switched on OR SIM is inserted OR removed */
static int gsm322_m_switch_on(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscr *subscr = &ms->subscr;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;

	if (!subscr->sim_valid) {
		new_a_state(plmn, GSM_A8_NO_SIM);

		return 0;
	}

	/* if there is a registered PLMN */
	if (subscr->plmn_valid) {
		/* select the registered PLMN */
		plmn->mcc = subscr->plmn_mcc;
		plmn->mnc = subscr->plmn_mnc;

		new_m_state(plmn, GSM_M1_TRYING_RPLMN);

		/* indicate New PLMN */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
		if (!nmsg)
			return -ENOMEM;
		gsm322_sendmsg(ms, nmsg);
		return 0;
	}

	/* display PLMNs */
	return gsm322_m_display_plmns(ms, msg);
}

/* MS is switched off */
static int gsm322_m_switch_off(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	gsm322_flush_forbidden_la(ms);

	new_m_state(plmn, GSM_M0_NULL);

	return 0;
}

/* SIM is removed */
static int gsm322_m_sim_removed(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	gsm322_flush_forbidden_la(ms);

	return gsm322_m_switch_on(ms, msg);
}

/* go to On PLMN state */
static int gsm322_m_go_on_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;

	/* if selected PLMN is in list of forbidden PLMNs */
	gsm322_del_forbidden_plmn(ms, plmn->mcc, plmn->mnc);

	new_m_state(plmn, GSM_M2_ON_PLMN);

	/* indicate On PLMN */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_ON_PLMN);
	if (!nmsg)
		return -ENOMEM;
	gsm322_sendmsg(ms, nmsg);

	return 0;
}

/* indicate selected PLMN */
static int gsm322_m_indicate_selected(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

#ifdef TODO
	indicate selected plmn to user
#endif

	return gsm322_m_go_on_plmn(ms, msg);
}

/* previously selected PLMN becomes available again */
static int gsm322_m_plmn_avail(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	new_m_state(plmn, GSM_M1_TRYING_RPLMN);

	return 0;
}

/* the user has selected given PLMN */
static int gsm322_m_plmn_avail(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_msg *gm = (struct gsm322_msg *)msg->data;

	/* use user selection */
	plmn->mcc = gm->mcc;
	plmn->mnc = gm->mnc;

	new_m_state(plmn, GSM_M4_TRYING_PLMN);

	/* indicate New PLMN */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
	if (!nmsg)
		return -ENOMEM;
	gsm322_sendmsg(ms, nmsg);

	return 0;
}

/* auto mode selected */
static int gsm322_m_sel_auto(struct osmocom_ms *ms, struct msgb *msg)
{
	/* restart state machine */
	gsm322_m_switch_off(ms, msg);
	plmn->mode = PLMN_MODE_AUTO;
	gsm322_a_switch_on(ms, msg);

	return 0;
}

/*
 * handler for cell selection process
 */

/* start stored cell selection */
static int gsm322_c_stored_cell_sel(struct osmocom_ms *ms, struct gsm322_ba_list *ba)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;
	struct gsm58_msg *gm;

	new_c_state(plmn, GSM_C2_STORED_CELL_SEL);

	nmsg = gsm58_msgb_alloc(GSM58_EVENT_START_STORED);
	if (!nmsg)
		return -ENOMEM;
	gm = (struct gsm58_msg *)msg->data;

	memcpy(gm->ba, ba->freq, sizeof(gm->ba));
	gm->mcc = plmn->mcc;
	gm->mnc = plmn->mnc;

	gsm322_sendmsg(ms, nmsg);


	return 0;
}

/* start noraml cell selection */
static int gsm322_c_normal_cell_sel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;

	new_c_state(plmn, GSM_C1_NORMAL_CELL_SEL);

	nmsg = gsm58_msgb_alloc(GSM58_EVENT_START_NORMAL);
	if (!nmsg)
		return -ENOMEM;

	gm->mcc = plmn->mcc;
	gm->mnc = plmn->mnc;

	gsm322_sendmsg(ms, nmsg);

	return 0;
}

/* start any cell selection */
static int gsm322_c_any_cell_sel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;

	new_c_state(plmn, GSM_C6_ANY_CELL_SEL);

	nmsg = gsm58_msgb_alloc(GSM58_EVENT_START_ANY);
	if (!nmsg)
		return -ENOMEM;
	gsm322_sendmsg(ms, nmsg);

	return 0;
}

/* start noraml cell re-selection */
static int gsm322_c_normal_cell_resel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	new_c_state(plmn, GSM_C4_NORMAL_CELL_RSEL);

#ifdef TODO
	start cell selection process
	what will be the list?:
#endif

	return 0;
}

/* start any cell re-selection */
static int gsm322_c_any_cell_resel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	new_c_state(plmn, GSM_C8_ANY_CELL_RSEL);

#ifdef TODO
	start cell selection process
	what will be the list?:
#endif

	return 0;
}

/* start 'Choose cell' */
static int gsm322_c_choose_cell(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	new_c_state(plmn, GSM_C5_CHOOSE_CELL);

#ifdef TODO
	start cell selection process
	what will be the list?:
#endif

	return 0;
}

/* start 'Choose any cell' */
static int gsm322_c_choose_any_cell(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	new_c_state(plmn, GSM_C9_CHOOSE_ANY_CELL);

#ifdef TODO
	start cell selection process
	what will be the list?:
#endif

	return 0;
}

/* a new PLMN is selected by PLMN search process */
static int gsm322_c_new_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_ba_list *ba;

	/* search for BA list */
	ba = gsm322_find_ba_list(plmn, plmn->mcc, plmn->mnc);

	if (ba)
		return gsm322_c_stored_cell_sel(ms, ba);
	else
		return gsm322_c_normal_cell_sel(ms, msg);
}

/* a suitable cell was found, so we camp normally */
static int gsm322_c_camp_normally(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	new_c_state(plmn, GSM_C3_CAMPED_NORMALLY);

	return 0;
}

/* a not suitable cell was found, so we camp on any cell */
static int gsm322_c_camp_any_cell(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	new_c_state(plmn, GSM_C7_CAMPED_ANY_CELL);

	return 0;
}

/* location update reject */
static int gsm322_c_lu_reject(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_hdr *gh = msgb->data;

	/* not changing to Any Cell Selection */
	if (gh->reject != GSM48_REJECT_IMSI_UNKNOWN_IN_HLR
	 && gh->reject != GSM48_REJECT_ILLEGAL_MS
	 && gh->reject != GSM48_REJECT_ILLEGAL_ME
	 && gh->reject != GSM48_REJECT_PLMN_NOT_ALLOWED)

	return gsm322_c_any_cell_sel(ms, msg);
}

/* go connected mode */
static int gsm322_c_conn_mode_1(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	/* stop camping process */
	new_c_state(plmn, GSM_Cx_CONNECTED_MODE_1);

	return 0;
}

static int gsm322_c_conn_mode_2(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	/* stop camping process */
	new_c_state(plmn, GSM_Cx_CONNECTED_MODE_2);

	return 0;
}

/* switch on */
static int gsm322_c_switch_on(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_subscr *subscr = &ms->subscr;

	/* if no SIM is is MS */
	if (!subscr->sim_valid)
		return gsm322_c_any_cell_sel(ms, msg);

	/* stay in NULL state until PLMN is selected */

	return 0;
}

/*
 * state machines
 */

/* state machine for automatic PLMN selection events */
static struct plmnastatelist {
	uint32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} plmnastatelist[] = {
	{SBIT(GSM_A0_NULL),
	 GSM322_EVENT_SWITCH_ON, gsm322_a_switch_on},
	{ALL_STATES,
	 GSM322_EVENT_SWITCH_OFF, gsm322_a_switch_off},
	{SBIT(GSM_A6_NO_SIM),
	 GSM322_EVENT_SIM_INSERT, gsm322_a_switch_on},
	{ALL_STATES,
	 GSM322_EVENT_SIM_REMOVE, gsm322_a_sim_removed},
	{SBIT(GSM_A1_TRYING_RPLMN),
	 GSM322_EVENT_REG_FAILUE, gsm322_a_sel_first_plmn},
	{SBIT(GSM_A1_TRYING_RPLMN) | SBIT(GSM_A3_TRYING_PLMN),
	 GSM322_EVENT_REG_SUCC, gsm322_a_indicate_selected},
	{SBIT(GSM_A2_ON_PLMN),
	 GSM322_EVENT_LU_REJECT, gsm322_a_lu_reject},
	{SBIT(GSM_A2_ON_PLMN),
	 GSM322_EVENT_HPLMN_SEARCH, gsm322_a_hplmn_search},
	{SBIT(GSM_A2_ON_PLMN),
	 GSM322_EVENT_USER_RESEL, gsm322_a_user_reselection},
	{SBIT(GSM_A2_ON_PLMN),
	 GSM322_EVENT_LOS_RADIO, gsm322_a_los_of_radio},
	{SBIT(GSM_A3_TRYING_PLMN),
	 GSM322_EVENT_REG_FAILURE, gsm322_a_sel_next_plmn},
	{SBIT(GSM_HPLMN_SEARCH),
	 GSM322_EVENT_HPLMN_FOUN, gsm322_a_sel_first_plmn},
	{SBIT(GSM_HPLMN_SEARCH),
	 GSM322_EVENT_HPLMN_NOTF, gsm322_a_go_on_plmn},
	{SBIT(GSM_A4_WAIT_FOR_PLMN),
	 GSM322_EVENT_PLMN_AVAIL, gsm322_a_plmn_avail},
	{ALL_STATES,
	 GSM322_EVENT_SEL_MANUAL, gsm322_a_sel_manual},
};

#define PLMNASLLEN \
	(sizeof(plmnastatelist) / sizeof(struct plmnastatelist))

static int gsm322_a_event(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_hdr *gh = msgb->data;
	int msg_type = gh->msg_type;
	int rc;
	int i;

	DEBUGP(DRR, "(ms %s) Message '%s' for automatic PLMN selection in state "
		"%s\n", ms->name, gsm322_event_names[msg_type],
		plmn_a_state_names[plmn->state]);
	/* find function for current state and message */
	for (i = 0; i < PLMNASLLEN; i++)
		if ((msg_type == plmnastatelist[i].type)
		 && ((1 << plmn->state) & plmnastatelist[i].states))
			break;
	if (i == PLMNASLLEN) {
		DEBUGP(DRR, "Message unhandled at this state. (No error.)\n");
		return 0;
	}

	rc = plmnastatelist[i].rout(ms, msg);

	return rc;
}

/* state machine for manual PLMN selection events */
static struct plmnmstatelist {
	uint32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} plmnmstatelist[] = {
	{SBIT(GSM_M0_NULL),
	 GSM322_EVENT_SWITCH_ON, gsm322_m_switch_on},
	{ALL_STATES,
	 GSM322_EVENT_SWITCH_OFF, gsm322_m_switch_off},
	{SBIT(GSM_M5_NO_SIM),
	 GSM322_EVENT_SIM_INSERT, gsm322_m_switch_on},
	{ALL_STATES,
	 GSM322_EVENT_SIM_REMOVE, gsm322_m_sim_removed},
	{SBIT(GSM_M1_TRYING_RPLMN),
	 GSM322_EVENT_REG_FAILUE, gsm322_m_display_plmns},
	{SBIT(GSM_M1_TRYING_RPLMN),
	 GSM322_EVENT_REG_SUCC, gsm322_m_indicate_selected},
	{SBIT(GSM_M2_ON_PLMN),
	 GSM322_EVENT_ROAMING_NA, gsm322_m_display_plmns},
	{SBIT(GSM_M2_ON_PLMN) | SBIT(GSM_M4_TRYING_PLMN),
	 GSM322_EVENT_INVAL_SIM, gsm322_m_sim_removed},
	{SBIT(GSM_M2_ON_PLMN),
	 GSM322_EVENT_USER_RESEL, gsm322_m_display_plmns},
	{SBIT(GSM_M3_NOT_ON_PLMN),
	 GSM322_EVENT_PLMN_AVAIL, gsm322_m_plmn_avail},
	{SBIT(GSM_M3_NOT_ON_PLMN),
	 GSM322_EVENT_CHOSE_PLMN, gsm322_m_chose_plmn},
	{SBIT(GSM_M4_TRYING_PLMN),
	 GSM322_EVENT_REG_SUCC, gsm322_m_go_on_plmn},
	{SBIT(GSM_M4_TRYING_PLMN),
	 GSM322_EVENT_REG_FAILUE, gsm322_m_go_not_on_plmn},
	{ALL_STATES,
	 GSM322_EVENT_SEL_AUTO, gsm322_m_sel_auto},
};

#define PLMNMSLLEN \
	(sizeof(plmnmstatelist) / sizeof(struct plmnmstatelist))

static int gsm322_m_event(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_hdr *gh = msgb->data;
	int msg_type = gh->msg_type;
	int rc;
	int i;

	DEBUGP(DRR, "(ms %s) Message '%s' for manual PLMN selection in state "
		"%s\n", ms->name, gsm322_event_names[msg_type],
		plmn_m_state_names[plmn->state]);
	/* find function for current state and message */
	for (i = 0; i < PLMNMSLLEN; i++)
		if ((msg_type == plmnmstatelist[i].type)
		 && ((1 << plmn->state) & plmnmstatelist[i].states))
			break;
	if (i == PLMNMSLLEN) {
		DEBUGP(DRR, "Message unhandled at this state. (No error.)\n");
		return 0;
	}

	rc = plmnmstatelist[i].rout(ms, msg);

	return rc;
}

/* state machine for channel selection events */
static struct cellselstatelist {
	uint32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} cellselstatelist[] = {
	{ALL_STATES,
	 GSM322_EVENT_SWITCH_ON, gsm322_c_switch_on},
	{ALL_STATES,
	 GSM322_EVENT_SIM_REMOVE, gsm322_c_any_cell_sel},
	{SBIT(GSM_C6_ANY_CELL_SEL),
	 GSM322_EVENT_SIM_INSERT, gsm322_c_new_plmn},
	{ALL_STATES,
	 GSM322_EVENT_NEW_PLMN, gsm322_c_new_plmn},
	{SBIT(GSM_C1_NORMAL_CELL_SEL) | SBIT(GSM_C2_STORED_CELL_SEL) |
	 SBIT(GSM_C4_NORMAL_CELL_RSEL) | SBIT(GSM_C5_CHOOSE_CELL),
	 GSM322_EVENT_CELL_FOUND, gsm322_c_camp_normally},
	{SBIT(GSM_C9_CHOOSE_ANY_CELL) | SBIT(GSM_C6_ANY_CELL_SEL) |
	 SBIT(GSM_C4_NORMAL_CELL_RSEL),
	 GSM322_EVENT_CELL_FOUND, gsm322_c_camp_any_cell},
	{SBIT(GSM_C1_NORMAL_CELL_SEL),
	 GSM322_EVENT_NO_CELL_F, gsm322_c_any_cell_sel},
	{SBIT(GSM_C9_CHOOSE_ANY_CELL) | SBIT(GSM_C8_ANY_CELL_RSEL),
	 GSM322_EVENT_NO_CELL_F, gsm322_c_any_cell_sel},
	{SBIT(GSM_C2_STORED_CELL_SEL) | SBIT(GSM_C5_CHOOSE_CELL) |
	 SBIT(GSM_C4_NORMAL_CELL_RSEL),
	 GSM322_EVENT_NO_CELL_F, gsm322_c_normal_cell_sel},
	{SBIT(GSM_C3_CAMPED_NORMALLY),
	 GSM322_EVENT_LU_REJECT, gsm322_c_lu_reject},
	{SBIT(GSM_C3_CAMPED_NORMALLY),
	 GSM322_EVENT_LEAVE_IDLE, gsm322_c_conn_mode_1},
	{SBIT(GSM_C7_CAMPED_ANY_CELL),
	 GSM322_EVENT_LEAVE_IDLE, gsm322_c_conn_mode_2},
	{SBIT(GSM_Cx_CONNECTED_MODE_1),
	 GSM322_EVENT_RET_IDLE, gsm322_c_choose_cell},
	{SBIT(GSM_Cx_CONNECTED_MODE_2),
	 GSM322_EVENT_RET_IDLE, gsm322_c_choose_any_cell},
	{SBIT(GSM_C3_CAMPED_NORMALLY),
	 GSM322_EVENT_CELL_RESEL, gsm322_c_normal_cell_resel},
	{SBIT(GSM_C7_CAMPED_ANY_CELL),
	 GSM322_EVENT_CELL_RESEL, gsm322_c_any_cell_resel},
	{SBIT(GSM_C7_CAMPED_ANY_CELL),
	 GSM322_EVENT_CELL_FOUND, gsm322_c_normal_cell_sel},
};

#define CELLSELSLLEN \
	(sizeof(cellselstatelist) / sizeof(struct cellselstatelist))

static int gsm322_c_event(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm322_hdr *gh = msgb->data;
	int msg_type = gh->msg_type;
	int rc;
	int i;

	DEBUGP(DRR, "(ms %s) Message '%s' for Cell selection in state "
		"%s\n", ms->name, gsm322_event_names[msg_type],
		cellsel_state_names[cs->state]);
	/* find function for current state and message */
	for (i = 0; i < CELLSELSLLEN; i++)
		if ((msg_type == cellselstatelist[i].type)
		 && ((1 << cs->state) & cellselstatelist[i].states))
			break;
	if (i == CELLSELSLLEN) {
		DEBUGP(DRR, "Message unhandled at this state. (No error.)\n");
		return 0;
	}

	rc = cellselstatelist[i].rout(ms, msg);

	return rc;
}

/* broadcast event to all GSM 03.22 processes */
static int gsm322_event(struct osmocom_ms *ms, struct msgb *msg)
{
	/* send event to PLMN search process */
	if (plmn->mode == PLMN_MODE_AUTO)
		gsm322_a_event(ms, msg);
	else
		gsm322_m_event(ms, msg);

	/* send event to cell selection process */
	gsm322_c_event(ms, msg);

	/* send event to location registration process */
	gsm322_l_event(ms, msg);

	return 0;
}

/* dequeue GSM 03.22 events */
int gsm322_event_queue(struct osmocom_ms *ms)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *msg;
	int work = 0;
	
	while ((msg = msgb_dequeue(&plmn->event_queue))) {
		gsm322_event(ms, msg);
		free_msgb(msg);
		work = 1; /* work done */
	}
	
	return work;
}


finished
------------------------------------------------------------------------------
unfinished

unsolved issues:
- now to handle change in mode (manual / auto)
- available and allowable
- when do we have a new list, when to sort

todo:
 handle not suitable cells, forbidden cells, barred cells....



/* LR request */
static int gsm322_l_lr_request(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscr *subscr = &ms->subscr;
	struct gsm322_locreg *lr = &ms->locreg;
	struct msgb *nmsg;

	new_l_state(plmn, GSM_Lx_LR_PENDING);a

	start location update...
}

/* go into Updated state */
static int gsm322_l_go_updated(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscr *subscr = &ms->subscr;
	struct gsm322_locreg *lr = &ms->locreg;
	struct msgb *nmsg;

	new_l_state(plmn, GSM_L1_UPDATED);
}

/* SIM is removed */
static int gsm322_l_sim_remove(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscr *subscr = &ms->subscr;
	struct gsm322_locreg *lr = &ms->locreg;
	struct msgb *nmsg;

	new_l_state(plmn, GSM_L2_IDLE_NO_SIM);
}

/* MS is switched on */
static int gsm322_l_switch_on(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscr *subscr = &ms->subscr;
	struct gsm322_locreg *lr = &ms->locreg;
	struct msgb *nmsg;

	/* Switch on, No SIM */
	if (!subscr->sim_valid)
		return gsm322_l_sim_remove(ms, msg);

	/* perform LR, only if att flag indicates 'allowed' */
	if (subscr->att_allowed)
		return gsm322_l_lr_request(ms, msg);
	else
		return gsm322_l_go_updated(ms, msg);
}


/* state machine for location registration selection events */
static struct locstatelist {
	uint32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} locstatelist[] = {
	{SBIT(GSM_L0_NULL),
	 GSM322_EVENT_SWITCH_ON, gsm322_l_switch_on},
};

#define LOCSLLEN \
	(sizeof(locstatelist) / sizeof(struct locstatelist))

static int gsm322_l_event(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_locreg *lr = &ms->locreg;
	struct gsm322_hdr *gh = msgb->data;
	int msg_type = gh->msg_type;
	int rc;
	int i;

	DEBUGP(DRR, "(ms %s) Message '%s' for Location registration in state "
		"%s\n", ms->name, gsm322_event_names[msg_type],
		locrec_state_names[lr->state]);
	/* find function for current state and message */
	for (i = 0; i < LOCSLLEN; i++)
		if ((msg_type == locstatelist[i].type)
		 && ((1 << lr->state) & locstatelist[i].states))
			break;
	if (i == LOCSLLEN) {
		DEBUGP(DRR, "Message unhandled at this state. (No error.)\n");
		return 0;
	}

	rc = locstatelist[i].rout(ms, msg);

	return rc;
}

