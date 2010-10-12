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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <osmocore/msgb.h>
#include <osmocore/talloc.h>
#include <osmocore/utils.h>
#include <osmocore/gsm48.h>
#include <osmocore/signal.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/mobile/vty.h>

extern void *l23_ctx;

static void gsm322_cs_timeout(void *arg);
static void gsm322_cs_loss(void *arg);
static int gsm322_cs_select(struct osmocom_ms *ms, int any, int plmn_allowed);
static int gsm322_m_switch_on(struct osmocom_ms *ms, struct msgb *msg);

#define SKIP_MAX_PER_BAND

#warning HACKING!!!
int hack;

/*
 * notes
 */

/* Cell selection process
 *
 * The process depends on states and events (finites state machine).
 *
 * During states of cell selection or cell re-selection, the search for a cell
 * is performed in two steps:
 *
 *  1. Measurement of received level of all relevant frequencies (rx-lev)
 *
 *  2. Receive system information messages of all relevant frequencies
 *
 * During this process, the results are stored in a list of all frequencies.
 * This list is checked whenever a cell is selected. It depends on the results
 * if the cell is 'suitable' and 'allowable' to 'camp' on.
 *
 * This list is also used to generate a list of available networks.
 *
 * The states are:
 *
 * - cs->list[0..1023].xxx for each cell, where
 *  - flags and rxlev are used to store outcome of cell scanning process
 *  - sysinfo pointing to sysinfo memory, allocated temporarily
 * - cs->selected and cs->sel_* states of the current / last selected cell.
 *
 *
 * There is a special state: GSM322_PLMN_SEARCH
 * It is used to search for all cells, to find the HPLMN. This is triggered
 * by a timer. Also it is used before selecting PLMN from list.
 *
 */

/* PLMN selection process
 *
 * The PLMN (Public Land Mobile Network = Operator's Network) has two different
 * search processes:
 *
 *  1. Automatic search
 *
 *  2. Manual search
 *
 * The process depends on states and events (finites state machine).
 *
 */

/* File format of BA list:
 *
 * uint16_t	mcc
 * uint16_t	mcc
 * uint8_t	freq[128];
 * 	where frequency 0 is bit 0 of first byte
 * 
 * If not end-of-file, the next BA list is stored.
 */

/* List of lists:
 *
 * * subscr->plmn_list
 *
 * The "PLMN Selector list" stores prefered networks to select during PLMN
 * search process. This list is also stored in the SIM.
 *
 * * subscr->plmn_na
 *
 * The "forbidden PLMNs" list stores all networks that rejected us. The stored
 * network will not be used when searching PLMN automatically. This list is
 * also stored din the SIM.
 *
 * * plmn->forbidden_la
 *
 * The "forbidden LAs for roaming" list stores all location areas where roaming
 * was not allowed.
 *
 * * cs->list[1024]
 *
 * This list stores measurements and cell informations during cell selection
 * process. It can be used to speed up repeated cell selection.
 *
 * * cs->ba_list
 *
 * This list stores a map of frequencies used for a PLMN. If this lists exists
 * for a PLMN, it helps to speedup cell scan process.
 *
 * * plmn->sorted_plmn
 *
 * This list is generated whenever a PLMN search is started and a list of PLMNs
 * is required. It consists of home PLMN, PLMN Selector list, and PLMNs found
 * during scan process.
 */

/*
 * event messages
 */

static const struct value_string gsm322_event_names[] = {
	{ GSM322_EVENT_SWITCH_ON,	"EVENT_SWITCH_ON" },
	{ GSM322_EVENT_SWITCH_OFF,	"EVENT_SWITCH_OFF" },
	{ GSM322_EVENT_SIM_INSERT,	"EVENT_SIM_INSERT" },
	{ GSM322_EVENT_SIM_REMOVE,	"EVENT_SIM_REMOVE" },
	{ GSM322_EVENT_REG_FAILED,	"EVENT_REG_FAILED" },
	{ GSM322_EVENT_ROAMING_NA,	"EVENT_ROAMING_NA" },
	{ GSM322_EVENT_INVALID_SIM,	"EVENT_INVALID_SIM" },
	{ GSM322_EVENT_REG_SUCCESS,	"EVENT_REG_SUCCESS" },
	{ GSM322_EVENT_NEW_PLMN,	"EVENT_NEW_PLMN" },
	{ GSM322_EVENT_ON_PLMN,		"EVENT_ON_PLMN" },
	{ GSM322_EVENT_PLMN_SEARCH_START,"EVENT_PLMN_SEARCH_START" },
	{ GSM322_EVENT_PLMN_SEARCH_END,	"EVENT_PLMN_SEARCH_END" },
	{ GSM322_EVENT_USER_RESEL,	"EVENT_USER_RESEL" },
	{ GSM322_EVENT_PLMN_AVAIL,	"EVENT_PLMN_AVAIL" },
	{ GSM322_EVENT_CHOOSE_PLMN,	"EVENT_CHOOSE_PLMN" },
	{ GSM322_EVENT_SEL_MANUAL,	"EVENT_SEL_MANUAL" },
	{ GSM322_EVENT_SEL_AUTO,	"EVENT_SEL_AUTO" },
	{ GSM322_EVENT_CELL_FOUND,	"EVENT_CELL_FOUND" },
	{ GSM322_EVENT_NO_CELL_FOUND,	"EVENT_NO_CELL_FOUND" },
	{ GSM322_EVENT_LEAVE_IDLE,	"EVENT_LEAVE_IDLE" },
	{ GSM322_EVENT_RET_IDLE,	"EVENT_RET_IDLE" },
	{ GSM322_EVENT_CELL_RESEL,	"EVENT_CELL_RESEL" },
	{ GSM322_EVENT_SYSINFO,		"EVENT_SYSINFO" },
	{ GSM322_EVENT_HPLMN_SEARCH,	"EVENT_HPLMN_SEARCH" },
	{ 0,				NULL }
};

const char *get_event_name(int value)
{
	return get_value_string(gsm322_event_names, value);
}


/* allocate a 03.22 event message */
struct msgb *gsm322_msgb_alloc(int msg_type)
{
	struct msgb *msg;
	struct gsm322_msg *gm;

	msg = msgb_alloc_headroom(sizeof(*gm), 0, "GSM 03.22 event");
	if (!msg)
		return NULL;

	gm = (struct gsm322_msg *)msgb_put(msg, sizeof(*gm));
	gm->msg_type = msg_type;

	return msg;
}

/* queue PLMN selection message */
int gsm322_plmn_sendmsg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	msgb_enqueue(&plmn->event_queue, msg);

	return 0;
}

/* queue cell selection message */
int gsm322_cs_sendmsg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;

	msgb_enqueue(&cs->event_queue, msg);

	return 0;
}

/*
 * support
 */

char *gsm_print_rxlev(uint8_t rxlev)
{
	static char string[5];
	if (rxlev == 0)
		return "<=-110";
	if (rxlev >= 63)
		return ">=-48";
	sprintf(string, "-%d", 110 - rxlev);
	return string;
}

static int gsm322_sync_to_cell(struct gsm322_cellsel *cs)
{
	struct osmocom_ms *ms = cs->ms;
	struct gsm48_sysinfo *s = cs->si;
	struct rx_meas_stat *meas = &ms->meas;

	cs->ccch_state = GSM322_CCCH_ST_INIT;
	if (s && s->si3) {
		if (s->ccch_conf == 1) {
			LOGP(DCS, LOGL_INFO, "Sync to ARFCN=%d rxlev=%s "
				"(Sysinfo, ccch mode COMB)\n", cs->arfcn,
				gsm_print_rxlev(cs->list[cs->arfcn].rxlev));
			cs->ccch_mode = CCCH_MODE_COMBINED;
		} else {
			LOGP(DCS, LOGL_INFO, "Sync to ARFCN=%d rxlev=%s "
				"(Sysinfo, ccch mode NON-COMB)\n", cs->arfcn,
				gsm_print_rxlev(cs->list[cs->arfcn].rxlev));
			cs->ccch_mode = CCCH_MODE_NON_COMBINED;
		}
	} else {
		LOGP(DCS, LOGL_INFO, "Sync to ARFCN=%d rxlev=%s (No sysinfo "
			"yet, ccch mode NONE)\n", cs->arfcn,
			gsm_print_rxlev(cs->list[cs->arfcn].rxlev));
		cs->ccch_mode = CCCH_MODE_NONE;
	}

	meas->frames = meas->snr = meas->berr = meas->rxlev = 0;

	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	return l1ctl_tx_fbsb_req(ms, cs->arfcn,
	                         L1CTL_FBSB_F_FB01SB, 100, 0,
				 cs->ccch_mode);
}

static void gsm322_unselect_cell(struct gsm322_cellsel *cs)
{
	cs->selected = 0;
	if (cs->si)
		cs->si->si5 = 0; /* unset SI5* */
	cs->si = NULL;
	memset(&cs->sel_si, 0, sizeof(cs->sel_si));
	cs->sel_mcc = cs->sel_mnc = cs->sel_lac = cs->sel_id = 0;
}

/* print to DCS logging */
static void print_dcs(void *priv, const char *fmt, ...)
{
	char buffer[1000];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
	buffer[sizeof(buffer) - 1] = '\0';
	va_end(args);

	if (buffer[0])
//		LOGP(DCS, LOGL_INFO, "%s", buffer);
		printf("%s", buffer);
}

/* del forbidden LA */
int gsm322_del_forbidden_la(struct osmocom_ms *ms, uint16_t mcc,
	uint16_t mnc, uint16_t lac)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_la_list *la;

	llist_for_each_entry(la, &plmn->forbidden_la, entry) {
		if (la->mcc == mcc && la->mnc == mnc && la->lac == lac) {
			LOGP(DPLMN, LOGL_INFO, "Delete from list of forbidden "
				"LAs (mcc=%s, mnc=%s, lac=%04x)\n",
				gsm_print_mcc(mcc), gsm_print_mnc(mnc), lac);
			llist_del(&la->entry);
			talloc_free(la);
			return 0;
		}
	}

	return -EINVAL;
}

/* add forbidden LA */
int gsm322_add_forbidden_la(struct osmocom_ms *ms, uint16_t mcc,
	uint16_t mnc, uint16_t lac, uint8_t cause)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_la_list *la;

	LOGP(DPLMN, LOGL_INFO, "Add to list of forbidden LAs "
		"(mcc=%s, mnc=%s, lac=%04x)\n", gsm_print_mcc(mcc),
		gsm_print_mnc(mnc), lac);
	la = talloc_zero(l23_ctx, struct gsm322_la_list);
	if (!la)
		return -ENOMEM;
	la->mcc = mcc;
	la->mnc = mnc;
	la->lac = lac;
	la->cause = cause;
	llist_add_tail(&la->entry, &plmn->forbidden_la);

	return 0;
}

/* search forbidden LA */
int gsm322_is_forbidden_la(struct osmocom_ms *ms, uint16_t mcc, uint16_t mnc,
	uint16_t lac)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_la_list *la;

	llist_for_each_entry(la, &plmn->forbidden_la, entry) {
		if (la->mcc == mcc && la->mnc == mnc && la->lac == lac)
			return 1;
	}

	return 0;
}

/* search for PLMN in all BA lists */
static struct gsm322_ba_list *gsm322_find_ba_list(struct gsm322_cellsel *cs,
	uint16_t mcc, uint16_t mnc)
{
	struct gsm322_ba_list *ba, *ba_found = NULL;

	/* search for BA list */
	llist_for_each_entry(ba, &cs->ba_list, entry) {
		if (ba->mcc == mcc
		 && ba->mnc == mnc) {
		 	ba_found = ba;
			break;
		}
	}

	return ba_found;
}

/* search available PLMN */
int gsm322_is_plmn_avail(struct gsm322_cellsel *cs, uint16_t mcc, uint16_t mnc)
{
	int i;

	for (i = 0; i <= 1023; i++) {
		if (cs->list[i].sysinfo
		 && cs->list[i].sysinfo->mcc == mcc
		 && cs->list[i].sysinfo->mnc == mnc)
			return 1;
	}

	return 0;
}

/* search available HPLMN */
int gsm322_is_hplmn_avail(struct gsm322_cellsel *cs, char *imsi)
{
	int i;

	for (i = 0; i <= 1023; i++) {
		if (cs->list[i].sysinfo
		 && gsm_match_mnc(cs->list[i].sysinfo->mcc,
		 	cs->list[i].sysinfo->mnc, imsi))
			return 1;
	}

	return 0;
}

/* del forbidden LA */
/*
 * timer
 */

/*plmn search timer event */
static void plmn_timer_timeout(void *arg)
{
	struct gsm322_plmn *plmn = arg;
	struct msgb *nmsg;

	LOGP(DPLMN, LOGL_INFO, "HPLMN search timer has fired.\n");

	/* indicate PLMN selection T timeout */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_HPLMN_SEARCH);
	if (!nmsg)
		return;
	gsm322_plmn_sendmsg(plmn->ms, nmsg);
}

/* start plmn search timer */
static void start_plmn_timer(struct gsm322_plmn *plmn, int secs)
{
	LOGP(DPLMN, LOGL_INFO, "Starting HPLMN search timer with %d minutes.\n",
		secs / 60);
	plmn->timer.cb = plmn_timer_timeout;
	plmn->timer.data = plmn;
	bsc_schedule_timer(&plmn->timer, secs, 0);
}

/* stop plmn search timer */
static void stop_plmn_timer(struct gsm322_plmn *plmn)
{
	if (bsc_timer_pending(&plmn->timer)) {
		LOGP(DPLMN, LOGL_INFO, "Stopping pending timer.\n");
		bsc_del_timer(&plmn->timer);
	}
}

/* start cell selection timer */
void start_cs_timer(struct gsm322_cellsel *cs, int sec, int micro)
{
	LOGP(DCS, LOGL_DEBUG, "Starting CS timer with %d seconds.\n", sec);
	cs->timer.cb = gsm322_cs_timeout;
	cs->timer.data = cs;
	bsc_schedule_timer(&cs->timer, sec, micro);
}

/* stop cell selection timer */
static void stop_cs_timer(struct gsm322_cellsel *cs)
{
	if (bsc_timer_pending(&cs->timer)) {
		LOGP(DCS, LOGL_DEBUG, "stopping pending CS timer.\n");
		bsc_del_timer(&cs->timer);
	}
}

/*
 * state change
 */

const char *plmn_a_state_names[] = {
	"A0 null",
	"A1 trying RPLMN",
	"A2 on PLMN",
	"A3 trying PLMN",
	"A4 wait for PLMN to appear",
	"A5 HPLMN search",
	"A6 no SIM inserted"
};

const char *plmn_m_state_names[] = {
	"M0 null",
	"M1 trying RPLMN",
	"M2 on PLMN",
	"M3 not on PLMN",
	"M4 trying PLMN",
	"M5 no SIM inserted"
};

const char *cs_state_names[] = {
	"C0 null",
	"C1 normal cell selection",
	"C2 stored cell selection",
	"C3 camped normally",
	"C4 normal cell re-selection",
	"C5 choose cell",
	"C6 any cell selection",
	"C7 camped on any cell",
	"C8 any cell re-selection",
	"C9 choose any cell",
	"PLMN search",
	"HPLMN search"
};


/* new automatic PLMN search state */
static void new_a_state(struct gsm322_plmn *plmn, int state)
{
	if (plmn->ms->settings.plmn_mode != PLMN_MODE_AUTO) {
		LOGP(DPLMN, LOGL_FATAL, "not in auto mode, please fix!\n");
		return;
	}

	stop_plmn_timer(plmn);

	if (state < 0 || state >= (sizeof(plmn_a_state_names) / sizeof(char *)))
		return;

	LOGP(DPLMN, LOGL_INFO, "new state '%s' -> '%s'\n",
		plmn_a_state_names[plmn->state], plmn_a_state_names[state]);

	plmn->state = state;
}

/* new manual PLMN search state */
static void new_m_state(struct gsm322_plmn *plmn, int state)
{
	if (plmn->ms->settings.plmn_mode != PLMN_MODE_MANUAL) {
		LOGP(DPLMN, LOGL_FATAL, "not in manual mode, please fix!\n");
		return;
	}

	if (state < 0 || state >= (sizeof(plmn_m_state_names) / sizeof(char *)))
		return;

	LOGP(DPLMN, LOGL_INFO, "new state '%s' -> '%s'\n",
		plmn_m_state_names[plmn->state], plmn_m_state_names[state]);

	plmn->state = state;
}

/* new Cell selection state */
static void new_c_state(struct gsm322_cellsel *cs, int state)
{
	if (state < 0 || state >= (sizeof(cs_state_names) / sizeof(char *)))
		return;

	LOGP(DCS, LOGL_INFO, "new state '%s' -> '%s'\n",
		cs_state_names[cs->state], cs_state_names[state]);

	/* stop cell selection timer, if running */
	stop_cs_timer(cs);

	/* stop scanning of power measurement */
	if (cs->powerscan) {
		LOGP(DCS, LOGL_INFO, "changing state while power scanning\n");
		l1ctl_tx_reset_req(cs->ms, L1CTL_RES_T_FULL);
		cs->powerscan = 0;
	}

	cs->state = state;
}

/*
 * list of PLMNs
 */

/* 4.4.3 create sorted list of PLMN
 *
 * the source of entries are
 * 
 * - HPLMN
 * - entries found in the SIM's PLMN Selector list
 * - scanned PLMNs above -85 dB (random order)
 * - scanned PLMNs below or equal -85 (by received level)
 *
 * NOTE:
 *
 * The list only includes networks found at last scan.
 *
 * The list always contains HPLMN if available, even if not used by PLMN
 * search process at some conditions.
 *
 * The list contains all PLMNs even if not allowed, so entries have to be
 * removed when selecting from the list. (In case we use manual cell selection,
 * we need to provide non-allowed networks also.)
 */
static int gsm322_sort_list(struct osmocom_ms *ms)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm_sub_plmn_list *sim_entry;
	struct gsm_sub_plmn_na *na_entry;
	struct llist_head temp_list;
	struct gsm322_plmn_list *temp, *found;
	struct llist_head *lh, *lh2;
	int i, entries, move;
	int8_t search = 0;

	/* flush list */
	llist_for_each_safe(lh, lh2, &plmn->sorted_plmn) {
		llist_del(lh);
		talloc_free(lh);
	}

	/* Create a temporary list of all networks */
	INIT_LLIST_HEAD(&temp_list);
	for (i = 0; i <= 1023; i++) {
		if (!(cs->list[i].flags & GSM322_CS_FLAG_TEMP_AA)
		 || !cs->list[i].sysinfo)
			continue;

		/* search if network has multiple cells */
		found = NULL;
		llist_for_each_entry(temp, &temp_list, entry) {
			if (temp->mcc == cs->list[i].sysinfo->mcc
			 && temp->mnc == cs->list[i].sysinfo->mnc) {
			 	found = temp;
			 	break;
			}
		}
		/* update or create */
		if (found) {
			if (cs->list[i].rxlev > found->rxlev)
				found->rxlev = cs->list[i].rxlev;
		} else {
			temp = talloc_zero(l23_ctx, struct gsm322_plmn_list);
			if (!temp)
				return -ENOMEM;
			temp->mcc = cs->list[i].sysinfo->mcc;
			temp->mnc = cs->list[i].sysinfo->mnc;
			temp->rxlev = cs->list[i].rxlev;
			llist_add_tail(&temp->entry, &temp_list);
		}
	}

	/* move Home PLMN, if in list, else add it */
	if (subscr->sim_valid) {
		found = NULL;
		llist_for_each_entry(temp, &temp_list, entry) {
			if (gsm_match_mnc(temp->mcc, temp->mnc, subscr->imsi)) {
			 	found = temp;
				break;
			}
		}
		if (found) {
			/* move */
			llist_del(&found->entry);
			llist_add_tail(&found->entry, &plmn->sorted_plmn);
		}
	}

	/* move entries if in SIM's PLMN Selector list */
	llist_for_each_entry(sim_entry, &subscr->plmn_list, entry) {
		found = NULL;
		llist_for_each_entry(temp, &temp_list, entry) {
			if (temp->mcc == sim_entry->mcc
			 && temp->mnc == sim_entry->mnc) {
			 	found = temp;
				break;
			}
		}
		if (found) {
			llist_del(&found->entry);
			llist_add_tail(&found->entry, &plmn->sorted_plmn);
		}
	}

	/* move PLMN above -85 dBm in random order */
	entries = 0;
	llist_for_each_entry(temp, &temp_list, entry) {
		if (rxlev2dbm(temp->rxlev) > -85)
			entries++;
	}
	while(entries) {
		move = random() % entries;
		i = 0;
		llist_for_each_entry(temp, &temp_list, entry) {
			if (rxlev2dbm(temp->rxlev) > -85) {
				if (i == move) {
					llist_del(&temp->entry);
					llist_add_tail(&temp->entry,
						&plmn->sorted_plmn);
					break;
				}
				i++;
			}
		}
		entries--;
	}

	/* move ohter PLMN in decreasing order */
	while(1) {
		found = NULL;
		llist_for_each_entry(temp, &temp_list, entry) {
			if (!found
			 || temp->rxlev > search) {
			 	search = temp->rxlev;
				found = temp;
			}
		}
		if (!found)
			break;
		llist_del(&found->entry);
		llist_add_tail(&found->entry, &plmn->sorted_plmn);
	}

	/* mark forbidden PLMNs, if in list of forbidden networks */
	i = 0;
	llist_for_each_entry(temp, &plmn->sorted_plmn, entry) {
		llist_for_each_entry(na_entry, &subscr->plmn_na, entry) {
			if (temp->mcc == na_entry->mcc
			 && temp->mnc == na_entry->mnc) {
				temp->cause = na_entry->cause;
				break;
			}
		}
		LOGP(DPLMN, LOGL_INFO, "Creating Sorted PLMN list. "
			"(%02d: mcc %s mnc %s allowed %s rx-lev %s)\n",
			i, gsm_print_mcc(temp->mcc),
			gsm_print_mnc(temp->mnc), (temp->cause) ? "no ":"yes",
			gsm_print_rxlev(temp->rxlev));
		i++;
	}

	gsm322_dump_sorted_plmn(ms);

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

	new_a_state(plmn, GSM322_A2_ON_PLMN);

	/* start timer, if on VPLMN of home country OR special case */
	if (!gsm_match_mnc(plmn->mcc, plmn->mnc, subscr->imsi)
	 && (subscr->always_search_hplmn
	  || gsm_match_mcc(plmn->mcc, subscr->imsi))
	 && subscr->sim_valid && subscr->t6m_hplmn)
	 	start_plmn_timer(plmn, subscr->t6m_hplmn * 360);
	else
		stop_plmn_timer(plmn);

	return 0;
}

/* indicate selected PLMN */
static int gsm322_a_indicate_selected(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	vty_notify(ms, NULL);
	vty_notify(ms, "Selected Network: %s, %s\n",
		gsm_get_mcc(plmn->mcc), gsm_get_mnc(plmn->mcc, plmn->mnc));

	return gsm322_a_go_on_plmn(ms, msg);
}

/* no (more) PLMN in list */
static int gsm322_a_no_more_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *nmsg;
	int found;

	/* any allowable PLMN available? */
	plmn->mcc = plmn->mnc = 0;
	found = gsm322_cs_select(ms, 0, 1);

	/* if no PLMN in list */
	if (found < 0) {
		LOGP(DPLMN, LOGL_INFO, "Not any PLMN allowable.\n");

		new_a_state(plmn, GSM322_A4_WAIT_FOR_PLMN);

#if 0
		/* we must forward this, otherwhise "Any cell selection" 
		 * will not start automatically.
		 */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_NO_CELL_FOUND);
		if (!nmsg)
			return -ENOMEM;
		gsm322_cs_sendmsg(ms, nmsg);
#endif
		LOGP(DPLMN, LOGL_INFO, "Trigger full PLMN search.\n");

		nmsg = gsm322_msgb_alloc(GSM322_EVENT_PLMN_SEARCH_START);
		if (!nmsg)
			return -ENOMEM;
		gsm322_cs_sendmsg(ms, nmsg);

		return 0;
	}

	/* select first PLMN in list */
	plmn->mcc = cs->list[found].sysinfo->mcc;
	plmn->mnc = cs->list[found].sysinfo->mnc;

	LOGP(DPLMN, LOGL_INFO, "PLMN available (mcc=%s mnc=%s  %s, %s)\n",
		gsm_print_mcc(plmn->mcc), gsm_print_mnc(plmn->mnc),
		gsm_get_mcc(plmn->mcc), gsm_get_mnc(plmn->mcc, plmn->mnc));

	/* indicate New PLMN */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	/* go On PLMN */
	return gsm322_a_indicate_selected(ms, msg);
}

/* select first PLMN in list */
static int gsm322_a_sel_first_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	struct gsm322_plmn_list *plmn_entry;
	struct gsm322_plmn_list *plmn_first = NULL;
	int i;

	/* generate list */
	gsm322_sort_list(ms);

	/* select first entry */
	i = 0;
	llist_for_each_entry(plmn_entry, &plmn->sorted_plmn, entry) {
		/* if last selected PLMN was HPLMN, we skip that */
		if (gsm_match_mnc(plmn_entry->mcc, plmn_entry->mnc,
					subscr->imsi)
		 && plmn_entry->mcc == plmn->mcc
		 && plmn_entry->mnc == plmn->mnc) {
			LOGP(DPLMN, LOGL_INFO, "Skip HPLMN, because it was "
				"previously selected.\n");
			i++;
			continue;
		}
		/* select first allowed network */
		if (!plmn_entry->cause) {
			plmn_first = plmn_entry;
			break;
		}
		LOGP(DPLMN, LOGL_INFO, "Skip PLMN (%02d: mcc=%s, mnc=%s), "
			"because it is not allowed (cause %d).\n", i,
			gsm_print_mcc(plmn_entry->mcc),
			gsm_print_mnc(plmn_entry->mnc),
			plmn_entry->cause);
		i++;
	}
	plmn->plmn_curr = i;

	/* if no PLMN in list */
	if (!plmn_first) {
		LOGP(DPLMN, LOGL_INFO, "No PLMN in list.\n");
		gsm322_a_no_more_plmn(ms, msg);

		return 0;
	}

	LOGP(DPLMN, LOGL_INFO, "Selecting PLMN from list. (%02d: mcc=%s "
		"mnc=%s  %s, %s)\n", plmn->plmn_curr,
		gsm_print_mcc(plmn_first->mcc), gsm_print_mnc(plmn_first->mnc),
		gsm_get_mcc(plmn_first->mcc),
		gsm_get_mnc(plmn_first->mcc, plmn_first->mnc));

	/* set current network */
	plmn->mcc = plmn_first->mcc;
	plmn->mnc = plmn_first->mnc;

	new_a_state(plmn, GSM322_A3_TRYING_PLMN);

	/* indicate New PLMN */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	return 0;
}

/* select next PLMN in list */
static int gsm322_a_sel_next_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;
	struct gsm322_plmn_list *plmn_entry;
	struct gsm322_plmn_list *plmn_next = NULL;
	int i, ii;

	/* select next entry from list */
	i = 0;
	ii = plmn->plmn_curr + 1;
	llist_for_each_entry(plmn_entry, &plmn->sorted_plmn, entry) {
		/* skip previously selected networks */
		if (i < ii) {
			i++;
			continue;
		}
		/* select next allowed network */
		if (!plmn_entry->cause) {
			plmn_next = plmn_entry;
			break;
		}
		LOGP(DPLMN, LOGL_INFO, "Skip PLMN (%02d: mcc=%s, mnc=%s), "
			"because it is not allowed (cause %d).\n", i,
			gsm_print_mcc(plmn_entry->mcc),
			gsm_print_mnc(plmn_entry->mnc),
			plmn_entry->cause);
		i++;
	}
	plmn->plmn_curr = i;

	/* if no more PLMN in list */
	if (!plmn_next) {
		LOGP(DPLMN, LOGL_INFO, "No more PLMN in list.\n");
		gsm322_a_no_more_plmn(ms, msg);
		return 0;
	}

	/* set next network */
	plmn->mcc = plmn_next->mcc;
	plmn->mnc = plmn_next->mnc;

	LOGP(DPLMN, LOGL_INFO, "Selecting PLMN from list. (%02d: mcc=%s "
		"mnc=%s  %s, %s)\n", plmn->plmn_curr,
		gsm_print_mcc(plmn->mcc), gsm_print_mnc(plmn->mnc),
		gsm_get_mcc(plmn->mcc), gsm_get_mnc(plmn->mcc, plmn->mnc));

	new_a_state(plmn, GSM322_A3_TRYING_PLMN);

	/* indicate New PLMN */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	return 0;
}

/* User re-selection event */
static int gsm322_a_user_resel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_plmn_list *plmn_entry;
	struct gsm322_plmn_list *plmn_found = NULL;

	if (!subscr->sim_valid) {
		return 0;
	}

	/* try again later, if not idle */
	if (rr->state != GSM48_RR_ST_IDLE) {
		LOGP(DPLMN, LOGL_INFO, "Not idle, rejecting.\n");

		return 0;
	}

	/* search current PLMN in list */
	llist_for_each_entry(plmn_entry, &plmn->sorted_plmn, entry) {
		if (plmn_entry->mcc == plmn->mcc
		 && plmn_entry->mnc == plmn->mnc)
			plmn_found = plmn_entry;
			break;
	}

	/* abort if list is empty */
	if (!plmn_found) {
		LOGP(DPLMN, LOGL_INFO, "Selected PLMN not in list, strange!\n");
		return 0;
	}

	LOGP(DPLMN, LOGL_INFO, "Movin selected PLMN to the bottom of the list "
		"and restarting PLMN search process.\n");

	/* move entry to end of list */
	llist_del(&plmn_found->entry);
	llist_add_tail(&plmn_found->entry, &plmn->sorted_plmn);

	/* select first PLMN in list */
	return gsm322_a_sel_first_plmn(ms, msg);
}

/* PLMN becomes available */
static int gsm322_a_plmn_avail(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm322_msg *gm = (struct gsm322_msg *) msg->data;

	if (subscr->plmn_valid && subscr->plmn_mcc == gm->mcc
	 && subscr->plmn_mnc == gm->mnc) {
		/* go On PLMN */
		plmn->mcc = gm->mcc;
		plmn->mnc = gm->mnc;
		LOGP(DPLMN, LOGL_INFO, "RPLMN became available.\n");
		return gsm322_a_go_on_plmn(ms, msg);
	} else {
		/* select first PLMN in list */
		LOGP(DPLMN, LOGL_INFO, "PLMN became available, start PLMN "
			"search process.\n");
		return gsm322_a_sel_first_plmn(ms, msg);
	}
}
		
/* loss of radio coverage */
static int gsm322_a_loss_of_radio(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;
	int found;
	struct msgb *nmsg;

	/* any PLMN available */
	found = gsm322_cs_select(ms, 0, 1);

	/* if PLMN in list */
	if (found >= 0) {
		LOGP(DPLMN, LOGL_INFO, "PLMN available (mcc=%s mnc=%s  "
			"%s, %s)\n", gsm_print_mcc(
			cs->list[found].sysinfo->mcc),
			gsm_print_mnc(cs->list[found].sysinfo->mnc),
			gsm_get_mcc(cs->list[found].sysinfo->mcc),
			gsm_get_mnc(cs->list[found].sysinfo->mcc,
				cs->list[found].sysinfo->mnc));
		return gsm322_a_sel_first_plmn(ms, msg);
	}

	LOGP(DPLMN, LOGL_INFO, "PLMN not available.\n");

	plmn->mcc = plmn->mnc = 0;

	new_a_state(plmn, GSM322_A4_WAIT_FOR_PLMN);

	/* Tell cell selection process to handle "no cell found". */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NO_CELL_FOUND);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	return 0;
}

/* MS is switched on OR SIM is inserted OR removed */
static int gsm322_a_switch_on(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;

	if (!subscr->sim_valid) {
		LOGP(DSUM, LOGL_INFO, "SIM is removed\n");
		LOGP(DPLMN, LOGL_INFO, "SIM is removed\n");
		new_a_state(plmn, GSM322_A6_NO_SIM);

		return 0;
	}

	/* if there is a registered PLMN */
	if (subscr->plmn_valid) {
		/* select the registered PLMN */
		plmn->mcc = subscr->plmn_mcc;
		plmn->mnc = subscr->plmn_mnc;

		LOGP(DSUM, LOGL_INFO, "Start search of last registered PLMN "
			"(mcc=%s mnc=%s  %s, %s)\n", gsm_print_mcc(plmn->mcc),
			gsm_print_mnc(plmn->mnc), gsm_get_mcc(plmn->mcc),
			gsm_get_mnc(plmn->mcc, plmn->mnc));
		LOGP(DPLMN, LOGL_INFO, "Use RPLMN (mcc=%s mnc=%s  "
			"%s, %s)\n", gsm_print_mcc(plmn->mcc),
			gsm_print_mnc(plmn->mnc), gsm_get_mcc(plmn->mcc),
			gsm_get_mnc(plmn->mcc, plmn->mnc));

		new_a_state(plmn, GSM322_A1_TRYING_RPLMN);

		/* indicate New PLMN */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
		if (!nmsg)
			return -ENOMEM;
		gsm322_cs_sendmsg(ms, nmsg);

		return 0;
	}

	/* initiate search at cell selection */
	LOGP(DSUM, LOGL_INFO, "Search for network\n");
	LOGP(DPLMN, LOGL_INFO, "Switch on, start PLMN search first.\n");

	nmsg = gsm322_msgb_alloc(GSM322_EVENT_PLMN_SEARCH_START);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	return 0;
}

/* MS is switched off */
static int gsm322_a_switch_off(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	new_a_state(plmn, GSM322_A0_NULL);

	return 0;
}

static int gsm322_a_sim_insert(struct osmocom_ms *ms, struct msgb *msg)
{
	LOGP(DPLMN, LOGL_INFO, "SIM already inserted when switched on.\n");
	return 0;
}

/* SIM is removed */
static int gsm322_a_sim_removed(struct osmocom_ms *ms, struct msgb *msg)
{
	struct msgb *nmsg;

	/* indicate SIM remove to cell selection process */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_SIM_REMOVE);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	return gsm322_a_switch_on(ms, msg);
}

/* location update response: "Roaming not allowed" */
static int gsm322_a_roaming_na(struct osmocom_ms *ms, struct msgb *msg)
{
	/* store in list of forbidden LAs is done in gsm48* */

	return gsm322_a_sel_first_plmn(ms, msg);
}

/* On VPLMN of home country and timeout occurs */
static int gsm322_a_hplmn_search_start(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *nmsg;

	/* try again later, if not idle and not camping */
	if (rr->state != GSM48_RR_ST_IDLE
	 || cs->state != GSM322_C3_CAMPED_NORMALLY) {
		LOGP(DPLMN, LOGL_INFO, "Not camping, wait some more.\n");
		start_plmn_timer(plmn, 60);

		return 0;
	}

	new_a_state(plmn, GSM322_A5_HPLMN_SEARCH);

	/* initiate search at cell selection */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_PLMN_SEARCH_START);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	return 0;
}

/* manual mode selected */
static int gsm322_a_sel_manual(struct osmocom_ms *ms, struct msgb *msg)
{
	struct msgb *nmsg;

	/* restart state machine */
	gsm322_a_switch_off(ms, msg);
	ms->settings.plmn_mode = PLMN_MODE_MANUAL;
	gsm322_m_switch_on(ms, msg);

	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_USER_PLMN_SEL);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(ms, nmsg);

	return 0;
}

/*
 * handler for manual search
 */

/* display PLMNs and to Not on PLMN */
static int gsm322_m_display_plmns(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_msg *gm = (struct gsm322_msg *) msg->data;
	int msg_type = gm->msg_type;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_sub_plmn_list *temp;

	/* generate list */
	gsm322_sort_list(ms);

	vty_notify(ms, NULL);
	switch (msg_type) {
	case GSM322_EVENT_REG_FAILED:
		vty_notify(ms, "Failed to register to network %s, %s "
			"(%s, %s)\n",
			gsm_print_mcc(plmn->mcc), gsm_print_mnc(plmn->mnc),
			gsm_get_mcc(plmn->mcc),
			gsm_get_mnc(plmn->mcc, plmn->mnc));
		break;
	case GSM322_EVENT_NO_CELL_FOUND:
		vty_notify(ms, "No cell found for network %s, %s "
			"(%s, %s)\n",
			gsm_print_mcc(plmn->mcc), gsm_print_mnc(plmn->mnc),
			gsm_get_mcc(plmn->mcc),
			gsm_get_mnc(plmn->mcc, plmn->mnc));
		break;
	case GSM322_EVENT_ROAMING_NA:
		vty_notify(ms, "Roaming not allowed to network %s, %s "
			"(%s, %s)\n",
			gsm_print_mcc(plmn->mcc), gsm_print_mnc(plmn->mnc),
			gsm_get_mcc(plmn->mcc),
			gsm_get_mnc(plmn->mcc, plmn->mnc));
		break;
	}

	if (llist_empty(&plmn->sorted_plmn))
		vty_notify(ms, "Search network!\n");
	else {
		vty_notify(ms, "Search or select from network:\n");
		llist_for_each_entry(temp, &plmn->sorted_plmn, entry)
			vty_notify(ms, " Network %s, %s (%s, %s)\n",
				gsm_print_mcc(temp->mcc),
				gsm_print_mnc(temp->mnc),
				gsm_get_mcc(temp->mcc),
				gsm_get_mnc(temp->mcc, temp->mnc));
	}
	
	/* go Not on PLMN state */
	new_m_state(plmn, GSM322_M3_NOT_ON_PLMN);

	return 0;
}

/* user starts reselection */
static int gsm322_m_user_resel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct msgb *nmsg;

	if (!subscr->sim_valid) {
		return 0;
	}

	/* try again later, if not idle */
	if (rr->state != GSM48_RR_ST_IDLE) {
		LOGP(DPLMN, LOGL_INFO, "Not idle, rejecting.\n");

		return 0;
	}

	/* initiate search at cell selection */
	vty_notify(ms, NULL);
	vty_notify(ms, "Searching Network, please wait...\n");
	LOGP(DPLMN, LOGL_INFO, "User re-select, start PLMN search first.\n");

	nmsg = gsm322_msgb_alloc(GSM322_EVENT_PLMN_SEARCH_START);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	return 0;
}

/* MS is switched on OR SIM is inserted OR removed */
static int gsm322_m_switch_on(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;

	if (!subscr->sim_valid) {
		LOGP(DSUM, LOGL_INFO, "SIM is removed\n");
		LOGP(DPLMN, LOGL_INFO, "Switch on without SIM.\n");
		new_m_state(plmn, GSM322_M5_NO_SIM);

		return 0;
	}

	/* if there is a registered PLMN */
	if (subscr->plmn_valid) {
		struct msgb *nmsg;

		/* select the registered PLMN */
		plmn->mcc = subscr->plmn_mcc;
		plmn->mnc = subscr->plmn_mnc;

		LOGP(DSUM, LOGL_INFO, "Start search of last registered PLMN "
			"(mcc=%s mnc=%s  %s, %s)\n", gsm_print_mcc(plmn->mcc),
			gsm_print_mnc(plmn->mnc), gsm_get_mcc(plmn->mcc),
			gsm_get_mnc(plmn->mcc, plmn->mnc));
		LOGP(DPLMN, LOGL_INFO, "Use RPLMN (mcc=%s mnc=%s  "
			"%s, %s)\n", gsm_print_mcc(plmn->mcc),
			gsm_print_mnc(plmn->mnc), gsm_get_mcc(plmn->mcc),
			gsm_get_mnc(plmn->mcc, plmn->mnc));

		new_m_state(plmn, GSM322_M1_TRYING_RPLMN);

		/* indicate New PLMN */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
		if (!nmsg)
			return -ENOMEM;
		gsm322_cs_sendmsg(ms, nmsg);

		return 0;
	}

	/* initiate search at cell selection */
	LOGP(DSUM, LOGL_INFO, "Search for network\n");
	LOGP(DPLMN, LOGL_INFO, "Switch on, start PLMN search first.\n");
	vty_notify(ms, NULL);
	vty_notify(ms, "Searching Network, please wait...\n");

	nmsg = gsm322_msgb_alloc(GSM322_EVENT_PLMN_SEARCH_START);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	return 0;
}

/* MS is switched off */
static int gsm322_m_switch_off(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	stop_plmn_timer(plmn);

	new_m_state(plmn, GSM322_M0_NULL);

	return 0;
}

static int gsm322_m_sim_insert(struct osmocom_ms *ms, struct msgb *msg)
{
	LOGP(DPLMN, LOGL_INFO, "SIM already inserted when switched on.\n");
	return 0;
}

/* SIM is removed */
static int gsm322_m_sim_removed(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;

	stop_plmn_timer(plmn);

	/* indicate SIM remove to cell selection process */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_SIM_REMOVE);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	return gsm322_m_switch_on(ms, msg);
}

/* go to On PLMN state */
static int gsm322_m_go_on_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_subscriber *subscr = &ms->subscr;

	/* set last registered PLMN */
	subscr->plmn_valid = 1;
	subscr->plmn_mcc = plmn->mcc;
	subscr->plmn_mnc = plmn->mnc;
#ifdef TODO
	store on sim
#endif

	new_m_state(plmn, GSM322_M2_ON_PLMN);

	return 0;
}

/* indicate selected PLMN */
static int gsm322_m_indicate_selected(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;

	vty_notify(ms, NULL);
	vty_notify(ms, "Selected Network: %s, %s\n",
		gsm_get_mcc(plmn->mcc), gsm_get_mnc(plmn->mcc, plmn->mnc));

	return gsm322_m_go_on_plmn(ms, msg);
}

/* previously selected PLMN becomes available again */
static int gsm322_m_plmn_avail(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;

	new_m_state(plmn, GSM322_M1_TRYING_RPLMN);

	if (cs->mcc != plmn->mcc || cs->mnc != plmn->mnc) {
		struct msgb *nmsg;

		LOGP(DPLMN, LOGL_INFO, "PLMN available, but currently not "
			"selected, so start selection.\n");

		/* indicate New PLMN */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
		if (!nmsg)
			return -ENOMEM;
		gsm322_cs_sendmsg(ms, nmsg);
	}
	
	return 0;
}

/* the user has selected given PLMN */
static int gsm322_m_choose_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm322_msg *gm = (struct gsm322_msg *) msg->data;
	struct msgb *nmsg;

	/* use user selection */
	plmn->mcc = gm->mcc;
	plmn->mnc = gm->mnc;

	vty_notify(ms, NULL);
	vty_notify(ms, "Selected Network: %s, %s\n",
		gsm_get_mcc(plmn->mcc), gsm_get_mnc(plmn->mcc, plmn->mnc));
	LOGP(DPLMN, LOGL_INFO, "User selects PLMN. (mcc=%s mnc=%s  "
		"%s, %s)\n", gsm_print_mcc(plmn->mcc), gsm_print_mnc(plmn->mnc),
		gsm_get_mcc(plmn->mcc), gsm_get_mnc(plmn->mcc, plmn->mnc));

	/* if selected PLMN is in list of forbidden PLMNs */
	gsm_subscr_del_forbidden_plmn(subscr, plmn->mcc, plmn->mnc);

	new_m_state(plmn, GSM322_M4_TRYING_PLMN);

	/* indicate New PLMN */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	return 0;
}

/* auto mode selected */
static int gsm322_m_sel_auto(struct osmocom_ms *ms, struct msgb *msg)
{
	struct msgb *nmsg;

	/* restart state machine */
	gsm322_m_switch_off(ms, msg);
	ms->settings.plmn_mode = PLMN_MODE_AUTO;
	gsm322_a_switch_on(ms, msg);

	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_USER_PLMN_SEL);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(ms, nmsg);

	return 0;
}

/* if no cell is found in other states than in *_TRYING_* states */
static int gsm322_am_no_cell_found(struct osmocom_ms *ms, struct msgb *msg)
{
	struct msgb *nmsg;

	/* Tell cell selection process to handle "no cell found". */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NO_CELL_FOUND);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	return 0;
}

/*
 * cell scanning process
 */

/* select a suitable and allowable cell */
static int gsm322_cs_select(struct osmocom_ms *ms, int any, int plmn_allowed)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_sysinfo *s;
	int i, found = -1, power = 0;
	uint8_t flags, mask;
	uint16_t acc_class;

	/* set out access class depending on the cell selection type */
	if (any) {
		acc_class = subscr->acc_class | 0x0400; /* add emergency */
		LOGP(DCS, LOGL_DEBUG, "Select using access class with "
			"Emergency class.\n");
	} else {
		acc_class = subscr->acc_class;
		LOGP(DCS, LOGL_DEBUG, "Select using access class \n");
	}

	/* flags to match */
	mask = GSM322_CS_FLAG_SUPPORT | GSM322_CS_FLAG_POWER
		| GSM322_CS_FLAG_SIGNAL | GSM322_CS_FLAG_SYSINFO;
	if (cs->state == GSM322_C2_STORED_CELL_SEL
	 || cs->state == GSM322_C5_CHOOSE_CELL)
		mask |= GSM322_CS_FLAG_BA;
	flags = mask; /* all masked flags are requied */

	/* loop through all scanned frequencies and select cell */
	for (i = 0; i <= 1023; i++) {
		cs->list[i].flags &= ~GSM322_CS_FLAG_TEMP_AA;
		s = cs->list[i].sysinfo;

		/* channel has no informations for us */
		if (!s || (cs->list[i].flags & mask) != flags) {
			continue;
		}

		/* check C1 criteria not fullfilled */
		// TODO: C1 is also dependant on power class and max power
		if (rxlev2dbm(cs->list[i].rxlev) < s->rxlev_acc_min_db) {
			LOGP(DCS, LOGL_INFO, "Skip frequency %d: C1 criteria "
				"not met. (rxlev %s < min %d)\n", i,
				gsm_print_rxlev(cs->list[i].rxlev),
				s->rxlev_acc_min_db);
			continue;
		}

		/* if cell is barred and we don't override */
	        if (!subscr->acc_barr
		 && (cs->list[i].flags & GSM322_CS_FLAG_BARRED)) {
			LOGP(DCS, LOGL_INFO, "Skip frequency %d: Cell is "
				"barred.\n", i);
		 	continue;
		}

		/* if cell is in list of forbidden LAs */
	        if ((cs->list[i].flags & GSM322_CS_FLAG_FORBIDD)) {
			LOGP(DCS, LOGL_INFO, "Skip frequency %d: Cell is in "
				"list of forbidden LAs. (mcc=%s mnc=%s "
				"lai=%04x)\n", i, gsm_print_mcc(s->mcc),
				gsm_print_mnc(s->mnc), s->lac);
		 	continue;
		}

		/* if cell is in list of forbidden PLMNs */
	        if (plmn_allowed && gsm_subscr_is_forbidden_plmn(subscr,
						s->mcc, s->mnc)) {
			LOGP(DCS, LOGL_INFO, "Skip frequency %d: Cell is in "
				"list of forbidden PLMNs. (mcc=%s mnc=%s)\n", i,
				gsm_print_mcc(s->mcc), gsm_print_mnc(s->mnc));
		 	continue;
		}

		/* if we have no access to the cell and we don't override */
	        if (!subscr->acc_barr 
		 && !(acc_class & (s->class_barr ^ 0xffff))) {
			LOGP(DCS, LOGL_INFO, "Skip frequency %d: Class is "
				"barred for out access. (access=%04x "
				"barred=%04x)\n", i, acc_class, s->class_barr);
		 	continue;
		}

		/* store temporary available and allowable flag */
		cs->list[i].flags |= GSM322_CS_FLAG_TEMP_AA;

		/* if we search a specific PLMN, but it does not match */
		if (!any && cs->mcc && (cs->mcc != s->mcc
				|| cs->mnc != s->mnc)) {
			LOGP(DCS, LOGL_INFO, "Skip frequency %d: PLMN of cell "
				"does not match target PLMN. (mcc=%s "
				"mnc=%s)\n", i, gsm_print_mcc(s->mcc),
				gsm_print_mnc(s->mnc));
			continue;
		}

		LOGP(DCS, LOGL_INFO, "Cell frequency %d: Cell found, (rxlev=%s "
			"mcc=%s mnc=%s lac=%04x  %s, %s)\n", i,
			gsm_print_rxlev(cs->list[i].rxlev),
			gsm_print_mcc(s->mcc), gsm_print_mnc(s->mnc), s->lac,
			gsm_get_mcc(s->mcc), gsm_get_mnc(s->mcc, s->mnc));

		/* find highest power cell */
		if (found < 0 || cs->list[i].rxlev > power) {
			power = cs->list[i].rxlev;
			found = i;
		}
	}

	if (found >= 0)
		LOGP(DCS, LOGL_INFO, "Cell frequency %d selected.\n", found);

	return found;
}

/* tune to first/next unscanned frequency and search for PLMN */
static int gsm322_cs_scan(struct osmocom_ms *ms)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int i;
#ifndef SKIP_MAX_PER_BAND
	int j;
#endif
	uint8_t mask, flags;
	uint32_t weight = 0, test = cs->scan_state;

	/* search for strongest unscanned cell */
	mask = GSM322_CS_FLAG_SUPPORT | GSM322_CS_FLAG_POWER
		| GSM322_CS_FLAG_SIGNAL;
	if (cs->state == GSM322_C2_STORED_CELL_SEL
	 || cs->state == GSM322_C5_CHOOSE_CELL)
		mask |= GSM322_CS_FLAG_BA;
	flags = mask; /* all masked flags are requied */
	for (i = 0; i <= 1023; i++) {
#ifndef SKIP_MAX_PER_BAND
		/* skip if band has enough frequencies scanned (3.2.1) */
		for (j = 0; gsm_sup_smax[j].max; j++) {
			if (gsm_sup_smax[j].end > gsm_sup_smax[j].start) {
				if (gsm_sup_smax[j].start >= i
				 && gsm_sup_smax[j].end <= i)
				 	break;
			} else {
				if (gsm_sup_smax[j].end <= i
				 || gsm_sup_smax[j].start >= i)
				 	break;
			}
		}
		if (gsm_sup_smax[j].max) {
			if (gsm_sup_smax[j].temp == gsm_sup_smax[j].max)
				continue;
		}
#endif
		/* search for unscanned frequency */
		if ((cs->list[i].flags & mask) == flags) {
			/* weight depends on the power level
			 * if it is the same, it depends on arfcn
			 */
			test = cs->list[i].rxlev + 1;
			test = (test << 16) | i;
			if (test >= cs->scan_state)
				continue;
			if (test > weight)
				weight = test;
		}
	}
	cs->scan_state = weight;

	if (!weight)
		gsm322_dump_cs_list(cs, GSM322_CS_FLAG_SYSINFO, print_dcs,
			NULL);

	/* special case for PLMN search */
	if (cs->state == GSM322_PLMN_SEARCH && !weight) {
		struct msgb *nmsg;

		/* create AA flag */
		cs->mcc = cs->mnc = 0;
		gsm322_cs_select(ms, 0, 0);

		new_c_state(cs, GSM322_C0_NULL);

		nmsg = gsm322_msgb_alloc(GSM322_EVENT_PLMN_SEARCH_END);
		LOGP(DCS, LOGL_INFO, "PLMN search finished.\n");
		if (!nmsg)
			return -ENOMEM;
		gsm322_plmn_sendmsg(ms, nmsg);

		return 0;
	}

	/* special case for HPLMN search */
	if (cs->state == GSM322_HPLMN_SEARCH && !weight) {
		struct msgb *nmsg;

		nmsg = gsm322_msgb_alloc(GSM322_EVENT_NO_CELL_FOUND);
		LOGP(DCS, LOGL_INFO, "HPLMN search finished, no cell.\n");
		if (!nmsg)
			return -ENOMEM;
		gsm322_plmn_sendmsg(ms, nmsg);

		new_c_state(cs, GSM322_C3_CAMPED_NORMALLY);

		cs->arfcn = cs->sel_arfcn;
		LOGP(DCS, LOGL_INFO, "Tuning back to frequency %d (rxlev "
			"%s).\n", cs->arfcn,
			gsm_print_rxlev(cs->list[cs->arfcn].rxlev));
		hack = 1;
		gsm322_sync_to_cell(cs);

		return 0;
	}

	/* if all frequencies have been searched */
	if (!weight) {
		struct msgb *nmsg;
#if 0
		int found, any = 0;

		LOGP(DCS, LOGL_INFO, "All frequencies scanned.\n");

		/* just see, if we search for any cell */
		if (cs->state == GSM322_C6_ANY_CELL_SEL
		 || cs->state == GSM322_C8_ANY_CELL_RESEL
		 || cs->state == GSM322_C9_CHOOSE_ANY_CELL)
		 	any = 1;

		found = gsm322_cs_select(ms, any, 0);

		/* if found */
		if (found >= 0) {
			struct gsm322_plmn *plmn = &ms->plmn;

			LOGP(DCS, LOGL_INFO, "Tune to frequency %d.\n", found);
			/* tune */
			cs->arfcn = found;
			cs->si = cs->list[cs->arfcn].sysinfo;
			hack = 1;
			gsm322_sync_to_cell(cs);

			/* selected PLMN (manual) or any PLMN (auto) */
			switch (ms->settings.plmn_mode) {
			case PLMN_MODE_AUTO:
				if (plmn->state == GSM322_A4_WAIT_FOR_PLMN) {
					/* PLMN becomes available */
					nmsg = gsm322_msgb_alloc(
						GSM322_EVENT_PLMN_AVAIL);
					if (!nmsg)
						return -ENOMEM;
					gsm322_plmn_sendmsg(ms, nmsg);
				}
				break;
			case PLMN_MODE_MANUAL:
				if (plmn->state == GSM322_M3_NOT_ON_PLMN
				  && gsm322_is_plmn_avail(cs, plmn->mcc,
				  	plmn->mnc)) {
					/* PLMN becomes available */
					nmsg = gsm322_msgb_alloc(
						GSM322_EVENT_PLMN_AVAIL);
					if (!nmsg)
						return -ENOMEM;
					gsm322_plmn_sendmsg(ms, nmsg);
				}
				break;
			}

			/* set selected cell */
			cs->selected = 1;
			cs->sel_arfcn = cs->arfcn;
			memcpy(&cs->sel_si, cs->si, sizeof(cs->sel_si));
			cs->sel_mcc = cs->si->mcc;
			cs->sel_mnc = cs->si->mnc;
			cs->sel_lac = cs->si->lac;
			cs->sel_id = cs->si->cell_id;

			/* tell CS process about available cell */
			LOGP(DCS, LOGL_INFO, "Cell available.\n");
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_CELL_FOUND);
		} else {
#endif
			/* unset selected cell */
			gsm322_unselect_cell(cs);

			/* tell CS process about no cell available */
			LOGP(DCS, LOGL_INFO, "No cell available.\n");
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_NO_CELL_FOUND);
//		}
		if (!nmsg)
			return -ENOMEM;
		gsm322_c_event(ms, nmsg);
		msgb_free(nmsg);

		return 0;
	}

	/* NOTE: We might already have system information from previous
	 * scan. But we need recent informations, so we scan again!
	 */

	/* Tune to frequency for a while, to receive broadcasts. */
	cs->arfcn = weight & 1023;
	LOGP(DCS, LOGL_DEBUG, "Scanning frequency %d (rxlev %s).\n", cs->arfcn,
		gsm_print_rxlev(cs->list[cs->arfcn].rxlev));
	hack = 1;
	gsm322_sync_to_cell(cs);

	/* Allocate/clean system information. */
	cs->list[cs->arfcn].flags &= ~GSM322_CS_FLAG_SYSINFO;
	if (cs->list[cs->arfcn].sysinfo)
		memset(cs->list[cs->arfcn].sysinfo, 0,
			sizeof(struct gsm48_sysinfo));
	else
		cs->list[cs->arfcn].sysinfo = talloc_zero(l23_ctx,
						struct gsm48_sysinfo);
	if (!cs->list[cs->arfcn].sysinfo)
		exit(-ENOMEM);
	cs->si = cs->list[cs->arfcn].sysinfo;

	/* increase scan counter for each maximum scan range */
#ifndef SKIP_MAX_PER_BAND
	if (gsm_sup_smax[j].max) {
		LOGP(DCS, LOGL_DEBUG, "%d frequencies left in band %d..%d\n",
			gsm_sup_smax[j].max - gsm_sup_smax[j].temp,
			gsm_sup_smax[j].start, gsm_sup_smax[j].end);
		gsm_sup_smax[j].temp++;
	}
#endif

	return 0;
}

/* check if cell is now suitable and allowable */
static int gsm322_cs_store(struct osmocom_ms *ms)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = cs->si;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;
	int found, any = 0;

	if (cs->state != GSM322_C2_STORED_CELL_SEL
	 && cs->state != GSM322_C1_NORMAL_CELL_SEL
	 && cs->state != GSM322_C6_ANY_CELL_SEL
	 && cs->state != GSM322_C4_NORMAL_CELL_RESEL
	 && cs->state != GSM322_C8_ANY_CELL_RESEL
	 && cs->state != GSM322_C5_CHOOSE_CELL
	 && cs->state != GSM322_C9_CHOOSE_ANY_CELL
	 && cs->state != GSM322_PLMN_SEARCH
	 && cs->state != GSM322_HPLMN_SEARCH) {
		LOGP(DCS, LOGL_FATAL, "This must only happen during cell "
			"(re-)selection, please fix!\n");
		return -EINVAL;
	}

	/* store sysinfo */
	cs->list[cs->arfcn].flags |= GSM322_CS_FLAG_SYSINFO;
	if (s->cell_barr
	 && !(cs->list[cs->arfcn].sysinfo && cs->list[cs->arfcn].sysinfo->sp &&
			cs->list[cs->arfcn].sysinfo->sp_cbq))
		cs->list[cs->arfcn].flags |= GSM322_CS_FLAG_BARRED;
	else
		cs->list[cs->arfcn].flags &= ~GSM322_CS_FLAG_BARRED;

#if 0
	cs->list[cs->arfcn].min_db = s->rxlev_acc_min_db;
	cs->list[cs->arfcn].class_barr = s->class_barr;
	cs->list[cs->arfcn].max_pwr = s->ms_txpwr_max_ccch;
#endif

	/* store selected network */
	if (s->mcc) {
#if 0
		cs->list[cs->arfcn].mcc = s->mcc;
		cs->list[cs->arfcn].mnc = s->mnc;
		cs->list[cs->arfcn].lac = s->lac;
#endif

		if (gsm322_is_forbidden_la(ms, s->mcc, s->mnc, s->lac))
			cs->list[cs->arfcn].flags |= GSM322_CS_FLAG_FORBIDD;
		else
			cs->list[cs->arfcn].flags &= ~GSM322_CS_FLAG_FORBIDD;
	}

	LOGP(DCS, LOGL_DEBUG, "Scan frequency %d: Cell found. (rxlev %s "
		"mcc %s mnc %s lac %04x)\n", cs->arfcn,
		gsm_print_rxlev(cs->list[cs->arfcn].rxlev),
		gsm_print_mcc(s->mcc), gsm_print_mnc(s->mnc), s->lac);

	/* special case for PLMN search */
	if (cs->state == GSM322_PLMN_SEARCH)
		/* tune to next cell */
		return gsm322_cs_scan(ms);

	/* special case for HPLMN search */
	if (cs->state == GSM322_HPLMN_SEARCH) {
		struct gsm_subscriber *subscr = &ms->subscr;
		struct msgb *nmsg;

		if (!gsm322_is_hplmn_avail(cs, subscr->imsi))
			/* tune to next cell */
			return gsm322_cs_scan(ms);

		nmsg = gsm322_msgb_alloc(GSM322_EVENT_CELL_FOUND);
		LOGP(DCS, LOGL_INFO, "HPLMN search finished, cell found.\n");
		if (!nmsg)
			return -ENOMEM;
		gsm322_plmn_sendmsg(ms, nmsg);

		return 0;
	}

	/* just see, if we search for any cell */
	if (cs->state == GSM322_C6_ANY_CELL_SEL
	 || cs->state == GSM322_C8_ANY_CELL_RESEL
	 || cs->state == GSM322_C9_CHOOSE_ANY_CELL)
	 	any = 1;

	found = gsm322_cs_select(ms, any, 0);

	/* if not found */
	if (found < 0) {
		LOGP(DCS, LOGL_INFO, "Cell not suitable and allowable.\n");
		/* tune to next cell */
		return gsm322_cs_scan(ms);
	}

	LOGP(DCS, LOGL_INFO, "Tune to frequency %d.\n", found);
	/* tune */
	cs->arfcn = found;
	cs->si = cs->list[cs->arfcn].sysinfo;
	hack = 1;
	gsm322_sync_to_cell(cs);

	/* selected PLMN (manual) or any PLMN (auto) */
	switch (ms->settings.plmn_mode) {
	case PLMN_MODE_AUTO:
		if (plmn->state == GSM322_A4_WAIT_FOR_PLMN) {
			/* PLMN becomes available */
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_PLMN_AVAIL);
			if (!nmsg)
				return -ENOMEM;
			gsm322_plmn_sendmsg(ms, nmsg);
		}
		break;
	case PLMN_MODE_MANUAL:
		if (plmn->state == GSM322_M3_NOT_ON_PLMN
		  && gsm322_is_plmn_avail(cs, plmn->mcc,
			plmn->mnc)) {
			/* PLMN becomes available */
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_PLMN_AVAIL);
			if (!nmsg)
				return -ENOMEM;
			gsm322_plmn_sendmsg(ms, nmsg);
		}
		break;
	}

	/* set selected cell */
	cs->selected = 1;
	cs->sel_arfcn = cs->arfcn;
	memcpy(&cs->sel_si, cs->si, sizeof(cs->sel_si));
	cs->sel_mcc = cs->si->mcc;
	cs->sel_mnc = cs->si->mnc;
	cs->sel_lac = cs->si->lac;
	cs->sel_id = cs->si->cell_id;

	/* tell CS process about available cell */
	LOGP(DCS, LOGL_INFO, "Cell available.\n");
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_CELL_FOUND);
	if (!nmsg)
		return -ENOMEM;
	gsm322_c_event(ms, nmsg);
	msgb_free(nmsg);

	return 0;
}

/* process system information when returing to idle mode */
struct gsm322_ba_list *gsm322_cs_sysinfo_sacch(struct osmocom_ms *ms)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = cs->si;
	struct gsm322_ba_list *ba = NULL;
	int i;
	uint8_t freq[128];

	/* collect system information received during dedicated mode */
	if (s->si5
	 && (!s->nb_ext_ind_si5 
	  || (s->si5bis && s->nb_ext_ind_si5 && !s->nb_ext_ind_si5bis)
	  || (s->si5bis && s->si5ter && s->nb_ext_ind_si5
	 	&& s->nb_ext_ind_si5bis))) {
		/* find or create ba list */
		ba = gsm322_find_ba_list(cs, s->mcc, s->mnc);
		if (!ba) {
			ba = talloc_zero(l23_ctx, struct gsm322_ba_list);
			if (!ba)
				return NULL;
			ba->mcc = s->mcc;
			ba->mnc = s->mnc;
			llist_add_tail(&ba->entry, &cs->ba_list);
		}
		/* update (add) ba list */
		memset(freq, 0, sizeof(freq));
		for (i = 0; i <= 1023; i++) {
			if ((s->freq[i].mask & (FREQ_TYPE_SERV
				| FREQ_TYPE_NCELL | FREQ_TYPE_REP)))
				freq[i >> 3] |= (1 << (i & 7));
		}
		if (!!memcmp(freq, ba->freq, sizeof(freq))) {
			LOGP(DCS, LOGL_INFO, "New BA list (mcc=%s mnc=%s  "
				"%s, %s).\n", gsm_print_mcc(ba->mcc),
				gsm_print_mnc(ba->mnc), gsm_get_mcc(ba->mcc),
				gsm_get_mnc(ba->mcc, ba->mnc));
			memcpy(ba->freq, freq, sizeof(freq));
		}
	}

	return ba;
}

/* store BA whenever a system informations changes */
static int gsm322_store_ba_list(struct gsm322_cellsel *cs,
	struct gsm48_sysinfo *s)
{
	struct gsm322_ba_list *ba;
	int i;
	uint8_t freq[128];

	/* find or create ba list */
	ba = gsm322_find_ba_list(cs, s->mcc, s->mnc);
	if (!ba) {
		ba = talloc_zero(l23_ctx, struct gsm322_ba_list);
		if (!ba)
			return -ENOMEM;
		ba->mcc = s->mcc;
		ba->mnc = s->mnc;
		llist_add_tail(&ba->entry, &cs->ba_list);
	}
	/* update ba list */
	memset(freq, 0, sizeof(freq));
	freq[cs->arfcn >> 3] |= (1 << (cs->arfcn & 7));
	for (i = 0; i <= 1023; i++) {
		if ((s->freq[i].mask &
		    (FREQ_TYPE_SERV | FREQ_TYPE_NCELL | FREQ_TYPE_REP)))
			freq[i >> 3] |= (1 << (i & 7));
	}
	if (!!memcmp(freq, ba->freq, sizeof(freq))) {
		LOGP(DCS, LOGL_INFO, "New BA list (mcc=%s mnc=%s  "
			"%s, %s).\n", gsm_print_mcc(ba->mcc),
			gsm_print_mnc(ba->mnc), gsm_get_mcc(ba->mcc),
			gsm_get_mnc(ba->mcc, ba->mnc));
		memcpy(ba->freq, freq, sizeof(freq));
	}

	return 0;
}

/* process system information during camping on a cell */
static int gsm322_c_camp_sysinfo_bcch(struct osmocom_ms *ms, struct msgb *msg)
{
//	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = cs->si;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm322_msg *gm = (struct gsm322_msg *) msg->data;
	struct msgb *nmsg;

#if 0
	if (rr->state != GSM48_RR_ST_IDLE) {
		LOGP(DCS, LOGL_INFO, "Ignoring in dedicated mode.\n");
		return -EBUSY;
	}
#endif

	/* Store BA if we have full system info about cells and neigbor cells.
	 * Depending on the extended bit in the channel description,
	 * we require more or less system informations about neighbor cells
	 */
	if (s->mcc
	 && s->mnc
	 && (gm->sysinfo == GSM48_MT_RR_SYSINFO_1
	  || gm->sysinfo == GSM48_MT_RR_SYSINFO_2
	  || gm->sysinfo == GSM48_MT_RR_SYSINFO_2bis
	  || gm->sysinfo == GSM48_MT_RR_SYSINFO_2ter)
	 && s->si1
	 && s->si2
	 && (!s->nb_ext_ind_si2 
	  || (s->si2bis && s->nb_ext_ind_si2 && !s->nb_ext_ind_si2bis)
	  || (s->si2bis && s->si2ter && s->nb_ext_ind_si2
	 	&& s->nb_ext_ind_si2bis)))
		gsm322_store_ba_list(cs, s);

	/* update sel_si, if all relevant system informations received */
	if (s->si1 && s->si2 && s->si3
	 && (!s->nb_ext_ind_si2
	  || (s->si2bis && s->nb_ext_ind_si2 && !s->nb_ext_ind_si2bis)
	  || (s->si2bis && s->si2ter && s->nb_ext_ind_si2
	 	&& s->nb_ext_ind_si2bis))) {
		if (cs->selected) {
			LOGP(DCS, LOGL_INFO, "Sysinfo of selected cell is "
				"updated.\n");
			memcpy(&cs->sel_si, s, sizeof(cs->sel_si));
			//gsm48_sysinfo_dump(s, print_dcs, NULL);
		}
	}

	/* check for barred cell */
	if (gm->sysinfo == GSM48_MT_RR_SYSINFO_1) {
		/* check if cell becomes barred */
		if (!subscr->acc_barr && s->cell_barr
		 && !(cs->list[cs->arfcn].sysinfo
		   && cs->list[cs->arfcn].sysinfo->sp
		   && cs->list[cs->arfcn].sysinfo->sp_cbq)) {
			LOGP(DCS, LOGL_INFO, "Cell becomes barred.\n");
			trigger_resel:
			/* mark cell as unscanned */
			cs->list[cs->arfcn].flags &= ~GSM322_CS_FLAG_SYSINFO;
			if (cs->list[cs->arfcn].sysinfo) {
				LOGP(DCS, LOGL_INFO, "free sysinfo arfcn=%d\n",
					cs->arfcn);
				talloc_free(cs->list[cs->arfcn].sysinfo);
				cs->list[cs->arfcn].sysinfo = NULL;
				gsm322_unselect_cell(cs);
			}
			/* trigger reselection without queueing,
			 * because other sysinfo message may be queued
			 * before
			 */
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_CELL_RESEL);
			if (!nmsg)
				return -ENOMEM;
			gsm322_c_event(ms, nmsg);
			msgb_free(nmsg);

			return 0;
		}
		/* check if cell access becomes barred */
		if (!((subscr->acc_class & 0xfbff)
			& (s->class_barr ^ 0xffff))) {
			LOGP(DCS, LOGL_INFO, "Cell access becomes barred.\n");
			goto trigger_resel;
		}
	}

	/* check if MCC, MNC, LAC, cell ID changes */
	if (cs->sel_mcc != s->mcc || cs->sel_mnc != s->mnc
	 || cs->sel_lac != s->lac) {
		LOGP(DCS, LOGL_NOTICE, "Cell changes location area. "
			"This is not good!\n");
		goto trigger_resel;
	}
	if (cs->sel_id != s->cell_id) {
		LOGP(DCS, LOGL_NOTICE, "Cell changes cell ID. "
			"This is not good!\n");
		goto trigger_resel;
	}

	return 0;
}

/* process system information during channel scanning */
static int gsm322_c_scan_sysinfo_bcch(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = cs->si;
	struct gsm322_msg *gm = (struct gsm322_msg *) msg->data;

	/* no sysinfo if we are not done with power scan */
	if (cs->powerscan) {
		LOGP(DCS, LOGL_INFO, "Ignoring sysinfo during power scan.\n");
		return -EINVAL;
	}

	/* Store BA if we have full system info about cells and neigbor cells.
	 * Depending on the extended bit in the channel description,
	 * we require more or less system informations about neighbor cells
	 */
	if (s->mcc
	 && s->mnc
	 && (gm->sysinfo == GSM48_MT_RR_SYSINFO_1
	  || gm->sysinfo == GSM48_MT_RR_SYSINFO_2
	  || gm->sysinfo == GSM48_MT_RR_SYSINFO_2bis
	  || gm->sysinfo == GSM48_MT_RR_SYSINFO_2ter)
	 && s->si1
	 && s->si2
	 && (!s->nb_ext_ind_si2 
	  || (s->si2bis && s->nb_ext_ind_si2 && !s->nb_ext_ind_si2bis)
	  || (s->si2bis && s->si2ter && s->nb_ext_ind_si2
	 	&& s->nb_ext_ind_si2bis)))
		gsm322_store_ba_list(cs, s);

	/* all relevant system informations received */
	if (s->si1 && s->si2 && s->si3
	 && (!s->nb_ext_ind_si2
	  || (s->si2bis && s->nb_ext_ind_si2 && !s->nb_ext_ind_si2bis)
	  || (s->si2bis && s->si2ter && s->nb_ext_ind_si2
	 	&& s->nb_ext_ind_si2bis))) {
		LOGP(DCS, LOGL_DEBUG, "Received relevant sysinfo.\n");
		/* stop timer */
		stop_cs_timer(cs);

		//gsm48_sysinfo_dump(s, print_dcs, NULL);

		/* store sysinfo and continue scan */
		return gsm322_cs_store(ms);
	}

	/* wait for more sysinfo or timeout */
	return 0;
}

static void gsm322_cs_timeout(void *arg)
{
	struct gsm322_cellsel *cs = arg;
	struct osmocom_ms *ms = cs->ms;

	/* if we have no lock, we retry */
	if (cs->ccch_state != GSM322_CCCH_ST_SYNC)
		LOGP(DCS, LOGL_INFO, "Cell selection failed, sync timeout.\n");
	else
		LOGP(DCS, LOGL_INFO, "Cell selection failed, read timeout.\n");

	/* remove system information */
	cs->list[cs->arfcn].flags &= ~GSM322_CS_FLAG_SYSINFO; 
	if (cs->list[cs->arfcn].sysinfo) {
		LOGP(DCS, LOGL_DEBUG, "free sysinfo arfcn=%d\n", cs->arfcn);
		talloc_free(cs->list[cs->arfcn].sysinfo);
		cs->list[cs->arfcn].sysinfo = NULL;
		gsm322_unselect_cell(cs);
	}

	/* tune to next cell */
	gsm322_cs_scan(ms);

	return;
}

/*
 * power scan process
 */

/* search for block of unscanned frequencies and start scanning */
static int gsm322_cs_powerscan(struct osmocom_ms *ms)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm_settings *set = &ms->settings;
	int i, s = -1, e;
	uint8_t mask, flags;

	again:

	mask = GSM322_CS_FLAG_SUPPORT | GSM322_CS_FLAG_POWER;
	flags = GSM322_CS_FLAG_SUPPORT;

	/* in case of sticking to a cell, we only select it */
	if (set->stick) {
		LOGP(DCS, LOGL_DEBUG, "Scanning power for sticked cell.\n");
		i = set->stick_arfcn;
		if ((cs->list[i].flags & mask) == flags)
			s = e = i;
	} else {
		/* search for first frequency to scan */
		if (cs->state == GSM322_C2_STORED_CELL_SEL
		 || cs->state == GSM322_C5_CHOOSE_CELL) {
			LOGP(DCS, LOGL_DEBUG, "Scanning power for stored BA "
				"list.\n");
			mask |= GSM322_CS_FLAG_BA;
			flags |= GSM322_CS_FLAG_BA;
		} else
			LOGP(DCS, LOGL_DEBUG, "Scanning power for all "
				"frequencies.\n");
		for (i = 0; i <= 1023; i++) {
			if ((cs->list[i].flags & mask) == flags) {
				s = e = i;
				break;
			}
		}
	}

	/* if there is no more frequency, we can tune to that cell */
	if (s < 0) {
		int found = 0;

		/* stop power level scanning */
		cs->powerscan = 0;

		/* check if not signal is found */
		for (i = 0; i <= 1023; i++) {
			if ((cs->list[i].flags & GSM322_CS_FLAG_SIGNAL))
				found++;
		}
		if (!found) {
			struct msgb *nmsg;

			LOGP(DCS, LOGL_INFO, "Found no frequency.\n");
			/* on normal cell selection, start over */
			if (cs->state == GSM322_C1_NORMAL_CELL_SEL) {
				for (i = 0; i <= 1023; i++) {
					/* clear flag that this was scanned */
					cs->list[i].flags &=
						~(GSM322_CS_FLAG_POWER
						| GSM322_CS_FLAG_SIGNAL
						| GSM322_CS_FLAG_SYSINFO);
					if (cs->list[i].sysinfo) {
						LOGP(DCS, LOGL_INFO, "free "
							"sysinfo arfcn=%d\n",
							i);
						talloc_free(
							cs->list[i].sysinfo);
						cs->list[i].sysinfo = NULL;
					}
				}
				/* no cell selected */
				gsm322_unselect_cell(cs);
				goto again;
			}
			/* on other cell selection, indicate "no cell found" */
			/* NOTE: PLMN search process handles it.
			 * If not handled there, CS process gets indicated.
			 * If we would continue to process CS, then we might get
			 * our list of scanned cells disturbed.
			 */
			if (cs->state == GSM322_PLMN_SEARCH)
				nmsg = gsm322_msgb_alloc(
					GSM322_EVENT_PLMN_SEARCH_END);
			else
				nmsg = gsm322_msgb_alloc(
					GSM322_EVENT_NO_CELL_FOUND);
			if (!nmsg)
				return -ENOMEM;
			gsm322_plmn_sendmsg(ms, nmsg);

			/* if HPLMN search, select last frequency */
			if (cs->state == GSM322_HPLMN_SEARCH) {
				new_c_state(cs, GSM322_C3_CAMPED_NORMALLY);

				cs->arfcn = cs->sel_arfcn;
				LOGP(DCS, LOGL_INFO, "Tuning back to frequency "
					"%d (rxlev %s).\n", cs->arfcn,
					gsm_print_rxlev(
						cs->list[cs->arfcn].rxlev));
				hack = 1;
				gsm322_sync_to_cell(cs);

			} else
				new_c_state(cs, GSM322_C0_NULL);

			return 0;
		}
		LOGP(DCS, LOGL_INFO, "Found %d frequencies.\n", found);
		cs->scan_state = 0xffffffff; /* higher than high */
		/* clear counter of scanned frequencies of each range */
		for (i = 0; gsm_sup_smax[i].max; i++)
			gsm_sup_smax[i].temp = 0;
		return gsm322_cs_scan(ms);
	}

	/* search last frequency to scan (en block) */
	e = i;
	if (!set->stick) {
		for (i = s + 1; i <= 1023; i++) {
			if ((cs->list[i].flags & mask) == flags)
				e = i;
			else
				break;
		}
	}

	LOGP(DCS, LOGL_DEBUG, "Scanning frequencies. (%d..%d)\n", s, e);

	/* start scan on radio interface */
	if (!cs->powerscan) {
		l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
		cs->powerscan = 1;
	}
//#warning TESTING!!!!
//usleep(300000);
	return l1ctl_tx_pm_req_range(ms, s, e);
}

static int gsm322_l1_signal(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms;
	struct gsm322_cellsel *cs;
	struct osmobb_meas_res *mr;
	struct osmobb_fbsb_res *fr;
	int i;
	int8_t rxlev;

	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_PM_RES:
		mr = signal_data;
		ms = mr->ms;
		cs = &ms->cellsel;
		if (!cs->powerscan)
			return -EINVAL;
		i = mr->band_arfcn & 1023;
		rxlev = mr->rx_lev;
		if ((cs->list[i].flags & GSM322_CS_FLAG_POWER)) {
			LOGP(DCS, LOGL_ERROR, "Getting PM for frequency %d "
				"twice. Overwriting the first! Please fix "
				"prim_pm.c\n", i);
		}
		cs->list[i].rxlev = rxlev;
		cs->list[i].flags |= GSM322_CS_FLAG_POWER;
		cs->list[i].flags &= ~GSM322_CS_FLAG_SIGNAL;
		/* if minimum level is reached or if we stick to a cell */
		if (rxlev2dbm(rxlev) >= ms->settings.min_rxlev_db
		 || ms->settings.stick) {
			cs->list[i].flags |= GSM322_CS_FLAG_SIGNAL;
			LOGP(DCS, LOGL_INFO, "Found signal (frequency %d "
				"rxlev %s (%d))\n", i,
				gsm_print_rxlev(rxlev), rxlev);
		}
		break;
	case S_L1CTL_PM_DONE:
		LOGP(DCS, LOGL_DEBUG, "Done with power scanning range.\n");
		ms = signal_data;
		cs = &ms->cellsel;
		if (!cs->powerscan)
			return -EINVAL;
		gsm322_cs_powerscan(ms);
		break;
	case S_L1CTL_FBSB_RESP:
		fr = signal_data;
		ms = fr->ms;
		cs = &ms->cellsel;
		if (cs->ccch_state == GSM322_CCCH_ST_INIT) {
			LOGP(DCS, LOGL_INFO, "Channel synched. (ARFCN=%d, "
				"snr=%u, BSIC=%u)\n", cs->arfcn, fr->snr,
				fr->bsic);
			cs->ccch_state = GSM322_CCCH_ST_SYNC;
			if (cs->si)
				cs->si->bsic = fr->bsic;
#if 0
			stop_cs_timer(cs);

			/* in dedicated mode */
			if (ms->rrlayer.state == GSM48_RR_ST_CONN_PEND)
				return gsm48_rr_tx_rand_acc(ms, NULL);
#endif

			/* set timer for reading BCCH */
			if (cs->state == GSM322_C2_STORED_CELL_SEL
			 || cs->state == GSM322_C1_NORMAL_CELL_SEL
			 || cs->state == GSM322_C6_ANY_CELL_SEL
			 || cs->state == GSM322_C4_NORMAL_CELL_RESEL
			 || cs->state == GSM322_C8_ANY_CELL_RESEL
			 || cs->state == GSM322_C5_CHOOSE_CELL
			 || cs->state == GSM322_C9_CHOOSE_ANY_CELL
			 || cs->state == GSM322_PLMN_SEARCH
			 || cs->state == GSM322_HPLMN_SEARCH)
				start_cs_timer(cs, ms->support.scan_to, 0);
					// TODO: timer depends on BCCH config

			/* set downlink signalling failure criterion */
			ms->meas.ds_fail = ms->meas.dsc = ms->settings.dsc_max;
			LOGP(DRR, LOGL_INFO, "using DSC of %d\n", ms->meas.dsc);
		}
		break;
	case S_L1CTL_FBSB_ERR:
#if 0
		if (hack) {
			ms = signal_data;
			cs = &ms->cellsel;
			gsm322_sync_to_cell(cs);
			hack--;
			LOGP(DCS, LOGL_INFO, "Channel sync error, try again\n");
			break;
		}
#endif
		LOGP(DCS, LOGL_INFO, "Channel sync error.\n");
		ms = signal_data;
		cs = &ms->cellsel;

		stop_cs_timer(cs);
		if (cs->selected)
			gsm322_cs_loss(cs);
		else
			gsm322_cs_timeout(cs);
		break;
	case S_L1CTL_LOSS_IND:
		ms = signal_data;
		cs = &ms->cellsel;
		gsm322_cs_loss(cs);
		break;
	case S_L1CTL_RESET:
		ms = signal_data;
		if (ms->mmlayer.power_off_idle) {
			l23_app_exit(ms);
			exit(0);
		}
		break;
	}

	return 0;
}

static void gsm322_cs_loss(void *arg)
{
	struct gsm322_cellsel *cs = arg;
	struct osmocom_ms *ms = cs->ms;
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	LOGP(DCS, LOGL_INFO, "Loss of CCCH.\n");
	if (cs->state == GSM322_C3_CAMPED_NORMALLY
	 || cs->state == GSM322_C7_CAMPED_ANY_CELL) {
		if (rr->state == GSM48_RR_ST_IDLE) {
			struct msgb *nmsg;

			LOGP(DCS, LOGL_INFO, "Trigger re-selection.\n");

			nmsg = gsm322_msgb_alloc(GSM322_EVENT_CELL_RESEL);
			if (!nmsg)
				return;
			gsm322_c_event(ms, nmsg);
			msgb_free(nmsg);
		} else {
			LOGP(DCS, LOGL_INFO, "Trigger RR abort.\n");
			gsm48_rr_los(ms);
			/* be shure that nothing else is done after here
			 * because the function call above may cause
			 * to return from idle state and trigger cell re-sel.
			 */
		}
	}

	return;
}

/*
 * handler for cell selection process
 */

/* start PLMN search */
static int gsm322_c_plmn_search(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int i;

	new_c_state(cs, GSM322_PLMN_SEARCH);

	/* mark all frequencies except our own BA to be scanned */
	for (i = 0; i <= 1023; i++) {
		cs->list[i].flags &= ~(GSM322_CS_FLAG_POWER
					| GSM322_CS_FLAG_SIGNAL
					| GSM322_CS_FLAG_SYSINFO);
		if (cs->list[i].sysinfo) {
			LOGP(DCS, LOGL_INFO, "free sysinfo arfcn=%d\n", i);
			talloc_free(cs->list[i].sysinfo);
			cs->list[i].sysinfo = NULL;
			gsm322_unselect_cell(cs);
		}
	}

	/* unset selected cell */
	gsm322_unselect_cell(cs);

	/* start power scan */
	return gsm322_cs_powerscan(ms);
}

/* start HPLMN search */
static int gsm322_c_hplmn_search(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int i;

	new_c_state(cs, GSM322_HPLMN_SEARCH);

	/* mark all frequencies except our own BA to be scanned */
	for (i = 0; i <= 1023; i++) {
		if (i != cs->sel_arfcn
		 && (cs->list[i].flags & GSM322_CS_FLAG_SYSINFO)
		 && !(cs->list[i].flags & GSM322_CS_FLAG_BA)) {
			cs->list[i].flags &= ~(GSM322_CS_FLAG_POWER
						| GSM322_CS_FLAG_SIGNAL
						| GSM322_CS_FLAG_SYSINFO);
			if (cs->list[i].sysinfo) {
				LOGP(DCS, LOGL_INFO, "free sysinfo arfcn=%d\n",
					i);
				talloc_free(cs->list[i].sysinfo);
				cs->list[i].sysinfo = NULL;
			}
		}
	}

	/* no cell selected */
	gsm322_unselect_cell(cs);

	/* start power scan */
	return gsm322_cs_powerscan(ms);
}

/* start stored cell selection */
static int gsm322_c_stored_cell_sel(struct osmocom_ms *ms, struct gsm322_ba_list *ba)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int i;

	new_c_state(cs, GSM322_C2_STORED_CELL_SEL);

	/* flag all frequencies that are in current band allocation */
	for (i = 0; i <= 1023; i++) {
		if ((ba->freq[i >> 3] & (1 << (i & 7))))
			cs->list[i].flags |= GSM322_CS_FLAG_BA;
		else
			cs->list[i].flags &= ~GSM322_CS_FLAG_BA;
	}

	/* unset selected cell */
	gsm322_unselect_cell(cs);

	/* start power scan */
	return gsm322_cs_powerscan(ms);
}

/* start noraml cell selection */
static int gsm322_c_normal_cell_sel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int i;

	/* except for stored cell selection state, we weed to rescan ?? */
	if (cs->state != GSM322_C2_STORED_CELL_SEL) {
		for (i = 0; i <= 1023; i++) {
			cs->list[i].flags &= ~(GSM322_CS_FLAG_POWER
						| GSM322_CS_FLAG_SIGNAL
						| GSM322_CS_FLAG_SYSINFO);
			if (cs->list[i].sysinfo) {
				LOGP(DCS, LOGL_INFO, "free sysinfo arfcn=%d\n",
					i);
				talloc_free(cs->list[i].sysinfo);
				cs->list[i].sysinfo = NULL;
			}
		}
	}

	new_c_state(cs, GSM322_C1_NORMAL_CELL_SEL);

	/* unset selected cell */
	gsm322_unselect_cell(cs);

	/* start power scan */
	return gsm322_cs_powerscan(ms);
}

/* start any cell selection */
static int gsm322_c_any_cell_sel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;

	/* in case we already tried any cell (re-)selection, power scan again */
	if (cs->state == GSM322_C0_NULL
	 || cs->state == GSM322_C6_ANY_CELL_SEL
	 || cs->state == GSM322_C8_ANY_CELL_RESEL) {
		int i;

		for (i = 0; i <= 1023; i++) {
			cs->list[i].flags &= ~(GSM322_CS_FLAG_POWER
						| GSM322_CS_FLAG_SIGNAL
						| GSM322_CS_FLAG_SYSINFO);
			if (cs->list[i].sysinfo) {
				LOGP(DCS, LOGL_INFO, "free sysinfo arfcn=%d\n",
					i);
				talloc_free(cs->list[i].sysinfo);
				cs->list[i].sysinfo = NULL;
			}
		}
	}
	/* after re-selection, indicate no cell found */
	if (cs->state == GSM322_C6_ANY_CELL_SEL
	 || cs->state == GSM322_C8_ANY_CELL_RESEL) {
		struct msgb *nmsg;

		/* tell that we have no cell found */
		nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_NO_CELL_FOUND);
		if (!nmsg)
			return -ENOMEM;
		gsm48_mmevent_msg(ms, nmsg);

	} else { 
		new_c_state(cs, GSM322_C6_ANY_CELL_SEL);
	}

	cs->mcc = cs->mnc = 0;

	/* unset selected cell */
	gsm322_unselect_cell(cs);

	/* start power scan */
	return gsm322_cs_powerscan(ms);
}

/* start noraml cell re-selection */
static int gsm322_c_normal_cell_resel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;

	new_c_state(cs, GSM322_C4_NORMAL_CELL_RESEL);

	/* NOTE: We keep our scan info we have so far.
	 * This may cause a skip in power scan. */

	/* start power scan */
	return gsm322_cs_powerscan(ms);
}

/* start any cell re-selection */
static int gsm322_c_any_cell_resel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;

	new_c_state(cs, GSM322_C8_ANY_CELL_RESEL);

	/* NOTE: We keep our scan info we have so far.
	 * This may cause a skip in power scan. */

	/* start power scan */
	return gsm322_cs_powerscan(ms);
}

/* a suitable cell was found, so we camp normally */
static int gsm322_c_camp_normally(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *nmsg;

	LOGP(DSUM, LOGL_INFO, "Camping normally on cell (arfcn=%d mcc=%s "
		"mnc=%s  %s, %s)\n", cs->sel_arfcn, gsm_print_mcc(cs->sel_mcc),
		gsm_print_mnc(cs->sel_mnc), gsm_get_mcc(cs->sel_mcc),
		gsm_get_mnc(cs->sel_mcc, cs->sel_mnc));

	/* tell that we have selected a (new) cell */
	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_CELL_SELECTED);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(ms, nmsg);

	new_c_state(cs, GSM322_C3_CAMPED_NORMALLY);

	return 0;
}

/* a not suitable cell was found, so we camp on any cell */
static int gsm322_c_camp_any_cell(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *nmsg;

	LOGP(DSUM, LOGL_INFO, "Camping on any cell (arfcn=%d mcc=%s "
		"mnc=%s  %s, %s)\n", cs->sel_arfcn, gsm_print_mcc(cs->sel_mcc),
		gsm_print_mnc(cs->sel_mnc), gsm_get_mcc(cs->sel_mcc),
		gsm_get_mnc(cs->sel_mcc, cs->sel_mnc));


	/* tell that we have selected a (new) cell */
	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_CELL_SELECTED);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(ms, nmsg);

	new_c_state(cs, GSM322_C7_CAMPED_ANY_CELL);

	return 0;
}

/* create temporary ba range with given frequency ranges */
struct gsm322_ba_list *gsm322_cs_ba_range(struct osmocom_ms *ms,
	uint32_t *range, uint8_t ranges)
{
	static struct gsm322_ba_list ba;
	uint16_t lower, higher;

	memset(&ba, 0, sizeof(ba));

	while(ranges--) {
		lower = *range & 1023;
		higher = (*range >> 16) & 1023;
		range++;
		LOGP(DCS, LOGL_INFO, "Use BA range: %d..%d\n", lower, higher);
		/* GSM 05.08 6.3 */
		while (1) {
			ba.freq[lower >> 3] |= 1 << (lower & 7);
			if (lower == higher)
				break;
			lower = (lower + 1) & 1023;
		}
	}

	return &ba;
}

/* common part of gsm322_c_choose_cell and gsm322_c_choose_any_cell */
static int gsm322_cs_choose(struct osmocom_ms *ms)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_ba_list *ba = NULL;
	int i;

	/* NOTE: The call to this function is synchron to RR layer, so
	 * we may access the BA range there.
	 */
	if (rr->ba_ranges)
		ba = gsm322_cs_ba_range(ms, rr->ba_range, rr->ba_ranges);
	else {
		LOGP(DCS, LOGL_INFO, "No BA range(s), try sysinfo.\n");
		/* get and update BA of last received sysinfo 5* */
		ba = gsm322_cs_sysinfo_sacch(ms);
		if (!ba) {
			LOGP(DCS, LOGL_INFO, "No BA on sysinfo, try stored "
				"BA list.\n");
			ba = gsm322_find_ba_list(cs, cs->sel_si.mcc,
				cs->sel_si.mnc);
		}
	}

	if (!ba) {
		struct msgb *nmsg;

		LOGP(DCS, LOGL_INFO, "No BA list to use.\n");

		/* tell CS to start over */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_NO_CELL_FOUND);
		if (!nmsg)
			return -ENOMEM;
		gsm322_c_event(ms, nmsg);
		msgb_free(nmsg);

		return 0;
	}

	/* flag all frequencies that are in current band allocation */
	for (i = 0; i <= 1023; i++) {
		if (cs->state == GSM322_C5_CHOOSE_CELL) {
			if ((ba->freq[i >> 3] & (1 << (i & 7)))) {
				cs->list[i].flags |= GSM322_CS_FLAG_BA;
			} else {
				cs->list[i].flags &= ~GSM322_CS_FLAG_BA;
			}
		}
		cs->list[i].flags &= ~(GSM322_CS_FLAG_POWER
					| GSM322_CS_FLAG_SIGNAL
					| GSM322_CS_FLAG_SYSINFO);
		if (cs->list[i].sysinfo) {
			LOGP(DCS, LOGL_INFO, "free sysinfo arfcn=%d\n", i);
			talloc_free(cs->list[i].sysinfo);
			cs->list[i].sysinfo = NULL;
		}
	}

	/* unset selected cell */
	gsm322_unselect_cell(cs);

	/* start power scan */
	return gsm322_cs_powerscan(ms);
}

/* start 'Choose cell' after returning to idle mode */
static int gsm322_c_choose_cell(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm322_msg *gm = (struct gsm322_msg *) msg->data;

	/* After location updating, we choose the last cell */
	if (gm->same_cell) {
		struct msgb *nmsg;

		if (!cs->selected) {
			printf("No cell selected when ret.idle, please fix!\n");
			exit(0L);
		}
		cs->arfcn = cs->sel_arfcn;

		/* be sure to go to current camping frequency on return */
		LOGP(DCS, LOGL_INFO, "Selecting frequency %d. after LOC.UPD.\n",
			cs->arfcn);
		hack = 1;
		gsm322_sync_to_cell(cs);
		cs->si = cs->list[cs->arfcn].sysinfo;

		new_c_state(cs, GSM322_C3_CAMPED_NORMALLY);

		/* tell that we have selected the cell, so RR returns IDLE */
		nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_CELL_SELECTED);
		if (!nmsg)
			return -ENOMEM;
		gsm48_mmevent_msg(ms, nmsg);

		return 0;
	}

	new_c_state(cs, GSM322_C5_CHOOSE_CELL);

	return gsm322_cs_choose(ms);
}

/* start 'Choose any cell' after returning to idle mode */
static int gsm322_c_choose_any_cell(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;

	new_c_state(cs, GSM322_C9_CHOOSE_ANY_CELL);

	return gsm322_cs_choose(ms);
}

/* a new PLMN is selected by PLMN search process */
static int gsm322_c_new_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_ba_list *ba;

	cs->mcc = plmn->mcc;
	cs->mnc = plmn->mnc;

	LOGP(DSUM, LOGL_INFO, "Selecting network (mcc=%s "
		"mnc=%s  %s, %s)\n", gsm_print_mcc(cs->mcc),
		gsm_print_mnc(cs->mnc), gsm_get_mcc(cs->mcc),
		gsm_get_mnc(cs->mcc, cs->mnc));

	/* search for BA list */
	ba = gsm322_find_ba_list(cs, plmn->mcc, plmn->mnc);

	if (ba) {
		LOGP(DCS, LOGL_INFO, "Start stored cell selection.\n");
		return gsm322_c_stored_cell_sel(ms, ba);
	} else {
		LOGP(DCS, LOGL_INFO, "Start normal cell selection.\n");
		return gsm322_c_normal_cell_sel(ms, msg);
	}
}

/* go connected mode */
static int gsm322_c_conn_mode_1(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;

	/* check for error */
	if (!cs->selected)
		return -EINVAL;
	cs->arfcn = cs->sel_arfcn;

	/* be sure to go to current camping frequency on return */
	LOGP(DCS, LOGL_INFO, "Going to camping frequency %d.\n", cs->arfcn);
	hack = 1;
	gsm322_sync_to_cell(cs);
	cs->si = cs->list[cs->arfcn].sysinfo;

	return 0;
}

static int gsm322_c_conn_mode_2(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;

	/* check for error */
	if (!cs->selected)
		return -EINVAL;
	cs->arfcn = cs->sel_arfcn;

	/* be sure to go to current camping frequency on return */
	LOGP(DCS, LOGL_INFO, "Going to camping frequency %d.\n", cs->arfcn);
	hack = 1;
	gsm322_sync_to_cell(cs);
	cs->si = cs->list[cs->arfcn].sysinfo;

	return 0;
}

/* switch on */
static int gsm322_c_switch_on(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;

	/* if no SIM is is MS */
	if (!subscr->sim_valid) {
		LOGP(DCS, LOGL_INFO, "Switch on without SIM.\n");
		return gsm322_c_any_cell_sel(ms, msg);
	}
	LOGP(DCS, LOGL_INFO, "Switch on with SIM inserted.\n");

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
	{SBIT(GSM322_A0_NULL),
	 GSM322_EVENT_SWITCH_ON, gsm322_a_switch_on},

	/* special case for full search */
	{SBIT(GSM322_A0_NULL),
	 GSM322_EVENT_PLMN_SEARCH_END, gsm322_a_sel_first_plmn},

	{ALL_STATES,
	 GSM322_EVENT_SWITCH_OFF, gsm322_a_switch_off},

	{SBIT(GSM322_A0_NULL) | SBIT(GSM322_A6_NO_SIM),
	 GSM322_EVENT_SIM_INSERT, gsm322_a_switch_on},

	{ALL_STATES,
	 GSM322_EVENT_SIM_INSERT, gsm322_a_sim_insert},

	{ALL_STATES,
	 GSM322_EVENT_SIM_REMOVE, gsm322_a_sim_removed},

	{ALL_STATES,
	 GSM322_EVENT_INVALID_SIM, gsm322_a_sim_removed},

	{SBIT(GSM322_A1_TRYING_RPLMN),
	 GSM322_EVENT_REG_FAILED, gsm322_a_sel_first_plmn},

	{SBIT(GSM322_A1_TRYING_RPLMN),
	 GSM322_EVENT_ROAMING_NA, gsm322_a_sel_first_plmn},

	{SBIT(GSM322_A1_TRYING_RPLMN),
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_a_sel_first_plmn},

	{SBIT(GSM322_A1_TRYING_RPLMN) | SBIT(GSM322_A3_TRYING_PLMN),
	 GSM322_EVENT_REG_SUCCESS, gsm322_a_indicate_selected},

	{SBIT(GSM322_A2_ON_PLMN),
	 GSM322_EVENT_ROAMING_NA, gsm322_a_roaming_na},

	{SBIT(GSM322_A2_ON_PLMN),
	 GSM322_EVENT_HPLMN_SEARCH, gsm322_a_hplmn_search_start},

	{SBIT(GSM322_A2_ON_PLMN),
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_a_loss_of_radio},

	{SBIT(GSM322_A2_ON_PLMN),
	 GSM322_EVENT_USER_RESEL, gsm322_a_user_resel},

	{SBIT(GSM322_A3_TRYING_PLMN),
	 GSM322_EVENT_REG_FAILED, gsm322_a_sel_next_plmn},

	{SBIT(GSM322_A3_TRYING_PLMN),
	 GSM322_EVENT_ROAMING_NA, gsm322_a_sel_next_plmn},

	{SBIT(GSM322_A3_TRYING_PLMN),
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_a_sel_next_plmn},

	{SBIT(GSM322_A5_HPLMN_SEARCH),
	 GSM322_EVENT_CELL_FOUND, gsm322_a_sel_first_plmn},

	{SBIT(GSM322_A5_HPLMN_SEARCH),
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_a_go_on_plmn},

	{SBIT(GSM322_A4_WAIT_FOR_PLMN),
	 GSM322_EVENT_PLMN_AVAIL, gsm322_a_plmn_avail},

	{SBIT(GSM322_A4_WAIT_FOR_PLMN),
	 GSM322_EVENT_PLMN_SEARCH_END, gsm322_a_plmn_avail},

	{ALL_STATES,
	 GSM322_EVENT_SEL_MANUAL, gsm322_a_sel_manual},

	{ALL_STATES,
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_am_no_cell_found},
};

#define PLMNASLLEN \
	(sizeof(plmnastatelist) / sizeof(struct plmnastatelist))

static int gsm322_a_event(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_msg *gm = (struct gsm322_msg *) msg->data;
	int msg_type = gm->msg_type;
	int rc;
	int i;

	LOGP(DPLMN, LOGL_INFO, "(ms %s) Event '%s' for automatic PLMN "
		"selection in state '%s'\n", ms->name, get_event_name(msg_type),
		plmn_a_state_names[plmn->state]);
	/* find function for current state and message */
	for (i = 0; i < PLMNASLLEN; i++)
		if ((msg_type == plmnastatelist[i].type)
		 && ((1 << plmn->state) & plmnastatelist[i].states))
			break;
	if (i == PLMNASLLEN) {
		LOGP(DPLMN, LOGL_NOTICE, "Event unhandled at this state.\n");
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
	{SBIT(GSM322_M0_NULL),
	 GSM322_EVENT_SWITCH_ON, gsm322_m_switch_on},

	{SBIT(GSM322_M0_NULL) | SBIT(GSM322_M3_NOT_ON_PLMN) |
	 SBIT(GSM322_M2_ON_PLMN),
	 GSM322_EVENT_PLMN_SEARCH_END, gsm322_m_display_plmns},

	{ALL_STATES,
	 GSM322_EVENT_SWITCH_OFF, gsm322_m_switch_off},

	{SBIT(GSM322_M0_NULL) | SBIT(GSM322_M5_NO_SIM),
	 GSM322_EVENT_SIM_INSERT, gsm322_m_switch_on},

	{ALL_STATES,
	 GSM322_EVENT_SIM_INSERT, gsm322_m_sim_insert},

	{ALL_STATES,
	 GSM322_EVENT_SIM_REMOVE, gsm322_m_sim_removed},

	{SBIT(GSM322_M1_TRYING_RPLMN),
	 GSM322_EVENT_REG_FAILED, gsm322_m_display_plmns},

	{SBIT(GSM322_M1_TRYING_RPLMN),
	 GSM322_EVENT_ROAMING_NA, gsm322_m_display_plmns},

	{SBIT(GSM322_M1_TRYING_RPLMN),
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_m_display_plmns},

	{SBIT(GSM322_M1_TRYING_RPLMN),
	 GSM322_EVENT_REG_SUCCESS, gsm322_m_indicate_selected},

	{SBIT(GSM322_M2_ON_PLMN),
	 GSM322_EVENT_ROAMING_NA, gsm322_m_display_plmns},

	{SBIT(GSM322_M1_TRYING_RPLMN) | SBIT(GSM322_M2_ON_PLMN) |
	 SBIT(GSM322_M4_TRYING_PLMN),
	 GSM322_EVENT_INVALID_SIM, gsm322_m_sim_removed},

	{SBIT(GSM322_M3_NOT_ON_PLMN) | SBIT(GSM322_M2_ON_PLMN),
	 GSM322_EVENT_USER_RESEL, gsm322_m_user_resel},

	{SBIT(GSM322_M3_NOT_ON_PLMN),
	 GSM322_EVENT_PLMN_AVAIL, gsm322_m_plmn_avail},

	{SBIT(GSM322_M3_NOT_ON_PLMN),
	 GSM322_EVENT_CHOOSE_PLMN, gsm322_m_choose_plmn},

	{SBIT(GSM322_M4_TRYING_PLMN),
	 GSM322_EVENT_REG_SUCCESS, gsm322_m_go_on_plmn},

	{SBIT(GSM322_M4_TRYING_PLMN),
	 GSM322_EVENT_REG_FAILED, gsm322_m_display_plmns},

	{SBIT(GSM322_M4_TRYING_PLMN),
	 GSM322_EVENT_ROAMING_NA, gsm322_m_display_plmns},

	{SBIT(GSM322_M4_TRYING_PLMN),
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_m_display_plmns},

	{ALL_STATES,
	 GSM322_EVENT_SEL_AUTO, gsm322_m_sel_auto},

	{ALL_STATES,
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_am_no_cell_found},
};

#define PLMNMSLLEN \
	(sizeof(plmnmstatelist) / sizeof(struct plmnmstatelist))

static int gsm322_m_event(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_msg *gm = (struct gsm322_msg *) msg->data;
	int msg_type = gm->msg_type;
	int rc;
	int i;

	LOGP(DPLMN, LOGL_INFO, "(ms %s) Event '%s' for manual PLMN selection "
		"in state '%s'\n", ms->name, get_event_name(msg_type),
		plmn_m_state_names[plmn->state]);
	/* find function for current state and message */
	for (i = 0; i < PLMNMSLLEN; i++)
		if ((msg_type == plmnmstatelist[i].type)
		 && ((1 << plmn->state) & plmnmstatelist[i].states))
			break;
	if (i == PLMNMSLLEN) {
		LOGP(DPLMN, LOGL_NOTICE, "Event unhandled at this state.\n");
		return 0;
	}

	rc = plmnmstatelist[i].rout(ms, msg);

	return rc;
}

/* dequeue GSM 03.22 PLMN events */
int gsm322_plmn_dequeue(struct osmocom_ms *ms)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *msg;
	int work = 0;
	
	while ((msg = msgb_dequeue(&plmn->event_queue))) {
		/* send event to PLMN select process */
		if (ms->settings.plmn_mode == PLMN_MODE_AUTO)
			gsm322_a_event(ms, msg);
		else
			gsm322_m_event(ms, msg);
		msgb_free(msg);
		work = 1; /* work done */
	}
	
	return work;
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

	{ALL_STATES,
	 GSM322_EVENT_NEW_PLMN, gsm322_c_new_plmn},

	{ALL_STATES,
	 GSM322_EVENT_PLMN_SEARCH_START, gsm322_c_plmn_search},

	{SBIT(GSM322_C1_NORMAL_CELL_SEL) | SBIT(GSM322_C2_STORED_CELL_SEL) |
	 SBIT(GSM322_C4_NORMAL_CELL_RESEL) | SBIT(GSM322_C5_CHOOSE_CELL),
	 GSM322_EVENT_CELL_FOUND, gsm322_c_camp_normally},

	{SBIT(GSM322_C9_CHOOSE_ANY_CELL) | SBIT(GSM322_C6_ANY_CELL_SEL) |
	 SBIT(GSM322_C8_ANY_CELL_RESEL),
	 GSM322_EVENT_CELL_FOUND, gsm322_c_camp_any_cell},

	{SBIT(GSM322_C1_NORMAL_CELL_SEL) | SBIT(GSM322_C6_ANY_CELL_SEL) |
	 SBIT(GSM322_C9_CHOOSE_ANY_CELL) | SBIT(GSM322_C8_ANY_CELL_RESEL) |
	 SBIT(GSM322_C0_NULL),
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_c_any_cell_sel},

	{SBIT(GSM322_C2_STORED_CELL_SEL) | SBIT(GSM322_C5_CHOOSE_CELL) |
	 SBIT(GSM322_C4_NORMAL_CELL_RESEL),
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_c_normal_cell_sel},

	{SBIT(GSM322_C3_CAMPED_NORMALLY),
	 GSM322_EVENT_LEAVE_IDLE, gsm322_c_conn_mode_1},

	{SBIT(GSM322_C7_CAMPED_ANY_CELL),
	 GSM322_EVENT_LEAVE_IDLE, gsm322_c_conn_mode_2},

	{SBIT(GSM322_C3_CAMPED_NORMALLY),
	 GSM322_EVENT_RET_IDLE, gsm322_c_choose_cell},

	{SBIT(GSM322_C7_CAMPED_ANY_CELL),
	 GSM322_EVENT_RET_IDLE, gsm322_c_choose_any_cell},

	{SBIT(GSM322_C3_CAMPED_NORMALLY),
	 GSM322_EVENT_CELL_RESEL, gsm322_c_normal_cell_resel},

	{SBIT(GSM322_C7_CAMPED_ANY_CELL),
	 GSM322_EVENT_CELL_RESEL, gsm322_c_any_cell_resel},

	{SBIT(GSM322_C7_CAMPED_ANY_CELL),
	 GSM322_EVENT_CELL_FOUND, gsm322_c_normal_cell_sel},

	{SBIT(GSM322_C1_NORMAL_CELL_SEL) | SBIT(GSM322_C2_STORED_CELL_SEL) |
	 SBIT(GSM322_C4_NORMAL_CELL_RESEL) | SBIT(GSM322_C5_CHOOSE_CELL) |
	 SBIT(GSM322_C9_CHOOSE_ANY_CELL) | SBIT(GSM322_C8_ANY_CELL_RESEL) |
	 SBIT(GSM322_C6_ANY_CELL_SEL) | SBIT(GSM322_PLMN_SEARCH),
	 GSM322_EVENT_SYSINFO, gsm322_c_scan_sysinfo_bcch},

	{SBIT(GSM322_C3_CAMPED_NORMALLY) | SBIT(GSM322_C7_CAMPED_ANY_CELL),
	 GSM322_EVENT_SYSINFO, gsm322_c_camp_sysinfo_bcch},

	{SBIT(GSM322_C3_CAMPED_NORMALLY),
	 GSM322_EVENT_HPLMN_SEARCH, gsm322_c_hplmn_search},
};

#define CELLSELSLLEN \
	(sizeof(cellselstatelist) / sizeof(struct cellselstatelist))

int gsm322_c_event(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm322_msg *gm = (struct gsm322_msg *) msg->data;
	int msg_type = gm->msg_type;
	int rc;
	int i;

	if (msg_type != GSM322_EVENT_SYSINFO)
		LOGP(DCS, LOGL_INFO, "(ms %s) Event '%s' for Cell selection "
			"in state '%s'\n", ms->name, get_event_name(msg_type),
			cs_state_names[cs->state]);
	/* find function for current state and message */
	for (i = 0; i < CELLSELSLLEN; i++)
		if ((msg_type == cellselstatelist[i].type)
		 && ((1 << cs->state) & cellselstatelist[i].states))
			break;
	if (i == CELLSELSLLEN) {
		LOGP(DCS, LOGL_NOTICE, "Event unhandled at this state.\n");
		return 0;
	}

	rc = cellselstatelist[i].rout(ms, msg);

	return rc;
}

/* dequeue GSM 03.22 cell selection events */
int gsm322_cs_dequeue(struct osmocom_ms *ms)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *msg;
	int work = 0;
	
	while ((msg = msgb_dequeue(&cs->event_queue))) {
		/* send event to cell selection process */
		gsm322_c_event(ms, msg);
		msgb_free(msg);
		work = 1; /* work done */
	}
	
	return work;
}

/*
 * dump lists
 */

int gsm322_dump_sorted_plmn(struct osmocom_ms *ms)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_plmn_list *temp;

	printf("MCC    |MNC    |allowed|rx-lev\n");
	printf("-------+-------+-------+-------\n");
	llist_for_each_entry(temp, &plmn->sorted_plmn, entry) {
		printf("%s    |%s%s    |%s    |%s\n", gsm_print_mcc(temp->mcc),
			gsm_print_mnc(temp->mnc),
			((temp->mnc & 0x00f) == 0x00f) ? " ":"",
			(temp->cause) ? "no ":"yes",
			gsm_print_rxlev(temp->rxlev));
	}

	return 0;
}

int gsm322_dump_cs_list(struct gsm322_cellsel *cs, uint8_t flags,
			void (*print)(void *, const char *, ...), void *priv)
{
	int i;
	struct gsm48_sysinfo *s;

	print(priv, "arfcn  |MCC    |MNC    |LAC    |cell ID|forb.LA|prio   |"
		"min-db |max-pwr|rx-lev\n");
	print(priv, "-------+-------+-------+-------+-------+-------+-------+"
		"-------+-------+-------\n");
	for (i = 0; i <= 1023; i++) {
		s = cs->list[i].sysinfo;
		if (!s || !(cs->list[i].flags & flags))
			continue;
		print(priv, "%4d   |", i);
		if ((cs->list[i].flags & GSM322_CS_FLAG_SYSINFO)) {
			print(priv, "%s    |%s%s    |", gsm_print_mcc(s->mcc),
				gsm_print_mnc(s->mnc),
				((s->mnc & 0x00f) == 0x00f) ? " ":"");
			print(priv, "0x%04x |0x%04x |", s->lac, s->cell_id);
			if ((cs->list[i].flags & GSM322_CS_FLAG_FORBIDD))
				print(priv, "yes    |");
			else
				print(priv, "no     |");
			if ((cs->list[i].flags & GSM322_CS_FLAG_BARRED))
				print(priv, "barred |");
			else {
				if (cs->list[i].sysinfo->cell_barr)
					print(priv, "low    |");
				else
					print(priv, "normal |");
			}
			print(priv, "%4d   |%4d   |%s\n", s->rxlev_acc_min_db,
				s->ms_txpwr_max_cch,
				gsm_print_rxlev(cs->list[i].rxlev));
		} else
			print(priv, "n/a    |n/a    |n/a    |n/a    |n/a    |"
				"n/a    |n/a    |n/a\n");
	}
	print(priv, "\n");

	return 0;
}

int gsm322_dump_forbidden_la(struct osmocom_ms *ms,
			void (*print)(void *, const char *, ...), void *priv)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_la_list *temp;

	print(priv, "MCC    |MNC    |LAC    |cause\n");
	print(priv, "-------+-------+-------+-------\n");
	llist_for_each_entry(temp, &plmn->forbidden_la, entry)
		print(priv, "%s    |%s%s    |0x%04x |#%d\n",
			gsm_print_mcc(temp->mcc), gsm_print_mnc(temp->mnc),
			((temp->mnc & 0x00f) == 0x00f) ? " ":"",
			temp->lac, temp->cause);

	return 0;
}

int gsm322_dump_ba_list(struct gsm322_cellsel *cs, uint16_t mcc, uint16_t mnc,
			void (*print)(void *, const char *, ...), void *priv)
{
	struct gsm322_ba_list *ba;
	int i;

	llist_for_each_entry(ba, &cs->ba_list, entry) {
		if (mcc && mnc && (mcc != ba->mcc || mnc != ba->mnc))
			continue;
		print(priv, "Band Allocation of network: MCC %s MNC %s "
			"(%s, %s)\n", gsm_print_mcc(ba->mcc),
			gsm_print_mnc(ba->mnc), gsm_get_mcc(ba->mcc),
			gsm_get_mnc(ba->mcc, ba->mnc));
		for (i = 0; i <= 1023; i++) {
			if ((ba->freq[i >> 3] & (1 << (i & 7))))
				print(priv, " %d", i);
		}
		print(priv, "\n");
	}

	return 0;
}

/*
 * initialization
 */

int gsm322_init(struct osmocom_ms *ms)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;
	FILE *fp;
	char filename[128];
	int i;
	struct gsm322_ba_list *ba;
	uint8_t buf[4];

	LOGP(DPLMN, LOGL_INFO, "init PLMN process\n");
	LOGP(DCS, LOGL_INFO, "init Cell Selection process\n");

	memset(plmn, 0, sizeof(*plmn));
	memset(cs, 0, sizeof(*cs));
	plmn->ms = ms;
	cs->ms = ms;

	/* set initial state */
	plmn->state = 0;
	cs->state = 0;
	ms->settings.plmn_mode = PLMN_MODE_AUTO;

	/* init lists */
	INIT_LLIST_HEAD(&plmn->event_queue);
	INIT_LLIST_HEAD(&cs->event_queue);
	INIT_LLIST_HEAD(&plmn->sorted_plmn);
	INIT_LLIST_HEAD(&plmn->forbidden_la);
	INIT_LLIST_HEAD(&cs->ba_list);

	/* set supported frequencies in cell selection list */
	for (i = 0; i <= 1023; i++)
		if ((ms->support.freq_map[i >> 3] & (1 << (i & 7))))
			cs->list[i].flags |= GSM322_CS_FLAG_SUPPORT;

	/* read BA list */
	sprintf(filename, "/etc/osmocom/%s.ba", ms->name);
	fp = fopen(filename, "r");
	if (fp) {
		int rc;

		while(!feof(fp)) {
			ba = talloc_zero(l23_ctx, struct gsm322_ba_list);
			if (!ba)
				return -ENOMEM;
			rc = fread(buf, 4, 1, fp);
			if (!rc) {
				talloc_free(ba);
				break;
			}
			ba->mcc = (buf[0] << 8) | buf[1];
			ba->mnc = (buf[2] << 8) | buf[3];
			rc = fread(ba->freq, sizeof(ba->freq), 1, fp);
			if (!rc) {
				talloc_free(ba);
				break;
			}
			llist_add_tail(&ba->entry, &cs->ba_list);
			LOGP(DCS, LOGL_INFO, "Read stored BA list (mcc=%s "
				"mnc=%s  %s, %s)\n", gsm_print_mcc(ba->mcc),
				gsm_print_mnc(ba->mnc), gsm_get_mcc(ba->mcc),
				gsm_get_mnc(ba->mcc, ba->mnc));
		}
		fclose(fp);
	} else
		LOGP(DCS, LOGL_INFO, "No stored BA list\n");

	register_signal_handler(SS_L1CTL, &gsm322_l1_signal, NULL);

	return 0;
}

int gsm322_exit(struct osmocom_ms *ms)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct llist_head *lh, *lh2;
	struct msgb *msg;
	FILE *fp;
	char filename[128];
	struct gsm322_ba_list *ba;
	uint8_t buf[4];
	int i;

	LOGP(DPLMN, LOGL_INFO, "exit PLMN process\n");
	LOGP(DCS, LOGL_INFO, "exit Cell Selection process\n");

	unregister_signal_handler(SS_L1CTL, &gsm322_l1_signal, NULL);

	/* stop cell selection process (if any) */
	new_c_state(cs, GSM322_C0_NULL);

	/* stop timers */
	stop_cs_timer(cs);
	stop_plmn_timer(plmn);

	/* flush sysinfo */
	for (i = 0; i <= 1023; i++) {
		if (cs->list[i].sysinfo) {
			LOGP(DCS, LOGL_INFO, "free sysinfo arfcn=%d\n", i);
			talloc_free(cs->list[i].sysinfo);
			cs->list[i].sysinfo = NULL;
		}
		cs->list[i].flags = 0;
	}

	/* store BA list */
	sprintf(filename, "/etc/osmocom/%s.ba", ms->name);
	fp = fopen(filename, "w");
	if (fp) {
		int rc;

		llist_for_each_entry(ba, &cs->ba_list, entry) {
			buf[0] = ba->mcc >> 8;
			buf[1] = ba->mcc & 0xff;
			buf[2] = ba->mnc >> 8;
			buf[3] = ba->mnc & 0xff;
			rc = fwrite(buf, 4, 1, fp);
			rc = fwrite(ba->freq, sizeof(ba->freq), 1, fp);
			LOGP(DCS, LOGL_INFO, "Write stored BA list (mcc=%s "
				"mnc=%s  %s, %s)\n", gsm_print_mcc(ba->mcc),
				gsm_print_mnc(ba->mnc), gsm_get_mcc(ba->mcc),
				gsm_get_mnc(ba->mcc, ba->mnc));
		}
		fclose(fp);
	} else
		LOGP(DCS, LOGL_ERROR, "Failed to write BA list\n");

	/* free lists */
	while ((msg = msgb_dequeue(&plmn->event_queue)))
		msgb_free(msg);
	while ((msg = msgb_dequeue(&cs->event_queue)))
		msgb_free(msg);
	llist_for_each_safe(lh, lh2, &plmn->sorted_plmn) {
		llist_del(lh);
		talloc_free(lh);
	}
	llist_for_each_safe(lh, lh2, &plmn->forbidden_la) {
		llist_del(lh);
		talloc_free(lh);
	}
	llist_for_each_safe(lh, lh2, &cs->ba_list) {
		llist_del(lh);
		talloc_free(lh);
	}
	return 0;
}


