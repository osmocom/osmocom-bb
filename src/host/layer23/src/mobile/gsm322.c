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
#include <limits.h>
#include <time.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/core/signal.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/mobile/vty.h>
#include <osmocom/bb/mobile/app_mobile.h>

#include <l1ctl_proto.h>

const char *ba_version = "osmocom BA V1\n";

extern void *l23_ctx;

static void gsm322_cs_timeout(void *arg);
static int gsm322_cs_select(struct osmocom_ms *ms, int index, uint16_t mcc,
	uint16_t mnc, int any);
static int gsm322_m_switch_on(struct osmocom_ms *ms, struct msgb *msg);
static void gsm322_any_timeout(void *arg);
static int gsm322_nb_scan(struct osmocom_ms *ms);
static int gsm322_nb_synced(struct gsm322_cellsel *cs, int yes);
static int gsm322_nb_read(struct gsm322_cellsel *cs, int yes);
static int gsm322_c_camp_any_cell(struct osmocom_ms *ms, struct msgb *msg);
static int gsm322_nb_start(struct osmocom_ms *ms, int synced);
static void gsm322_cs_loss(void *arg);
static int gsm322_nb_meas_ind(struct osmocom_ms *ms, uint16_t arfcn,
	uint8_t rx_lev);

#define SYNC_RETRIES		1
#define SYNC_RETRIES_SERVING	2

/* time for trying to sync and read BCCH of neighbour cell again
 * NOTE: This value is not defined by TS, i think. */
#define GSM58_TRY_AGAIN		30

/* time for reading BCCH of neighbour cell again */
#define GSM58_READ_AGAIN	300

/* number of neighbour cells to monitor */
#define GSM58_NB_NUMBER		6

/* Timeout for reading BCCH of neighbour cells */
#define GSM322_NB_TIMEOUT	2

/* number of neighbour cells to measure for average */
#define RLA_C_NUM		4

/* wait before doing neighbour cell reselecton due to a better cell again */
#define GSM58_RESEL_THRESHOLD	15

#define ARFCN_TEXT_LEN	10

//#define TEST_INCLUDE_SERV

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
 * - cs->list[0..(1023+299)].xxx for each cell, where
 *  - flags and rxlev are used to store outcome of cell scanning process
 *  - sysinfo pointing to sysinfo memory, allocated temporarily
 * - cs->selected and cs->sel_* states of the current / last selected cell.
 *
 *
 * There are special states: GSM322_HPLMN_SEARCH, GSM322_PLMN_SEARCH
 * and GSM322_ANY_SEARCH:
 *
 * GSM322_HPLMN_SEARCH is used to find a HPLMN. This is triggered
 * by automatic cell selection.
 *
 * GSM322_PLMN_SEARCH is triggered when network search process is started.
 * It will do a complete search. Also it is used before selecting PLMN from list.
 *
 * GSM322_ANY_SEARCH is similar to GSM322_PLMN_SEARCH, but it is done while
 * camping on any cell. If there is a suitable and allowable cell found,
 * it is indicated to the PLMN search process.
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
 * uint8_t	freq[128+38];
 * where frequency 0 is bit 0 of first byte
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
 * * cs->list[1024+299]
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
 *
 *
 * Cell re-selection process
 *
 * The cell re-selection process takes place when a "serving cell" is selected.
 * The neighbour cells to be monitored for re-selection are given via SI2* of
 * the serving cell.
 *
 * Therefore a list of neighbour cells is created or updated, when the cell
 * allocation is received or changed by the network.
 *
 * All neighbour cells are monitored, but only up to 6 of the strongest cells
 * are synced to, in order to read the BCCH data. A timer is used to re-read
 * the BCCH data after 5 minutes. This timer is also used if sync or read
 * fails.
 *
 * The C1 and C2 criterion is calculated for the currently monitored neigbour
 * cells. During this process, a better neighbour cell will trigger cell
 * re-selection.
 *
 * The cell re-selection is similar to the cell selection process, except that
 * only neighbour cells are searched in order of their quality criterion C2.
 *
 * During camping, and monitoring neighbour cells, it is possible to enter
 * dedicated mode at any time.
 *
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

uint16_t index2arfcn(int index)
{
	if (index >= 1024)
		return (index-1024+512) | ARFCN_PCS;
	return index;
}

int arfcn2index(uint16_t arfcn)
{
	int is_pcs = arfcn & ARFCN_PCS;
	arfcn &= ~ARFCN_FLAG_MASK;
	if ((is_pcs) && (arfcn >= 512) && (arfcn <= 810))
		return (arfcn & 1023)-512+1024;
	return arfcn & 1023;
}


static char *bargraph(int value, int min, int max)
{
	static char bar[128];

	/* shift value to the range of min..max */
	if (value < min)
		value = 0;
	else if (value > max)
		value = max - min;
	else
		value -= min;

	memset(bar, '=', value);
	bar[value] = '\0';

	return bar;
}

static int class_of_band(struct osmocom_ms *ms, int band)
{
	struct gsm_settings *set = &ms->settings;

	switch (band) {
	case GSM_BAND_450:
	case GSM_BAND_480:
		return set->class_400;
		break;
	case GSM_BAND_850:
		return set->class_850;
		break;
	case GSM_BAND_1800:
		return set->class_dcs;
		break;
	case GSM_BAND_1900:
		return set->class_pcs;
		break;
	}

	return set->class_900;
}

char *gsm_print_rxlev(uint8_t rxlev)
{
	static char string[5];
	if (rxlev == 0)
		return "<=-110";
	if (rxlev >= 63)
		return ">=-47";
	sprintf(string, "-%d", 110 - rxlev);
	return string;
}

/* GSM 05.08 6.4 (special class 3 DCS 1800 MS case is omitted ) */
static int16_t calculate_c1(int log, int8_t rla_c, int8_t rxlev_acc_min,
	int8_t ms_txpwr_max_cch, int8_t p)
{
	int16_t a, b, c1, max_b_0;

	a = rla_c - rxlev_acc_min;
	b = ms_txpwr_max_cch - p;

	max_b_0 = (b > 0) ? b : 0;

	c1 = a - max_b_0;

	LOGP(log, LOGL_INFO, "A (RLA_C (%d) - RXLEV_ACC_MIN (%d)) = %d\n",
		rla_c, rxlev_acc_min, a);
	LOGP(log, LOGL_INFO, "B (MS_TXPWR_MAX_CCH (%d) - p (%d)) = %d\n",
		ms_txpwr_max_cch, p, b);
	LOGP(log, LOGL_INFO, "C1 (A - MAX(B,0)) = %d\n", c1);

	return c1;
}

static int16_t calculate_c2(int16_t c1, int serving, int last_serving,
	int cell_resel_param_ind, uint8_t cell_resel_off, int t,
	uint8_t penalty_time, uint8_t temp_offset) {
	int16_t c2;

	c2 = c1;

	/* no reselect parameters. same process for serving and neighbour cells */
	if (!cell_resel_param_ind) {
		LOGP(DNB, LOGL_INFO, "C2 = C1 = %d (because no extended "
			"re-selection parameters available)\n", c2);
		return c2;
	}

	/* special case, if PENALTY_TIME is '11111' */
	if (penalty_time == 31) {
		c2 -= (cell_resel_off << 1);
		LOGP(DNB, LOGL_INFO, "C2 = C1 - CELL_RESELECT_OFFSET (%d) = %d "
			"(special case)\n", cell_resel_off, c2);
		return c2;
	}

	c2 += (cell_resel_off << 1);

	/* parameters for serving cell */
	if (serving) {
		LOGP(DNB, LOGL_INFO, "C2 = C1 + CELL_RESELECT_OFFSET (%d) = %d "
			"(serving cell)\n", cell_resel_off, c2);
		return c2;
	}

	/*  the cell is the last serving cell */
	if (last_serving) {
		LOGP(DNB, LOGL_INFO, "C2 = C1 + CELL_RESELECT_OFFSET (%d) = %d "
			"(last serving cell)\n", cell_resel_off, c2);
		return c2;
	}

	/*  penatly time reached */
	if (t >= (penalty_time + 1) * 20) {
		LOGP(DNB, LOGL_INFO, "C2 = C1 + CELL_RESELECT_OFFSET (%d) = %d "
			"(PENALTY_TIME reached)\n", cell_resel_off, c2);
		return c2;
	}

	/* penalty time not reached, substract temporary offset */
	if (temp_offset < 7)
		c2 -= temp_offset * 10;
	else
		c2 = -1000; /* infinite  */
	LOGP(DNB, LOGL_INFO, "C2 = C1 + CELL_RESELECT_OFFSET (%d) = %d "
		"(PENALTY_TIME not reached, %d seconds left)\n", cell_resel_off,
		c2, (penalty_time + 1) * 20 - t);
	return c2;
}

static int gsm322_sync_to_cell(struct gsm322_cellsel *cs,
	struct gsm322_neighbour * neighbour, int camping)
{
	struct osmocom_ms *ms = cs->ms;
	struct gsm48_sysinfo *s = cs->si;
	struct rx_meas_stat *meas = &ms->meas;

	if (cs->sync_pending) {
		LOGP(DCS, LOGL_INFO, "Sync to ARFCN=%s, but there is a sync "
			"already pending\n",gsm_print_arfcn(cs->arfcn));
		return 0;
	}

	cs->ccch_state = GSM322_CCCH_ST_INIT;
	if (s && s->si3) {
		if (s->ccch_conf == 1) {
			LOGP(DCS, LOGL_INFO, "Sync to ARFCN=%s rxlev=%s "
				"(Sysinfo, ccch mode COMB)\n",
				gsm_print_arfcn(cs->arfcn),
				gsm_print_rxlev(cs->list[cs->arfci].rxlev));
			cs->ccch_mode = CCCH_MODE_COMBINED;
		} else {
			LOGP(DCS, LOGL_INFO, "Sync to ARFCN=%s rxlev=%s "
				"(Sysinfo, ccch mode NON-COMB)\n",
				gsm_print_arfcn(cs->arfcn),
				gsm_print_rxlev(cs->list[cs->arfci].rxlev));
			cs->ccch_mode = CCCH_MODE_NON_COMBINED;
		}
	} else {
		LOGP(DCS, LOGL_INFO, "Sync to ARFCN=%s rxlev=%s (No sysinfo "
			"yet, ccch mode NONE)\n", gsm_print_arfcn(cs->arfcn),
			gsm_print_rxlev(cs->list[cs->arfci].rxlev));
		cs->ccch_mode = CCCH_MODE_NONE;
	}

	meas->frames = meas->snr = meas->berr = meas->rxlev = 0;
	cs->rxlev_sum_dbm = cs->rxlev_count = 0;

	cs->neighbour = neighbour;

	if (camping) {
		cs->rla_c_dbm = -128;
		cs->c12_valid = 0;
		/* keep neighbour cells! if they are old, they are re-read
		 * anyway, because re-read timer has expired. */
	}

	cs->sync_pending = 1;
	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	return l1ctl_tx_fbsb_req(ms, cs->arfcn,
			L1CTL_FBSB_F_FB01SB, 100, 0,
			cs->ccch_mode,
			cs->list[cs->arfci].rxlev);
}

/* this is called whenever the serving cell is unselectied */
static void gsm322_unselect_cell(struct gsm322_cellsel *cs)
{
	if (!cs->selected)
		return;

	LOGP(DCS, LOGL_INFO, "Unselecting serving cell.\n");

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
	static char buffer[256] = "";
	int in = strlen(buffer);
	va_list args;

	va_start(args, fmt);
	vsnprintf(buffer + in, sizeof(buffer) - in - 1, fmt, args);
	buffer[sizeof(buffer) - in - 1] = '\0';
	va_end(args);

	if (buffer[0] && buffer[strlen(buffer) - 1] == '\n') {
		LOGP(DCS, LOGL_INFO, "%s", buffer);
		buffer[0] = '\0';
	}
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
int gsm322_is_plmn_avail_and_allow(struct gsm322_cellsel *cs, uint16_t mcc,
	uint16_t mnc)
{
	int i;

	for (i = 0; i <= 1023+299; i++) {
		if ((cs->list[i].flags & GSM322_CS_FLAG_TEMP_AA)
		 && cs->list[i].sysinfo
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

	for (i = 0; i <= 1023+299; i++) {
		if ((cs->list[i].flags & GSM322_CS_FLAG_SYSINFO)
		 && cs->list[i].sysinfo
		 && gsm_match_mnc(cs->list[i].sysinfo->mcc,
			cs->list[i].sysinfo->mnc, imsi))
			return 1;
	}

	return 0;
}

static const struct value_string gsm322_nb_state_names[] = {
	{ GSM322_NB_NEW,	"new" },
	{ GSM322_NB_NOT_SUP,	"not sup" },
	{ GSM322_NB_RLA_C,	"RLA_C" },
	{ GSM322_NB_NO_SYNC,	"no sync" },
	{ GSM322_NB_NO_BCCH,	"no BCCH" },
	{ GSM322_NB_SYSINFO,	"SYSINFO" },
	{ 0,			NULL }
};

const char *get_nb_state_name(int value)
{
	return get_value_string(gsm322_nb_state_names, value);
}


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
	osmo_timer_schedule(&plmn->timer, secs, 0);
}

/* stop plmn search timer */
static void stop_plmn_timer(struct gsm322_plmn *plmn)
{
	if (osmo_timer_pending(&plmn->timer)) {
		LOGP(DPLMN, LOGL_INFO, "Stopping pending timer.\n");
		osmo_timer_del(&plmn->timer);
	}
}

/* start cell selection timer */
void start_cs_timer(struct gsm322_cellsel *cs, int sec, int micro)
{
	LOGP(DCS, LOGL_DEBUG, "Starting CS timer with %d seconds.\n", sec);
	cs->timer.cb = gsm322_cs_timeout;
	cs->timer.data = cs;
	osmo_timer_schedule(&cs->timer, sec, micro);
}

/* stop cell selection timer */
static void stop_cs_timer(struct gsm322_cellsel *cs)
{
	if (osmo_timer_pending(&cs->timer)) {
		LOGP(DCS, LOGL_DEBUG, "stopping pending CS timer.\n");
		osmo_timer_del(&cs->timer);
	}
}

/* the following timer is used to search again for allowable cell, after
 * loss of coverage. (loss of any allowed PLMN) */

/* start any cell selection timer */
void start_any_timer(struct gsm322_cellsel *cs, int sec, int micro)
{
	LOGP(DCS, LOGL_DEBUG, "Starting 'any cell selection' timer with %d "
		"seconds.\n", sec);
	cs->any_timer.cb = gsm322_any_timeout;
	cs->any_timer.data = cs;
	osmo_timer_schedule(&cs->any_timer, sec, micro);
}

/* stop cell selection timer */
static void stop_any_timer(struct gsm322_cellsel *cs)
{
	if (osmo_timer_pending(&cs->any_timer)) {
		LOGP(DCS, LOGL_DEBUG, "stopping pending 'any cell selection' "
			"timer.\n");
		osmo_timer_del(&cs->any_timer);
	}
}

/*
 * state change
 */

static const struct value_string gsm322_a_state_names[] = {
	{ GSM322_A0_NULL,		"A0 null"},
	{ GSM322_A1_TRYING_RPLMN,	"A1 trying RPLMN"},
	{ GSM322_A2_ON_PLMN,		"A2 on PLMN"},
	{ GSM322_A3_TRYING_PLMN,	"A3 trying PLMN"},
	{ GSM322_A4_WAIT_FOR_PLMN,	"A4 wait for PLMN to appear"},
	{ GSM322_A5_HPLMN_SEARCH,	"A5 HPLMN search"},
	{ GSM322_A6_NO_SIM,		"A6 no SIM inserted"},
	{ 0,				NULL }
};

const char *get_a_state_name(int value)
{
	return get_value_string(gsm322_a_state_names, value);
}

static const struct value_string gsm322_m_state_names[] = {
	{ GSM322_M0_NULL,		"M0 null"},
	{ GSM322_M1_TRYING_RPLMN,	"M1 trying RPLMN"},
	{ GSM322_M2_ON_PLMN,		"M2 on PLMN"},
	{ GSM322_M3_NOT_ON_PLMN,	"M3 not on PLMN"},
	{ GSM322_M4_TRYING_PLMN,	"M4 trying PLMN"},
	{ GSM322_M5_NO_SIM,		"M5 no SIM inserted"},
	{ 0,				NULL }
};

const char *get_m_state_name(int value)
{
	return get_value_string(gsm322_m_state_names, value);
}

static const struct value_string gsm322_cs_state_names[] = {
	{ GSM322_C0_NULL,		"C0 null"},
	{ GSM322_C1_NORMAL_CELL_SEL,	"C1 normal cell selection"},
	{ GSM322_C2_STORED_CELL_SEL,	"C2 stored cell selection"},
	{ GSM322_C3_CAMPED_NORMALLY,	"C3 camped normally"},
	{ GSM322_C4_NORMAL_CELL_RESEL,	"C4 normal cell re-selection"},
	{ GSM322_C5_CHOOSE_CELL,	"C5 choose cell"},
	{ GSM322_C6_ANY_CELL_SEL,	"C6 any cell selection"},
	{ GSM322_C7_CAMPED_ANY_CELL,	"C7 camped on any cell"},
	{ GSM322_C8_ANY_CELL_RESEL,	"C8 any cell re-selection"},
	{ GSM322_C9_CHOOSE_ANY_CELL,	"C9 choose any cell"},
	{ GSM322_CONNECTED_MODE_1,	"connected mode 1"},
	{ GSM322_CONNECTED_MODE_2,	"connected mode 2"},
	{ GSM322_PLMN_SEARCH,		"PLMN search"},
	{ GSM322_HPLMN_SEARCH,		"HPLMN search"},
	{ GSM322_ANY_SEARCH,		"ANY search"},
	{ 0,				NULL }
};

const char *get_cs_state_name(int value)
{
	return get_value_string(gsm322_cs_state_names, value);
}

/* new automatic PLMN search state */
static void new_a_state(struct gsm322_plmn *plmn, int state)
{
	if (plmn->ms->settings.plmn_mode != PLMN_MODE_AUTO) {
		LOGP(DPLMN, LOGL_FATAL, "not in auto mode, please fix!\n");
		return;
	}

	stop_plmn_timer(plmn);

	LOGP(DPLMN, LOGL_INFO, "new state '%s' -> '%s'\n",
		get_a_state_name(plmn->state), get_a_state_name(state));

	plmn->state = state;
}

/* new manual PLMN search state */
static void new_m_state(struct gsm322_plmn *plmn, int state)
{
	if (plmn->ms->settings.plmn_mode != PLMN_MODE_MANUAL) {
		LOGP(DPLMN, LOGL_FATAL, "not in manual mode, please fix!\n");
		return;
	}

	LOGP(DPLMN, LOGL_INFO, "new state '%s' -> '%s'\n",
		get_m_state_name(plmn->state), get_m_state_name(state));

	plmn->state = state;
}

/* new Cell selection state */
static void new_c_state(struct gsm322_cellsel *cs, int state)
{
	LOGP(DCS, LOGL_INFO, "new state '%s' -> '%s'\n",
		get_cs_state_name(cs->state), get_cs_state_name(state));

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
	uint8_t search = 0;

	/* flush list */
	llist_for_each_safe(lh, lh2, &plmn->sorted_plmn) {
		llist_del(lh);
		talloc_free(lh);
	}

	/* Create a temporary list of all networks */
	INIT_LLIST_HEAD(&temp_list);
	for (i = 0; i <= 1023+299; i++) {
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

/* go to Wait for PLMNs to appear state */
static int gsm322_a_go_wait_for_plmns(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;
	struct gsm322_msg *ngm;

	new_a_state(plmn, GSM322_A4_WAIT_FOR_PLMN);

	/* we must forward this, otherwhise "Any cell selection"
	 * will not start automatically.
	 */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
	if (!nmsg)
		return -ENOMEM;
	ngm = (struct gsm322_msg *) nmsg->data;
	ngm->limited = 1;
	gsm322_cs_sendmsg(ms, nmsg);

	return 0;
}

/* no (more) PLMN in list */
static int gsm322_a_no_more_plmn(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *nmsg;
	int found;

	/* any allowable PLMN available? */
	found = gsm322_cs_select(ms, -1, 0, 0, 0);

	/* if no PLMN in list:
	 * this means that we are at a point where we camp on any cell or
	 * no cell ist available. */
	if (found < 0) {
		if (subscr->plmn_valid) {
			LOGP(DPLMN, LOGL_INFO, "Not any PLMN allowable. "
				"Do limited search with RPLMN.\n");
			plmn->mcc = subscr->plmn_mcc;
			plmn->mnc = subscr->plmn_mnc;
		} else
		if (subscr->sim_valid) {
			LOGP(DPLMN, LOGL_INFO, "Not any PLMN allowable. "
				"Do limited search with HPLMN.\n");
			plmn->mcc = subscr->mcc;
			plmn->mnc = subscr->mnc;
		} else {
			LOGP(DPLMN, LOGL_INFO, "Not any PLMN allowable. "
				"Do limited search with no PLMN.\n");
			plmn->mcc = 0;
			plmn->mnc = 0;
		}

		return gsm322_a_go_wait_for_plmns(ms, msg);
	}

	/* select first PLMN in list */
	plmn->mcc = cs->list[found].sysinfo->mcc;
	plmn->mnc = cs->list[found].sysinfo->mnc;

	LOGP(DPLMN, LOGL_INFO, "PLMN available after searching PLMN list "
		"(mcc=%s mnc=%s  %s, %s)\n",
		gsm_print_mcc(plmn->mcc), gsm_print_mnc(plmn->mnc),
		gsm_get_mcc(plmn->mcc), gsm_get_mnc(plmn->mcc, plmn->mnc));

	/* indicate New PLMN */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

	/* go On PLMN */
	return gsm322_a_go_on_plmn(ms, msg);
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
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm322_plmn_list *plmn_entry;
	struct gsm322_plmn_list *plmn_found = NULL;
	struct msgb *nmsg;

	if (!subscr->sim_valid) {
		return 0;
	}

	/* try again later, if not idle */
	if (cs->state == GSM322_CONNECTED_MODE_1
	 || cs->state == GSM322_CONNECTED_MODE_2) {
		LOGP(DPLMN, LOGL_INFO, "Not idle, rejecting.\n");

		return 0;
	}

	/* search current PLMN in list */
	llist_for_each_entry(plmn_entry, &plmn->sorted_plmn, entry) {
		if (plmn_entry->mcc == plmn->mcc
		 && plmn_entry->mnc == plmn->mnc) {
			plmn_found = plmn_entry;
			break;
		}
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

	/* tell MM that we selected a PLMN */
	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_USER_PLMN_SEL);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(ms, nmsg);

	/* select first PLMN in list */
	return gsm322_a_sel_first_plmn(ms, msg);
}

/* PLMN becomes available */
static int gsm322_a_plmn_avail(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm322_msg *gm = (struct gsm322_msg *) msg->data;

	if (subscr->plmn_valid && plmn->mcc == gm->mcc
	 && plmn->mnc == gm->mnc) {
		struct msgb *nmsg;

		new_m_state(plmn, GSM322_A1_TRYING_RPLMN);

		LOGP(DPLMN, LOGL_INFO, "Last selected PLMN becomes available "
			"again.\n");

		/* indicate New PLMN */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
		if (!nmsg)
			return -ENOMEM;
		gsm322_cs_sendmsg(ms, nmsg);

		return 0;

	} else {
		/* select first PLMN in list */
		LOGP(DPLMN, LOGL_INFO, "Some PLMN became available, start PLMN "
			"search process.\n");
		return gsm322_a_sel_first_plmn(ms, msg);
	}
}

/* loss of radio coverage */
static int gsm322_a_loss_of_radio(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int found;

	/* any allowable PLMN available */
	found = gsm322_cs_select(ms, -1, 0, 0, 0);

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

	LOGP(DPLMN, LOGL_INFO, "PLMN not available after loss of coverage.\n");

	return gsm322_a_go_wait_for_plmns(ms, msg);
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

	plmn->mcc = plmn->mnc = 0;

	/* initiate search at cell selection */
	LOGP(DSUM, LOGL_INFO, "Search for network\n");
	LOGP(DPLMN, LOGL_INFO, "Switch on, no RPLMN, start PLMN search "
		"first.\n");

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

	/* flush list of PLMNs */
	gsm_subscr_del_forbidden_plmn(&ms->subscr, 0, 0);

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
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *nmsg;

	/* try again later, if not idle and not camping */
	if (cs->state != GSM322_C3_CAMPED_NORMALLY) {
		LOGP(DPLMN, LOGL_INFO, "Not camping normally, wait some more."
			"\n");
		start_plmn_timer(plmn, 60);

		return 0;
	}

	new_a_state(plmn, GSM322_A5_HPLMN_SEARCH);

	/* initiate search at cell selection */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_HPLMN_SEARCH);
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
	struct msgb *nmsg;
	struct gsm322_msg *ngm;

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

	/* we must forward this, otherwhise "Any cell selection"
	 * will not start automatically.
	 * this way we get back to the last PLMN, in case we gained
	 * our coverage back.
	 */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
	if (!nmsg)
		return -ENOMEM;
	ngm = (struct gsm322_msg *) nmsg->data;
	ngm->limited = 1;
	gsm322_cs_sendmsg(ms, nmsg);

	return 0;
}

/* user starts reselection */
static int gsm322_m_user_resel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;

	/* unselect PLMN. after search, the process will wait until a PLMN is
	 * selected by the user. this prevents from switching back to the
	 * last selected PLMN and destroying the list of scanned networks.
	 */
	plmn->mcc = plmn->mnc = 0;

	if (!subscr->sim_valid) {
		return 0;
	}

	/* try again later, if not idle */
	if (cs->state == GSM322_CONNECTED_MODE_1
	 || cs->state == GSM322_CONNECTED_MODE_2) {
		LOGP(DPLMN, LOGL_INFO, "Not idle, rejecting.\n");

		return 0;
	}

	/* initiate search at cell selection */
	LOGP(DPLMN, LOGL_INFO, "User re-select, start PLMN search first.\n");

	/* tell MM that we selected a PLMN */
	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_USER_PLMN_SEL);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(ms, nmsg);

	/* triffer PLMN search */
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

	plmn->mcc = plmn->mnc = 0;

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

	/* flush list of PLMNs */
	gsm_subscr_del_forbidden_plmn(&ms->subscr, 0, 0);

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

	new_m_state(plmn, GSM322_M2_ON_PLMN);

	return 0;
}

/* previously selected PLMN becomes available again */
static int gsm322_m_plmn_avail(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;

	new_m_state(plmn, GSM322_M1_TRYING_RPLMN);

	LOGP(DPLMN, LOGL_INFO, "Last selected PLMN becomes available again.\n");

	/* indicate New PLMN */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);
	if (!nmsg)
		return -ENOMEM;
	gsm322_cs_sendmsg(ms, nmsg);

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

	LOGP(DPLMN, LOGL_INFO, "User selects PLMN. (mcc=%s mnc=%s  "
		"%s, %s)\n", gsm_print_mcc(plmn->mcc), gsm_print_mnc(plmn->mnc),
		gsm_get_mcc(plmn->mcc), gsm_get_mnc(plmn->mcc, plmn->mnc));

	/* if selected PLMN is in list of forbidden PLMNs */
	gsm_subscr_del_forbidden_plmn(subscr, plmn->mcc, plmn->mnc);

	new_m_state(plmn, GSM322_M4_TRYING_PLMN);

	/* tell MM that we selected a PLMN */
	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_USER_PLMN_SEL);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(ms, nmsg);

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
static int gsm322_cs_select(struct osmocom_ms *ms, int index, uint16_t mcc,
	uint16_t mnc, int any)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm_settings *set = &ms->settings;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_sysinfo *s;
	int start, end, i, found = -1, power = 0;
	uint8_t flags, mask;
	uint16_t acc_class;
	int16_t c1;
	enum gsm_band band;
	int class;

	/* set our access class depending on the cell selection type */
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

	/* loop through all scanned frequencies and select cell.
	 * if an index is given (arfci), we just check this cell only */
	if (index >= 0) {
		start = end = index;
	} else {
		start = 0; end = 1023+299;
	}
	for (i = start; i <= end; i++) {
		cs->list[i].flags &= ~GSM322_CS_FLAG_TEMP_AA;
		s = cs->list[i].sysinfo;

		/* channel has no informations for us */
		if (!s || (cs->list[i].flags & mask) != flags) {
			continue;
		}

		/* check C1 criteria not fullfilled */
		// TODO: class 3 DCS mobile
		band = gsm_arfcn2band(index2arfcn(i));
		class = class_of_band(ms, band);
		c1 = calculate_c1(DCS, rxlev2dbm(cs->list[i].rxlev),
			s->rxlev_acc_min_db,
			ms_pwr_dbm(band, s->ms_txpwr_max_cch),
			ms_class_gmsk_dbm(band, class));
		if (!set->stick && c1 < 0) {
			LOGP(DCS, LOGL_INFO, "Skip ARFCN %s: C1 criterion "
				"not met. (C1 = %d)\n",
				gsm_print_arfcn(index2arfcn(i)), c1);
			continue;
		}

		/* if cell is barred and we don't override */
		if (!subscr->acc_barr
		 && (cs->list[i].flags & GSM322_CS_FLAG_BARRED)) {
			LOGP(DCS, LOGL_INFO, "Skip ARFCN %s: Cell is "
				"barred.\n", gsm_print_arfcn(index2arfcn(i)));
			continue;
		}

		/* if we have no access to the cell and we don't override */
		if (!subscr->acc_barr
		 && !(acc_class & (s->class_barr ^ 0xffff))) {
			LOGP(DCS, LOGL_INFO, "Skip ARFCN %s: Class is "
				"barred for our access. (access=%04x "
				"barred=%04x)\n",
				gsm_print_arfcn(index2arfcn(i)),
				acc_class, s->class_barr);
			continue;
		}

		/* store temporary available and allowable flag */
		cs->list[i].flags |= GSM322_CS_FLAG_TEMP_AA;

		/* if cell is in list of forbidden LAs */
		if ((cs->list[i].flags & GSM322_CS_FLAG_FORBIDD)) {
			if (!any) {
				LOGP(DCS, LOGL_INFO, "Skip ARFCN %s: Cell is "
					"in list of forbidden LAs. (mcc=%s "
					"mnc=%s lai=%04x)\n",
					gsm_print_arfcn(index2arfcn(i)),
					gsm_print_mcc(s->mcc),
					gsm_print_mnc(s->mnc), s->lac);
				continue;
			}
			LOGP(DCS, LOGL_INFO, "Accept ARFCN %s: Cell is in "
				"list of forbidden LAs, but we search for any "
				"cell. (mcc=%s mnc=%s lai=%04x)\n",
				gsm_print_arfcn(index2arfcn(i)),
				gsm_print_mcc(s->mcc),
				gsm_print_mnc(s->mnc), s->lac);
			cs->list[i].flags &= ~GSM322_CS_FLAG_TEMP_AA;
		}

		/* if cell is in list of forbidden PLMNs */
		if (gsm_subscr_is_forbidden_plmn(subscr, s->mcc, s->mnc)) {
			if (!any) {
				LOGP(DCS, LOGL_INFO, "Skip ARFCN %s: Cell is "
					"in list of forbidden PLMNs. (mcc=%s "
					"mnc=%s)\n",
					gsm_print_arfcn(index2arfcn(i)),
					gsm_print_mcc(s->mcc),
					gsm_print_mnc(s->mnc));
				continue;
			}
			LOGP(DCS, LOGL_INFO, "Accept ARFCN %s: Cell is in list "
				"of forbidden PLMNs, but we search for any "
				"cell. (mcc=%s mnc=%s)\n",
				gsm_print_arfcn(index2arfcn(i)),
				gsm_print_mcc(s->mcc), gsm_print_mnc(s->mnc));
			cs->list[i].flags &= ~GSM322_CS_FLAG_TEMP_AA;
		}

		/* if we search a specific PLMN, but it does not match */
		if (!any && mcc && (mcc != s->mcc
				|| mnc != s->mnc)) {
			LOGP(DCS, LOGL_INFO, "Skip ARFCN %s: PLMN of cell "
				"does not match target PLMN. (mcc=%s "
				"mnc=%s)\n", gsm_print_arfcn(index2arfcn(i)),
				gsm_print_mcc(s->mcc),
				gsm_print_mnc(s->mnc));
			continue;
		}

		LOGP(DCS, LOGL_INFO, "Cell ARFCN %s: Cell found, (rxlev=%s "
			"mcc=%s mnc=%s lac=%04x  %s, %s)\n",
				gsm_print_arfcn(index2arfcn(i)),
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
		LOGP(DCS, LOGL_INFO, "Cell ARFCN %s selected.\n",
			gsm_print_arfcn(index2arfcn(found)));

	return found;
}

/* re-select a suitable and allowable cell */
static int gsm322_cs_reselect(struct osmocom_ms *ms, uint16_t mcc,
	uint16_t mnc, int any)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_sysinfo *s = cs->si;
	int i = cs->arfci;
	uint16_t acc_class;

	/* set our access class depending on the cell selection type */
	if (any) {
		acc_class = subscr->acc_class | 0x0400; /* add emergency */
		LOGP(DCS, LOGL_DEBUG, "Select using access class with "
			"Emergency class.\n");
	} else {
		acc_class = subscr->acc_class;
		LOGP(DCS, LOGL_DEBUG, "Select using access class \n");
	}

	/* if cell is barred and we don't override */
	if (!subscr->acc_barr
	 && (cs->list[i].flags & GSM322_CS_FLAG_BARRED)) {
		LOGP(DCS, LOGL_INFO, "Skip ARFCN %s: Cell is barred.\n",
			gsm_print_arfcn(index2arfcn(i)));
		return -1;
	}

	/* if cell is in list of forbidden LAs */
	if (!any && (cs->list[i].flags & GSM322_CS_FLAG_FORBIDD)) {
		LOGP(DCS, LOGL_INFO, "Skip ARFCN %s: Cell is in list of "
			"forbidden LAs. (mcc=%s mnc=%s lai=%04x)\n",
			gsm_print_arfcn(index2arfcn(i)), gsm_print_mcc(s->mcc),
			gsm_print_mnc(s->mnc), s->lac);
		return -1;
	}

	/* if cell is in list of forbidden PLMNs */
	if (!any && gsm_subscr_is_forbidden_plmn(subscr, s->mcc, s->mnc)) {
		LOGP(DCS, LOGL_INFO, "Skip ARFCN %s: Cell is in "
			"list of forbidden PLMNs. (mcc=%s mnc=%s)\n",
			gsm_print_arfcn(index2arfcn(i)),
			gsm_print_mcc(s->mcc), gsm_print_mnc(s->mnc));
		return -1;
	}

	/* if we have no access to the cell and we don't override */
	if (!subscr->acc_barr
	 && !(acc_class & (s->class_barr ^ 0xffff))) {
		LOGP(DCS, LOGL_INFO, "Skip ARFCN %s: Class is barred for our "
			"access. (access=%04x barred=%04x)\n",
			gsm_print_arfcn(index2arfcn(i)), acc_class,
			s->class_barr);
		return -1;
	}

	/* if we search a specific PLMN, but it does not match */
	if (!any && mcc && (mcc != s->mcc
			|| mnc != s->mnc)) {
		LOGP(DCS, LOGL_INFO, "Skip ARFCN %s: PLMN of cell "
			"does not match target PLMN. (mcc=%s mnc=%s)\n",
			gsm_print_arfcn(index2arfcn(i)), gsm_print_mcc(s->mcc),
			gsm_print_mnc(s->mnc));
		return -1;
	}

	LOGP(DCS, LOGL_INFO, "Cell ARFCN %s: Neighbour cell accepted, "
		"(rxlev=%s mcc=%s mnc=%s lac=%04x  %s, %s)\n",
		gsm_print_arfcn(index2arfcn(i)),
		gsm_print_rxlev(cs->list[i].rxlev),
		gsm_print_mcc(s->mcc), gsm_print_mnc(s->mnc), s->lac,
		gsm_get_mcc(s->mcc), gsm_get_mnc(s->mcc, s->mnc));

	return i;
}

/* this processes the end of frequency scanning or cell searches */
static int gsm322_search_end(struct osmocom_ms *ms)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;
	struct gsm322_msg *ngm;
	int msg_type = -1; /* no message to be sent */
	int tune_back = 0, mcc = 0, mnc = 0;
	int found;

	switch (cs->state) {
	case GSM322_ANY_SEARCH:
		/* special case for 'any cell' search */
		LOGP(DCS, LOGL_INFO, "Any cell search finished.\n");

		/* create AA flag */
		found = gsm322_cs_select(ms, -1, 0, 0, 0);

		/* if no cell is found, or if we don't wait for any available
		 * and allowable PLMN to appear, we just continue to camp */
		if (ms->settings.plmn_mode != PLMN_MODE_AUTO
		 || plmn->state != GSM322_A4_WAIT_FOR_PLMN
		 || found < 0) {
			tune_back = 1;
			gsm322_c_camp_any_cell(ms, NULL);
			break;
		}

		/* indicate available PLMN, include selected PLMN, if found */
		msg_type = GSM322_EVENT_PLMN_AVAIL;
		if (gsm322_is_plmn_avail_and_allow(cs, plmn->mcc, plmn->mnc)) {
			/* set what PLMN becomes available */
			mcc = plmn->mcc;
			mnc = plmn->mnc;
		}

		new_c_state(cs, GSM322_C0_NULL);

		break;

	case GSM322_PLMN_SEARCH:
		/* special case for PLMN search */
		msg_type = GSM322_EVENT_PLMN_SEARCH_END;
		LOGP(DCS, LOGL_INFO, "PLMN search finished.\n");

		/* create AA flag */
		gsm322_cs_select(ms, -1, 0, 0, 0);

		new_c_state(cs, GSM322_C0_NULL);

		break;

	case GSM322_HPLMN_SEARCH:
		/* special case for HPLMN search */
		msg_type = GSM322_EVENT_NO_CELL_FOUND;
		LOGP(DCS, LOGL_INFO, "HPLMN search finished, no cell.\n");

		new_c_state(cs, GSM322_C3_CAMPED_NORMALLY);

		tune_back = 1;

		break;

	default:
		/* we start normal cell selection if this fails */
		if (cs->state == GSM322_C2_STORED_CELL_SEL
		 || cs->state == GSM322_C5_CHOOSE_CELL) {
			/* tell CS to start over */
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_NO_CELL_FOUND);
			if (!nmsg)
				return -ENOMEM;
			gsm322_c_event(ms, nmsg);
			msgb_free(nmsg);

			break;
		}

		/* on other cell selection, indicate "no cell found" */
		/* NOTE: PLMN search process handles it.
		 * If not handled there, CS process gets indicated.
		 * If we would continue to process CS, then we might get
		 * our list of scanned cells disturbed.
		 */
		LOGP(DCS, LOGL_INFO, "Cell search finished without result.\n");
		msg_type = GSM322_EVENT_NO_CELL_FOUND;

		/* stay in null-state until any cell selectio is triggered or
		 * new plmn is indicated.
		 */
		new_c_state(cs, GSM322_C0_NULL);
	}

	if (msg_type > -1) {
		/* send result to PLMN process, to trigger next CS event */
		nmsg = gsm322_msgb_alloc(msg_type);
		if (!nmsg)
			return -ENOMEM;
		ngm = (struct gsm322_msg *) nmsg->data;
		ngm->mcc = mcc;
		ngm->mnc = mcc;
		gsm322_plmn_sendmsg(ms, nmsg);
	}

	if (cs->selected && tune_back) {
		/* tuning back */
		cs->arfcn = cs->sel_arfcn;
		cs->arfci = arfcn2index(cs->arfcn);
		if (!cs->list[cs->arfci].sysinfo)
			cs->list[cs->arfci].sysinfo = talloc_zero(l23_ctx,
							struct gsm48_sysinfo);
		if (!cs->list[cs->arfci].sysinfo)
			exit(-ENOMEM);
		cs->list[cs->arfci].flags |= GSM322_CS_FLAG_SYSINFO;
		memcpy(cs->list[cs->arfci].sysinfo, &cs->sel_si,
			sizeof(struct gsm48_sysinfo));
		cs->si = cs->list[cs->arfci].sysinfo;
		cs->sel_mcc = cs->si->mcc;
		cs->sel_mnc = cs->si->mnc;
		cs->sel_lac = cs->si->lac;
		cs->sel_id = cs->si->cell_id;
		LOGP(DCS, LOGL_INFO, "Tuning back to frequency %s after full "
			"search.\n", gsm_print_arfcn(cs->arfcn));
		cs->sync_retries = SYNC_RETRIES;
		gsm322_sync_to_cell(cs, NULL, 0);
	}

	return 0;
}


/* tune to first/next unscanned frequency and search for PLMN */
static int gsm322_cs_scan(struct osmocom_ms *ms)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int i;
	int j, band = 0;
	uint8_t mask, flags;
	uint32_t weight = 0, test = cs->scan_state;

	/* search for strongest unscanned cell */
	mask = GSM322_CS_FLAG_SUPPORT | GSM322_CS_FLAG_POWER
		| GSM322_CS_FLAG_SIGNAL;
	if (cs->state == GSM322_C2_STORED_CELL_SEL
	 || cs->state == GSM322_C5_CHOOSE_CELL)
		mask |= GSM322_CS_FLAG_BA;
	flags = mask; /* all masked flags are requied */
	for (i = 0; i <= 1023+299; i++) {
		j = 0; /* make gcc happy */
		if (!ms->settings.skip_max_per_band) {
			/* skip if band has enough freqs. scanned (3.2.1) */
			for (j = 0; gsm_sup_smax[j].max; j++) {
				if (gsm_sup_smax[j].end >
						gsm_sup_smax[j].start) {
					if (gsm_sup_smax[j].start <= i
					 && gsm_sup_smax[j].end >= i)
						break;
				} else {
					if (gsm_sup_smax[j].start <= i
					 && 1023 >= i)
						break;
					if (0 <= i
					 && gsm_sup_smax[j].end >= i)
						break;
				}
			}
			if (gsm_sup_smax[j].max) {
				if (gsm_sup_smax[j].temp == gsm_sup_smax[j].max)
					continue;
			}
		}

		/* search for unscanned frequency */
		if ((cs->list[i].flags & mask) == flags) {
			/* weight depends on the power level
			 * if it is the same, it depends on arfcn
			 */
			test = cs->list[i].rxlev + 1;
			test = (test << 16) | i;
			if (test >= cs->scan_state)
				continue;
			if (test > weight) {
				weight = test;
				band = j;
			}

		}
	}
	cs->scan_state = weight;

	/* if all frequencies have been searched */
	if (!weight) {
		gsm322_dump_cs_list(cs, GSM322_CS_FLAG_SYSINFO, print_dcs,
			NULL);

		/* selection process done, process (negative) result */
		return gsm322_search_end(ms);
	}

	/* NOTE: We might already have system information from previous
	 * scan. But we need recent informations, so we scan again!
	 */

	/* Tune to frequency for a while, to receive broadcasts. */
	cs->arfci = weight & 0xffff;
	cs->arfcn = index2arfcn(cs->arfci);
	LOGP(DCS, LOGL_DEBUG, "Scanning frequency %s (rxlev %s).\n",
		gsm_print_arfcn(cs->arfcn),
		gsm_print_rxlev(cs->list[cs->arfci].rxlev));

	/* Allocate/clean system information. */
	cs->list[cs->arfci].flags &= ~GSM322_CS_FLAG_SYSINFO;
	if (cs->list[cs->arfci].sysinfo)
		memset(cs->list[cs->arfci].sysinfo, 0,
			sizeof(struct gsm48_sysinfo));
	else
		cs->list[cs->arfci].sysinfo = talloc_zero(l23_ctx,
						struct gsm48_sysinfo);
	if (!cs->list[cs->arfci].sysinfo)
		exit(-ENOMEM);
	cs->si = cs->list[cs->arfci].sysinfo;
	cs->sync_retries = 0;
	gsm322_sync_to_cell(cs, NULL, 0);

	/* increase scan counter for each maximum scan range */
	if (!ms->settings.skip_max_per_band && gsm_sup_smax[band].max) {
		LOGP(DCS, LOGL_DEBUG, "%d frequencies left in band %d..%d\n",
			gsm_sup_smax[band].max - gsm_sup_smax[band].temp,
			gsm_sup_smax[band].start, gsm_sup_smax[band].end);
		gsm_sup_smax[band].temp++;
	}

	return 0;
}

/* check if cell is now suitable and allowable */
static int gsm322_cs_store(struct osmocom_ms *ms)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = cs->si;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct msgb *nmsg;
	struct gsm322_msg *ngm;
	int found, any = 0;

	if (cs->state != GSM322_C2_STORED_CELL_SEL
	 && cs->state != GSM322_C1_NORMAL_CELL_SEL
	 && cs->state != GSM322_C6_ANY_CELL_SEL
	 && cs->state != GSM322_C4_NORMAL_CELL_RESEL
	 && cs->state != GSM322_C8_ANY_CELL_RESEL
	 && cs->state != GSM322_C5_CHOOSE_CELL
	 && cs->state != GSM322_C9_CHOOSE_ANY_CELL
	 && cs->state != GSM322_ANY_SEARCH
	 && cs->state != GSM322_PLMN_SEARCH
	 && cs->state != GSM322_HPLMN_SEARCH) {
		LOGP(DCS, LOGL_FATAL, "This must only happen during cell "
			"(re-)selection, please fix!\n");
		return -EINVAL;
	}

	/* store sysinfo */
	cs->list[cs->arfci].flags |= GSM322_CS_FLAG_SYSINFO;
	if (s->cell_barr && !(s->sp && s->sp_cbq))
		cs->list[cs->arfci].flags |= GSM322_CS_FLAG_BARRED;
	else
		cs->list[cs->arfci].flags &= ~GSM322_CS_FLAG_BARRED;

	/* store selected network */
	if (s->mcc) {
		if (gsm322_is_forbidden_la(ms, s->mcc, s->mnc, s->lac))
			cs->list[cs->arfci].flags |= GSM322_CS_FLAG_FORBIDD;
		else
			cs->list[cs->arfci].flags &= ~GSM322_CS_FLAG_FORBIDD;
	}

	LOGP(DCS, LOGL_DEBUG, "Scan frequency %s: Cell found. (rxlev %s "
		"mcc %s mnc %s lac %04x)\n", gsm_print_arfcn(cs->arfcn),
		gsm_print_rxlev(cs->list[cs->arfci].rxlev),
		gsm_print_mcc(s->mcc), gsm_print_mnc(s->mnc), s->lac);

	/* selected PLMN (auto) becomes available during "any search" */
	if (ms->settings.plmn_mode == PLMN_MODE_AUTO
	 && (cs->state == GSM322_ANY_SEARCH
	  || cs->state == GSM322_C6_ANY_CELL_SEL
	  || cs->state == GSM322_C8_ANY_CELL_RESEL
	  || cs->state == GSM322_C9_CHOOSE_ANY_CELL)
	 && plmn->state == GSM322_A4_WAIT_FOR_PLMN
	 && s->mcc == plmn->mcc && s->mnc == plmn->mnc) {
		LOGP(DCS, LOGL_INFO, "Candidate network to become available "
			"again\n");
		found = gsm322_cs_select(ms, cs->arfci, s->mcc, s->mnc, 0);
		if (found >= 0) {
			LOGP(DCS, LOGL_INFO, "Selected PLMN in \"A4_WAIT_F"
				"OR_PLMN\" state becomes available.\n");
indicate_plmn_avail:
			/* PLMN becomes available */
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_PLMN_AVAIL);
			if (!nmsg)
				return -ENOMEM;
			/* set what PLMN becomes available */
			ngm = (struct gsm322_msg *) nmsg->data;
			ngm->mcc = plmn->mcc;
			ngm->mnc = plmn->mcc;
			gsm322_plmn_sendmsg(ms, nmsg);

			new_c_state(cs, GSM322_C0_NULL);

			return 0;
		}
	}

	/* selected PLMN (manual) becomes available during "any search" */
	if (ms->settings.plmn_mode == PLMN_MODE_MANUAL
	 && (cs->state == GSM322_ANY_SEARCH
	  || cs->state == GSM322_C6_ANY_CELL_SEL
	  || cs->state == GSM322_C8_ANY_CELL_RESEL
	  || cs->state == GSM322_C9_CHOOSE_ANY_CELL)
	 && plmn->state == GSM322_M3_NOT_ON_PLMN
	 && s->mcc == plmn->mcc && s->mnc == plmn->mnc) {
		LOGP(DCS, LOGL_INFO, "Candidate network to become available "
			"again\n");
		found = gsm322_cs_select(ms, cs->arfci, s->mcc, s->mnc, 0);
		if (found >= 0) {
			LOGP(DCS, LOGL_INFO, "Current selected PLMN in \"M3_N"
				"OT_ON_PLMN\" state becomes available.\n");
			goto indicate_plmn_avail;
		}
	}

	/* special case for PLMN search */
	if (cs->state == GSM322_PLMN_SEARCH
	 || cs->state == GSM322_ANY_SEARCH)
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

	if (cs->state == GSM322_C4_NORMAL_CELL_RESEL
	 || cs->state == GSM322_C8_ANY_CELL_RESEL)
		found = gsm322_cs_reselect(ms, cs->mcc, cs->mnc, any);
	else
		found = gsm322_cs_select(ms, -1, cs->mcc, cs->mnc, any);

	/* if not found */
	if (found < 0) {
		LOGP(DCS, LOGL_INFO, "Cell not suitable and allowable.\n");
		/* tune to next cell */
		if (cs->state == GSM322_C4_NORMAL_CELL_RESEL
		 || cs->state == GSM322_C8_ANY_CELL_RESEL)
			return gsm322_nb_scan(ms);
		else
			return gsm322_cs_scan(ms);
	}

	LOGP(DCS, LOGL_INFO, "Tune to frequency %d.\n", found);
	/* tune */
	cs->arfci = found;
	cs->arfcn = index2arfcn(cs->arfci);
	cs->si = cs->list[cs->arfci].sysinfo;
	cs->sync_retries = SYNC_RETRIES;
	gsm322_sync_to_cell(cs, NULL, 0);

	/* set selected cell */
	cs->selected = 1;
	cs->sel_arfcn = cs->arfcn;
	memcpy(&cs->sel_si, cs->si, sizeof(cs->sel_si));
	cs->sel_mcc = cs->si->mcc;
	cs->sel_mnc = cs->si->mnc;
	cs->sel_lac = cs->si->lac;
	cs->sel_id = cs->si->cell_id;
	if (ms->rrlayer.monitor) {
		vty_notify(ms, "MON: %scell selected ARFCN=%s MCC=%s MNC=%s "
			"LAC=0x%04x cellid=0x%04x (%s %s)\n",
			(any) ? "any " : "", gsm_print_arfcn(cs->sel_arfcn),
			gsm_print_mcc(cs->sel_mcc), gsm_print_mnc(cs->sel_mnc),
			cs->sel_lac, cs->sel_id,
			gsm_get_mcc(cs->sel_mcc),
				gsm_get_mnc(cs->sel_mcc, cs->sel_mnc));
	}

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
	struct gsm48_sysinfo *s;
	struct gsm322_ba_list *ba = NULL;
	int i, refer_pcs;
	uint8_t freq[128+38];

	if (!cs) {
		LOGP(DCS, LOGL_INFO, "No BA, because no cell selected\n");
		return ba;
	}
	s = cs->si;
	if (!s) {
		LOGP(DCS, LOGL_INFO, "No BA, because no sysinfo\n");
		return ba;
	}

	/* collect system information received during dedicated mode */
	if (s->si5 && (!s->nb_ext_ind_si5 || s->si5bis)) {
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
		refer_pcs = gsm_refer_pcs(cs->arfcn, s);
		memset(freq, 0, sizeof(freq));
		for (i = 0; i <= 1023; i++) {
			if ((s->freq[i].mask & (FREQ_TYPE_SERV
				| FREQ_TYPE_NCELL | FREQ_TYPE_REP))) {
				if (refer_pcs && i >= 512 && i <= 810)
					freq[(i-512+1024) >> 3] |= (1 << (i&7));
				else
					freq[i >> 3] |= (1 << (i & 7));
			}
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
	int i, refer_pcs;
	uint8_t freq[128+38];

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
	refer_pcs = gsm_refer_pcs(cs->arfcn, s);
	memset(freq, 0, sizeof(freq));
	freq[(cs->arfci) >> 3] |= (1 << (cs->arfci & 7));
	for (i = 0; i <= 1023; i++) {
		if ((s->freq[i].mask &
		    (FREQ_TYPE_SERV | FREQ_TYPE_NCELL | FREQ_TYPE_REP))) {
			if (refer_pcs && i >= 512 && i <= 810)
				freq[(i-512+1024) >> 3] |= (1 << (i & 7));
			else
				freq[i >> 3] |= (1 << (i & 7));
		}
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

	/* start in case we are camping on neighbour cell */
	if ((cs->state == GSM322_C3_CAMPED_NORMALLY
	  || cs->state == GSM322_C7_CAMPED_ANY_CELL)
	 && (cs->neighbour)) {
		if (s->si3 || s->si4) {
			stop_cs_timer(cs);
			LOGP(DCS, LOGL_INFO, "Relevant sysinfo of neighbour "
				"cell is now received or updated.\n");
			return gsm322_nb_read(cs, 1);
		}
		return 0;
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

	/* update sel_si, if all relevant system informations received */
	if (s->si1 && s->si2 && s->si3
	 && (!s->nb_ext_ind_si2
	  || (s->si2bis && s->nb_ext_ind_si2 && !s->nb_ext_ind_si2bis)
	  || (s->si2bis && s->si2ter && s->nb_ext_ind_si2
		&& s->nb_ext_ind_si2bis))) {
		if (cs->selected) {
			LOGP(DCS, LOGL_INFO, "Sysinfo of selected cell is "
				"now received or updated.\n");
			memcpy(&cs->sel_si, s, sizeof(cs->sel_si));

			/* start in case we are camping on serving cell */
			if (cs->state == GSM322_C3_CAMPED_NORMALLY
			 || cs->state == GSM322_C7_CAMPED_ANY_CELL)
				gsm322_nb_start(ms, 0);
		}
	}

	/* check for barred cell */
	if (gm->sysinfo == GSM48_MT_RR_SYSINFO_1) {
		/* check if cell becomes barred */
		if (!subscr->acc_barr && s->cell_barr
		 && !(cs->list[cs->arfci].sysinfo
		   && cs->list[cs->arfci].sysinfo->sp
		   && cs->list[cs->arfci].sysinfo->sp_cbq)) {
			LOGP(DCS, LOGL_INFO, "Cell becomes barred.\n");
			if (ms->rrlayer.monitor)
				vty_notify(ms, "MON: trigger cell re-selection"
					": cell becomes barred\n");
			trigger_resel:
			/* mark cell as unscanned */
			cs->list[cs->arfci].flags &= ~GSM322_CS_FLAG_SYSINFO;
			if (cs->list[cs->arfci].sysinfo) {
				LOGP(DCS, LOGL_DEBUG, "free sysinfo arfcn=%s\n",
					gsm_print_arfcn(cs->arfcn));
				talloc_free(cs->list[cs->arfci].sysinfo);
				cs->list[cs->arfci].sysinfo = NULL;
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
			if (ms->rrlayer.monitor)
				vty_notify(ms, "MON: trigger cell re-selection"
					": access to cell becomes barred\n");
			goto trigger_resel;
		}
	}

	/* check if MCC, MNC, LAC, cell ID changes */
	if (cs->sel_mcc != s->mcc || cs->sel_mnc != s->mnc
	 || cs->sel_lac != s->lac) {
		LOGP(DCS, LOGL_NOTICE, "Cell changes location area. "
			"This is not good!\n");
		if (ms->rrlayer.monitor)
			vty_notify(ms, "MON: trigger cell re-selection: "
				"cell changes LAI\n");
		goto trigger_resel;
	}
	if (cs->sel_id != s->cell_id) {
		LOGP(DCS, LOGL_NOTICE, "Cell changes cell ID. "
			"This is not good!\n");
		if (ms->rrlayer.monitor)
			vty_notify(ms, "MON: trigger cell re-selection: "
				"cell changes cell ID\n");
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
	 && (!s->nb_ext_ind_si2 || s->si2bis)
	 && (!s->si2ter_ind || s->si2ter))
		gsm322_store_ba_list(cs, s);

	/* all relevant system informations received */
	if (s->si1 && s->si2 && s->si3
	 && (!s->nb_ext_ind_si2 || s->si2bis)
	 && (!s->si2ter_ind || s->si2ter)) {
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

	if (cs->neighbour) {
		LOGP(DCS, LOGL_INFO, "Neighbour cell read failed.\n");
		gsm322_nb_read(cs, 0);
		return;
	}

	/* if we have no lock, we retry */
	if (cs->ccch_state != GSM322_CCCH_ST_SYNC)
		LOGP(DCS, LOGL_INFO, "Cell selection failed, sync timeout.\n");
	else
		LOGP(DCS, LOGL_INFO, "Cell selection failed, read timeout.\n");

	/* remove system information */
	cs->list[cs->arfci].flags &= ~GSM322_CS_FLAG_SYSINFO;
	if (cs->list[cs->arfci].sysinfo) {
		LOGP(DCS, LOGL_DEBUG, "free sysinfo arfcn=%s\n",
			gsm_print_arfcn(cs->arfcn));
		talloc_free(cs->list[cs->arfci].sysinfo);
		cs->list[cs->arfci].sysinfo = NULL;
	}

	/* tune to next cell */
	if (cs->state == GSM322_C4_NORMAL_CELL_RESEL
	 || cs->state == GSM322_C8_ANY_CELL_RESEL)
		gsm322_nb_scan(ms);
	else
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
	char s_text[ARFCN_TEXT_LEN], e_text[ARFCN_TEXT_LEN];
	uint8_t mask, flags;

	again:

	mask = GSM322_CS_FLAG_SUPPORT | GSM322_CS_FLAG_POWER;
	flags = GSM322_CS_FLAG_SUPPORT;

	/* in case of sticking to a cell, we only select it */
	if (set->stick) {
		LOGP(DCS, LOGL_DEBUG, "Scanning power for sticked cell.\n");
		i = arfcn2index(set->stick_arfcn);
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
		for (i = 0; i <= 1023+299; i++) {
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

		/* check if no signal is found */
		for (i = 0; i <= 1023+299; i++) {
			if ((cs->list[i].flags & GSM322_CS_FLAG_SIGNAL))
				found++;
		}
		if (!found) {
			LOGP(DCS, LOGL_INFO, "Found no frequency.\n");
			/* on normal cell selection, start over */
			if (cs->state == GSM322_C1_NORMAL_CELL_SEL) {
				for (i = 0; i <= 1023+299; i++) {
					/* clear flag that this was scanned */
					cs->list[i].flags &=
						~(GSM322_CS_FLAG_POWER
						| GSM322_CS_FLAG_SIGNAL
						| GSM322_CS_FLAG_SYSINFO);
				}
				goto again;
			}

			/* freq. scan process done, process (negative) result */
			return gsm322_search_end(ms);
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
		for (i = s + 1; i <= 1023+299; i++) {
			if (i == 1024)
				break;
			if ((cs->list[i].flags & mask) == flags)
				e = i;
			else
				break;
		}
	}

	strncpy(s_text, gsm_print_arfcn(index2arfcn(s)), ARFCN_TEXT_LEN);
	strncpy(e_text, gsm_print_arfcn(index2arfcn(e)), ARFCN_TEXT_LEN);
	LOGP(DCS, LOGL_DEBUG, "Scanning frequencies. (%s..%s)\n",
		s_text,
		e_text);

	/* start scan on radio interface */
	if (!cs->powerscan) {
		l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
		cs->powerscan = 1;
	}
	cs->sync_pending = 0;
	return l1ctl_tx_pm_req_range(ms, index2arfcn(s), index2arfcn(e));
}

int gsm322_l1_signal(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms;
	struct gsm322_cellsel *cs;
	struct osmobb_meas_res *mr;
	struct osmobb_fbsb_res *fr;
	struct osmobb_neigh_pm_ind *ni;
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
		i = arfcn2index(mr->band_arfcn);
		rxlev = mr->rx_lev;
		if ((cs->list[i].flags & GSM322_CS_FLAG_POWER)) {
			LOGP(DCS, LOGL_ERROR, "Getting PM for ARFCN %s "
				"twice. Overwriting the first! Please fix "
				"prim_pm.c\n", gsm_print_arfcn(index2arfcn(i)));
		}
		cs->list[i].rxlev = rxlev;
		cs->list[i].flags |= GSM322_CS_FLAG_POWER;
		cs->list[i].flags &= ~GSM322_CS_FLAG_SIGNAL;
		/* if minimum level is reached or if we stick to a cell */
		if (rxlev2dbm(rxlev) >= ms->settings.min_rxlev_dbm
		 || ms->settings.stick) {
			cs->list[i].flags |= GSM322_CS_FLAG_SIGNAL;
			LOGP(DCS, LOGL_INFO, "Found signal (ARFCN %s "
				"rxlev %s (%d))\n",
				gsm_print_arfcn(index2arfcn(i)),
				gsm_print_rxlev(rxlev), rxlev);
		} else
		/* no signal found, free sysinfo, if allocated */
		if (cs->list[i].sysinfo) {
			cs->list[i].flags &= ~GSM322_CS_FLAG_SYSINFO;
			LOGP(DCS, LOGL_DEBUG, "free sysinfo ARFCN=%s\n",
				gsm_print_arfcn(index2arfcn(i)));
			talloc_free(cs->list[i].sysinfo);
			cs->list[i].sysinfo = NULL;
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
		if (cs->powerscan)
			return -EINVAL;
		cs->sync_pending = 0;
		if (cs->arfcn != fr->band_arfcn) {
			LOGP(DCS, LOGL_NOTICE, "Channel synched on "
				"wrong ARFCN=%d, syncing on right ARFCN again"
				"...\n", fr->band_arfcn);
			cs->sync_retries = SYNC_RETRIES;
			gsm322_sync_to_cell(cs, cs->neighbour, 0);
			break;
		}
		if (cs->ccch_state == GSM322_CCCH_ST_INIT) {
			LOGP(DCS, LOGL_INFO, "Channel synched. (ARFCN=%s, "
				"snr=%u, BSIC=%u)\n",
				gsm_print_arfcn(cs->arfcn), fr->snr, fr->bsic);
			cs->ccch_state = GSM322_CCCH_ST_SYNC;
			if (cs->si)
				cs->si->bsic = fr->bsic;

			/* set timer for reading BCCH */
			if (cs->state == GSM322_C2_STORED_CELL_SEL
			 || cs->state == GSM322_C1_NORMAL_CELL_SEL
			 || cs->state == GSM322_C6_ANY_CELL_SEL
			 || cs->state == GSM322_C4_NORMAL_CELL_RESEL
			 || cs->state == GSM322_C8_ANY_CELL_RESEL
			 || cs->state == GSM322_C5_CHOOSE_CELL
			 || cs->state == GSM322_C9_CHOOSE_ANY_CELL
			 || cs->state == GSM322_ANY_SEARCH
			 || cs->state == GSM322_PLMN_SEARCH
			 || cs->state == GSM322_HPLMN_SEARCH)
				start_cs_timer(cs, ms->support.scan_to, 0);
					// TODO: timer depends on BCCH config

			/* set downlink signalling failure criterion */
			ms->meas.ds_fail = ms->meas.dsc = ms->settings.dsc_max;
			LOGP(DRR, LOGL_INFO, "using DSC of %d\n", ms->meas.dsc);

			/* start in case we are camping on serving/neighbour
			 * cell */
			if (cs->state == GSM322_C3_CAMPED_NORMALLY
			 || cs->state == GSM322_C7_CAMPED_ANY_CELL) {
				if (cs->neighbour)
					gsm322_nb_synced(cs, 1);
				else
					gsm322_nb_start(ms, 1);
			}
		}
		break;
	case S_L1CTL_FBSB_ERR:
		fr = signal_data;
		ms = fr->ms;
		cs = &ms->cellsel;
		if (cs->powerscan)
			return -EINVAL;
		cs->sync_pending = 0;
		/* retry */
		if (cs->sync_retries) {
			LOGP(DCS, LOGL_INFO, "Channel sync error, try again\n");
			cs->sync_retries--;
			gsm322_sync_to_cell(cs, cs->neighbour, 0);
			break;
		}
		if (cs->arfcn != fr->band_arfcn) {
			LOGP(DCS, LOGL_NOTICE, "Channel synched failed on "
				"wrong ARFCN=%d, syncing on right ARFCN again"
				"...\n", fr->band_arfcn);
			cs->sync_retries = SYNC_RETRIES;
			gsm322_sync_to_cell(cs, cs->neighbour, 0);
			break;
		}
		LOGP(DCS, LOGL_INFO, "Channel sync error.\n");
		/* no sync, free sysinfo, if allocated */
		if (cs->list[cs->arfci].sysinfo) {
			cs->list[cs->arfci].flags &= ~GSM322_CS_FLAG_SYSINFO;
			LOGP(DCS, LOGL_DEBUG, "free sysinfo ARFCN=%s\n",
				gsm_print_arfcn(index2arfcn(cs->arfci)));
			talloc_free(cs->list[cs->arfci].sysinfo);
			cs->list[cs->arfci].sysinfo = NULL;

		}
		if (cs->selected && cs->sel_arfcn == cs->arfcn) {
			LOGP(DCS, LOGL_INFO, "Unselect cell due to sync "
				"error!\n");
			/* unset selected cell */
			gsm322_unselect_cell(cs);
		}
		stop_cs_timer(cs);

		/* start in case we are camping on neighbour * cell */
		if (cs->state == GSM322_C3_CAMPED_NORMALLY
		 || cs->state == GSM322_C7_CAMPED_ANY_CELL) {
			if (cs->neighbour) {
				gsm322_nb_synced(cs, 0);
				break;
			}
		}

		gsm322_cs_loss(cs);
		break;
	case S_L1CTL_LOSS_IND:
		ms = signal_data;
		cs = &ms->cellsel;
		LOGP(DCS, LOGL_INFO, "Loss of CCCH.\n");
		if (cs->selected && cs->sel_arfcn == cs->arfcn) {
			/* do not unselect cell */
			LOGP(DCS, LOGL_INFO, "Keep cell selected after loss, "
				"so we can use the Neighbour cell information "
				"for cell re-selection.\n");
		}
		stop_cs_timer(cs);
		gsm322_cs_loss(cs);
		break;
	case S_L1CTL_RESET:
		ms = signal_data;
		if (ms->mmlayer.power_off_idle) {
			mobile_exit(ms, 1);
			return 0;
		}
		break;
	case S_L1CTL_NEIGH_PM_IND:
		ni = signal_data;
		ms = ni->ms;
#ifdef COMMING_LATE_R
		/* in dedicated mode */
		if (ms->rrlayer.dm_est)
			gsm48_rr_meas_ind(ms, ni->band_arfcn, ni->rx_lev);
		else
#endif
		/* in camping mode */
		if ((ms->cellsel.state == GSM322_C3_CAMPED_NORMALLY
		  || ms->cellsel.state == GSM322_C7_CAMPED_ANY_CELL)
		 && !ms->cellsel.neighbour)
			gsm322_nb_meas_ind(ms, ni->band_arfcn, ni->rx_lev);
		break;
	}

	return 0;
}

static void gsm322_cs_loss(void *arg)
{
	struct gsm322_cellsel *cs = arg;
	struct osmocom_ms *ms = cs->ms;

	if ((cs->state == GSM322_C3_CAMPED_NORMALLY
	  || cs->state == GSM322_C7_CAMPED_ANY_CELL)
	 && !cs->neighbour) {
			struct msgb *nmsg;

			LOGP(DCS, LOGL_INFO, "Loss of CCCH, Trigger "
				"re-selection.\n");
			if (ms->rrlayer.monitor)
				vty_notify(ms, "MON: trigger cell "
					"re-selection: loss of signal\n");

			nmsg = gsm322_msgb_alloc(GSM322_EVENT_CELL_RESEL);
			if (!nmsg)
				return;
			gsm322_c_event(ms, nmsg);
			msgb_free(nmsg);

			return;
	} else
	if (cs->state == GSM322_CONNECTED_MODE_1
	 || cs->state == GSM322_CONNECTED_MODE_2) {
			LOGP(DCS, LOGL_INFO, "Loss of SACCH, Trigger RR "
				"abort.\n");

			/* keep cell info for re-selection */

			gsm48_rr_los(ms);
			/* be shure that nothing else is done after here
			 * because the function call above may cause
			 * to return from idle state and trigger cell re-sel.
			 */

			return;
	}

	gsm322_cs_timeout(cs);

	return;
}

/*
 * handler for cell selection process
 */

/* start any cell search */
static int gsm322_c_any_search(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int i;

	new_c_state(cs, GSM322_ANY_SEARCH);

	/* mark all frequencies as scanned */
	for (i = 0; i <= 1023+299; i++) {
		cs->list[i].flags &= ~(GSM322_CS_FLAG_POWER
					| GSM322_CS_FLAG_SIGNAL
					| GSM322_CS_FLAG_SYSINFO);
	}

	/* start power scan */
	return gsm322_cs_powerscan(ms);
}

/* start PLMN search */
static int gsm322_c_plmn_search(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int i;

	new_c_state(cs, GSM322_PLMN_SEARCH);

	/* mark all frequencies as scanned */
	for (i = 0; i <= 1023+299; i++) {
		cs->list[i].flags &= ~(GSM322_CS_FLAG_POWER
					| GSM322_CS_FLAG_SIGNAL
					| GSM322_CS_FLAG_SYSINFO);
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
	int i, sel_i = arfcn2index(cs->sel_arfcn);

	new_c_state(cs, GSM322_HPLMN_SEARCH);

	/* mark all frequencies except our own BA as unscanned */
	for (i = 0; i <= 1023+299; i++) {
		if (i != sel_i
		 && (cs->list[i].flags & GSM322_CS_FLAG_SYSINFO)
		 && !(cs->list[i].flags & GSM322_CS_FLAG_BA)) {
			cs->list[i].flags &= ~(GSM322_CS_FLAG_POWER
						| GSM322_CS_FLAG_SIGNAL
						| GSM322_CS_FLAG_SYSINFO);
		}
	}

	/* start power scan */
	return gsm322_cs_powerscan(ms);
}

/* start stored cell selection */
static int gsm322_c_stored_cell_sel(struct osmocom_ms *ms,
	struct gsm322_ba_list *ba)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int i;

	/* we weed to rescan */
	for (i = 0; i <= 1023+299; i++) {
		cs->list[i].flags &= ~(GSM322_CS_FLAG_POWER
					| GSM322_CS_FLAG_SIGNAL
					| GSM322_CS_FLAG_SYSINFO);
	}

	new_c_state(cs, GSM322_C2_STORED_CELL_SEL);

	/* flag all frequencies that are in current band allocation */
	for (i = 0; i <= 1023+299; i++) {
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

	/* except for stored cell selection state, we weed to rescan */
	if (cs->state != GSM322_C2_STORED_CELL_SEL) {
		for (i = 0; i <= 1023+299; i++) {
			cs->list[i].flags &= ~(GSM322_CS_FLAG_POWER
						| GSM322_CS_FLAG_SIGNAL
						| GSM322_CS_FLAG_SYSINFO);
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
	struct gsm322_msg *gm = (struct gsm322_msg *) msg->data;
	int msg_type = gm->msg_type;

	/* in case we already tried any cell (re-)selection, power scan again */
	if (cs->state == GSM322_C0_NULL
	 || cs->state == GSM322_C6_ANY_CELL_SEL
	 || cs->state == GSM322_C8_ANY_CELL_RESEL) {
		int i;

		for (i = 0; i <= 1023+299; i++) {
			cs->list[i].flags &= ~(GSM322_CS_FLAG_POWER
						| GSM322_CS_FLAG_SIGNAL
						| GSM322_CS_FLAG_SYSINFO);
		}

		/* indicate to MM that we lost coverage.
		 * this is the only case where we really have no coverage.
		 * we tell MM, so it will enter the "No Cell Avaiable" state. */
		if (msg_type == GSM322_EVENT_NO_CELL_FOUND) {
			struct msgb *nmsg;

			/* tell that we have no cell found
			 * (not any cell at all) */
			nmsg = gsm48_mmevent_msgb_alloc(
						GSM48_MM_EVENT_NO_CELL_FOUND);
			if (!nmsg)
				return -ENOMEM;
			gsm48_mmevent_msg(ms, nmsg);
		}
	}

	new_c_state(cs, GSM322_C6_ANY_CELL_SEL);

	cs->mcc = cs->mnc = 0;

	/* unset selected cell */
	gsm322_unselect_cell(cs);

	/* start power scan */
	return gsm322_cs_powerscan(ms);
}

static void gsm322_any_timeout(void *arg)
{
	struct gsm322_cellsel *cs = arg;
	struct osmocom_ms *ms = cs->ms;

	/* the timer may still run when not camping, so we ignore it.
	 * it will be restarted whenever the 'camped on any cell' state
	 * is reached. */
	if (cs->state != GSM322_C7_CAMPED_ANY_CELL)
		return;

	/* in case the time has been started before SIM was removed */
	if (!ms->subscr.sim_valid)
		return;

	LOGP(DCS, LOGL_INFO, "'Any cell selection timer' timed out. "
		"Starting special search to find allowed PLMNs.\n");

	gsm322_c_any_search(ms, NULL);
}

/* sim is removed, proceed with any cell selection */
static int gsm322_c_sim_remove(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct llist_head *lh, *lh2;

	/* flush list of forbidden LAs */
	llist_for_each_safe(lh, lh2, &plmn->forbidden_la) {
		llist_del(lh);
		talloc_free(lh);
	}
	return gsm322_c_any_cell_sel(ms, msg);
}

/* start noraml cell re-selection */
static int gsm322_c_normal_cell_resel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *nmsg;

	/* store last camped cell. this is required for next cell
	 * monitoring reselection criterion */
	cs->last_serving_arfcn = cs->sel_arfcn;
	cs->last_serving_valid = 1;

	/* unset selected cell */
	gsm322_unselect_cell(cs);

	/* tell MM that we lost coverage */
	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_LOST_COVERAGE);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(ms, nmsg);

	new_c_state(cs, GSM322_C4_NORMAL_CELL_RESEL);

	/* start scanning neighbour cells for reselection */
	return gsm322_nb_scan(ms);
}

/* start any cell re-selection */
static int gsm322_c_any_cell_resel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *nmsg;

	/* store last camped cell. this is required for next cell
	 * monitoring reselection criterion */
	cs->last_serving_arfcn = cs->sel_arfcn;
	cs->last_serving_valid = 1;

	/* unset selected cell */
	gsm322_unselect_cell(cs);

	/* tell MM that we lost coverage */
	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_LOST_COVERAGE);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(ms, nmsg);

	new_c_state(cs, GSM322_C8_ANY_CELL_RESEL);

	/* start scanning neighbour cells for reselection */
	return gsm322_nb_scan(ms);
}

/* a suitable cell was found, so we camp normally */
static int gsm322_c_camp_normally(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *nmsg;

	LOGP(DSUM, LOGL_INFO, "Camping normally on cell (ARFCN=%s mcc=%s "
		"mnc=%s  %s, %s)\n", gsm_print_arfcn(cs->sel_arfcn),
		gsm_print_mcc(cs->sel_mcc),
		gsm_print_mnc(cs->sel_mnc), gsm_get_mcc(cs->sel_mcc),
		gsm_get_mnc(cs->sel_mcc, cs->sel_mnc));

	/* if we did cell reselection, we have a valid last serving cell */
	if (cs->state != GSM322_C4_NORMAL_CELL_RESEL)
		cs->last_serving_valid = 0;

	/* tell that we have selected a (new) cell */
	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_CELL_SELECTED);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(ms, nmsg);

	new_c_state(cs, GSM322_C3_CAMPED_NORMALLY);

	return 0;
}

/* any cell was found, so we camp on any cell */
static int gsm322_c_camp_any_cell(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *nmsg;

	LOGP(DSUM, LOGL_INFO, "Camping on any cell (ARFCN=%s mcc=%s "
		"mnc=%s  %s, %s)\n", gsm_print_arfcn(cs->sel_arfcn),
		gsm_print_mcc(cs->sel_mcc),
		gsm_print_mnc(cs->sel_mnc), gsm_get_mcc(cs->sel_mcc),
		gsm_get_mnc(cs->sel_mcc, cs->sel_mnc));

	/* (re-)starting 'any cell selection' timer to look for coverage of
	 * allowed PLMNs.
	 * start timer, if not running.
	 * restart timer, if we just entered the 'camped any cell' state */
	if (ms->subscr.sim_valid
	 && (cs->state != GSM322_C8_ANY_CELL_RESEL
	  || !osmo_timer_pending(&cs->any_timer))) {
		struct gsm322_plmn *plmn = &ms->plmn;

		stop_any_timer(cs);
		if (ms->settings.plmn_mode == PLMN_MODE_MANUAL
		 && (!plmn->mcc
		  || gsm_subscr_is_forbidden_plmn(&ms->subscr, plmn->mcc,
							plmn->mnc))) {
			LOGP(DCS, LOGL_INFO, "Not starting 'any search' timer, "
				"because no selected PLMN or forbidden\n");
		} else
			start_any_timer(cs, ms->subscr.any_timeout, 0);
	}

	/* if we did cell reselection, we have a valid last serving cell */
	if (cs->state != GSM322_C8_ANY_CELL_RESEL)
		cs->last_serving_valid = 0;

	/* tell that we have selected a (new) cell.
	 * this cell iss not allowable, so the MM state will enter limited
	 * service */
	if (cs->state != GSM322_C7_CAMPED_ANY_CELL) {
		nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_CELL_SELECTED);
		if (!nmsg)
			return -ENOMEM;
		gsm48_mmevent_msg(ms, nmsg);
	}

	new_c_state(cs, GSM322_C7_CAMPED_ANY_CELL);

	return 0;
}

/* create temporary ba range with given frequency ranges */
struct gsm322_ba_list *gsm322_cs_ba_range(struct osmocom_ms *ms,
	uint32_t *range, uint8_t ranges, uint8_t refer_pcs)
{
	static struct gsm322_ba_list ba;
	int lower, higher;
	char lower_text[ARFCN_TEXT_LEN], higher_text[ARFCN_TEXT_LEN];

	memset(&ba, 0, sizeof(ba));

	while(ranges--) {
		lower = *range & 1023;
		higher = (*range >> 16) & 1023;
		if (refer_pcs && lower >= 512 && lower <= 810) {
			if (higher < 512 || higher > 810 || higher < lower) {
				LOGP(DCS, LOGL_NOTICE, "Illegal PCS range: "
					"%d..%d\n", lower, higher);
				range++;
				continue;
			}
			lower += 1024-512;
			higher += 1024-512;
		}
		range++;
		strncpy(lower_text,  gsm_print_arfcn(index2arfcn(lower)),  ARFCN_TEXT_LEN);
		strncpy(higher_text, gsm_print_arfcn(index2arfcn(higher)), ARFCN_TEXT_LEN);
		LOGP(DCS, LOGL_INFO, "Use BA range: %s..%s\n",
			lower_text,
			higher_text);
		/* GSM 05.08 6.3 */
		while (1) {
			ba.freq[lower >> 3] |= 1 << (lower & 7);
			if (lower == higher)
				break;
			lower++;
			/* wrap arround, only if not PCS */
			if (lower == 1024)
				lower = 0;
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
		ba = gsm322_cs_ba_range(ms, rr->ba_range, rr->ba_ranges,
			gsm_refer_pcs(cs->sel_arfcn, &cs->sel_si));
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
	for (i = 0; i <= 1023+299; i++) {
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
			LOGP(DCS, LOGL_INFO, "Cell not selected anymore, "
				"choose cell!\n");
			goto choose;
		}
		cs->arfcn = cs->sel_arfcn;
		cs->arfci = arfcn2index(cs->arfcn);

		/* be sure to go to current camping frequency on return */
		LOGP(DCS, LOGL_INFO, "Selecting ARFCN %s. after LOC.UPD.\n",
			gsm_print_arfcn(cs->arfcn));
		cs->sync_retries = SYNC_RETRIES;
		gsm322_sync_to_cell(cs, NULL, 0);
		cs->si = cs->list[cs->arfci].sysinfo;
		if (!cs->si) {
			printf("No SI when ret.idle, please fix!\n");
			exit(0L);
		}

		new_c_state(cs, GSM322_C3_CAMPED_NORMALLY);

		/* tell that we have selected the cell, so RR returns IDLE */
		nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_CELL_SELECTED);
		if (!nmsg)
			return -ENOMEM;
		gsm48_mmevent_msg(ms, nmsg);

		return 0;
	}

choose:
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
	struct gsm322_msg *gm = (struct gsm322_msg *) msg->data;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_ba_list *ba;

	cs->mcc = plmn->mcc;
	cs->mnc = plmn->mnc;

	if (gm->limited) {
		LOGP(DCS, LOGL_INFO, "Selected PLMN with limited service.\n");
		return gsm322_c_any_cell_sel(ms, msg);
	}

	LOGP(DSUM, LOGL_INFO, "Selecting PLMN (mcc=%s mnc=%s  %s, %s)\n",
		gsm_print_mcc(cs->mcc), gsm_print_mnc(cs->mnc),
		gsm_get_mcc(cs->mcc), gsm_get_mnc(cs->mcc, cs->mnc));

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
	if (!cs->selected) {
		LOGP(DCS, LOGL_INFO, "No cell selected, please fix!\n");
		exit(0L);
	}
	cs->arfcn = cs->sel_arfcn;
	cs->arfci = arfcn2index(cs->arfcn);

	/* maybe we are currently syncing to neighbours */
	stop_cs_timer(cs);

	new_c_state(cs, GSM322_CONNECTED_MODE_1);

	/* be sure to go to current camping frequency on return */
	LOGP(DCS, LOGL_INFO, "Going to camping (normal) ARFCN %s.\n",
		gsm_print_arfcn(cs->arfcn));
	cs->si = cs->list[cs->arfci].sysinfo;
	if (!cs->si) {
		printf("No SI when leaving idle, please fix!\n");
		exit(0L);
	}
	cs->sync_retries = SYNC_RETRIES;
	gsm322_sync_to_cell(cs, NULL, 1);

	return 0;
}

static int gsm322_c_conn_mode_2(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;

	/* check for error */
	if (!cs->selected) {
		LOGP(DCS, LOGL_INFO, "No cell selected, please fix!\n");
		exit(0L);
	}
	cs->arfcn = cs->sel_arfcn;
	cs->arfci = arfcn2index(cs->arfcn);

	stop_cs_timer(cs);

	new_c_state(cs, GSM322_CONNECTED_MODE_2);

	/* be sure to go to current camping frequency on return */
	LOGP(DCS, LOGL_INFO, "Going to camping (any cell) ARFCN %s.\n",
		gsm_print_arfcn(cs->arfcn));
	cs->si = cs->list[cs->arfci].sysinfo;
	if (!cs->si) {
		printf("No SI when leaving idle, please fix!\n");
		exit(0L);
	}
	cs->sync_retries = SYNC_RETRIES;
	gsm322_sync_to_cell(cs, NULL, 1);

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
	 GSM322_EVENT_REG_SUCCESS, gsm322_a_go_on_plmn},

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
		get_a_state_name(plmn->state));
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
	 GSM322_EVENT_REG_SUCCESS, gsm322_m_go_on_plmn},

	{SBIT(GSM322_M2_ON_PLMN),
	 GSM322_EVENT_ROAMING_NA, gsm322_m_display_plmns},

	/* undocumented case, where we loose coverage */
	{SBIT(GSM322_M2_ON_PLMN),
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_m_display_plmns},

	{SBIT(GSM322_M1_TRYING_RPLMN) | SBIT(GSM322_M2_ON_PLMN) |
	 SBIT(GSM322_M4_TRYING_PLMN),
	 GSM322_EVENT_INVALID_SIM, gsm322_m_sim_removed},

	{SBIT(GSM322_M3_NOT_ON_PLMN) | SBIT(GSM322_M2_ON_PLMN),
	 GSM322_EVENT_USER_RESEL, gsm322_m_user_resel},

	{SBIT(GSM322_M3_NOT_ON_PLMN),
	 GSM322_EVENT_PLMN_AVAIL, gsm322_m_plmn_avail},

	/* choose plmn is only specified when 'not on PLMN', but it makes
	 * sense to select cell from other states too. */
	{SBIT(GSM322_M3_NOT_ON_PLMN) | SBIT(GSM322_M2_ON_PLMN) |
	 SBIT(GSM322_M1_TRYING_RPLMN) | SBIT(GSM322_M4_TRYING_PLMN),
	 GSM322_EVENT_CHOOSE_PLMN, gsm322_m_choose_plmn},

	{SBIT(GSM322_M4_TRYING_PLMN),
	 GSM322_EVENT_REG_SUCCESS, gsm322_m_go_on_plmn},

	/* we also display available PLMNs after trying to register.
	 * this is not standard. we need that so the user knows
	 * that registration failed, and the user can select a new network. */
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
		get_m_state_name(plmn->state));
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
	 GSM322_EVENT_SIM_REMOVE, gsm322_c_sim_remove},

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
	 SBIT(GSM322_C0_NULL) /* after search */,
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_c_any_cell_sel},

	{SBIT(GSM322_C2_STORED_CELL_SEL) | SBIT(GSM322_C5_CHOOSE_CELL) |
	 SBIT(GSM322_C4_NORMAL_CELL_RESEL),
	 GSM322_EVENT_NO_CELL_FOUND, gsm322_c_normal_cell_sel},

	{SBIT(GSM322_C3_CAMPED_NORMALLY),
	 GSM322_EVENT_LEAVE_IDLE, gsm322_c_conn_mode_1},

	{SBIT(GSM322_C7_CAMPED_ANY_CELL),
	 GSM322_EVENT_LEAVE_IDLE, gsm322_c_conn_mode_2},

	{SBIT(GSM322_CONNECTED_MODE_1),
	 GSM322_EVENT_RET_IDLE, gsm322_c_choose_cell},

	{SBIT(GSM322_CONNECTED_MODE_2),
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
	 SBIT(GSM322_C6_ANY_CELL_SEL) | SBIT(GSM322_ANY_SEARCH) |
	 SBIT(GSM322_PLMN_SEARCH) | SBIT(GSM322_HPLMN_SEARCH) ,
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
			get_cs_state_name(cs->state));
	/* find function for current state and message */
	for (i = 0; i < CELLSELSLLEN; i++)
		if ((msg_type == cellselstatelist[i].type)
		 && ((1 << cs->state) & cellselstatelist[i].states))
			break;
	if (i == CELLSELSLLEN) {
		if (msg_type != GSM322_EVENT_SYSINFO)
			LOGP(DCS, LOGL_NOTICE, "Event unhandled at this state."
				"\n");
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
 * neighbour cell measurement process in idle mode
 */

static struct gsm322_neighbour *gsm322_nb_alloc(struct gsm322_cellsel *cs,
	uint16_t arfcn)
{
	struct gsm322_neighbour *nb;
	time_t now;

	time(&now);

	nb = talloc_zero(l23_ctx, struct gsm322_neighbour);
	if (!nb)
		return 0;

	nb->cs = cs;
	nb->arfcn = arfcn;
	nb->rla_c_dbm = -128;
	nb->created = now;
	llist_add_tail(&nb->entry, &cs->nb_list);

	return nb;
}

static void gsm322_nb_free(struct gsm322_neighbour *nb)
{
	llist_del(&nb->entry);
	talloc_free(nb);
}

/* check and calculate reselection criterion for all 6 neighbour cells and
 * return, if cell reselection has to be triggered */
static int gsm322_nb_check(struct osmocom_ms *ms, int any)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm_settings *set = &ms->settings;
	struct gsm48_sysinfo *s;
	int i = 0, reselect = 0;
	uint16_t acc_class;
	int band, class;
	struct gsm322_neighbour *nb;
	time_t now;
	char arfcn_text[10];

	time(&now);

	/* set out access class depending on the cell selection type */
	if (any) {
		acc_class = (subscr->acc_class | 0x0400); /* add emergency */
		LOGP(DNB, LOGL_DEBUG, "Re-select using access class with "
			"Emergency class.\n");
	} else {
		acc_class = subscr->acc_class;
		LOGP(DNB, LOGL_DEBUG, "Re-select using access class.\n");
	}

	if (ms->rrlayer.monitor) {
		vty_notify(ms, "MON: cell    ARFCN     LAC    C1  C2  CRH RLA_C "
			"bargraph\n");
		snprintf(arfcn_text, 10, "%s         ",
			gsm_print_arfcn(cs->sel_arfcn));
		arfcn_text[9] = '\0';
		vty_notify(ms, "MON: serving %s 0x%04x %3d %3d     %4d  "
			"%s\n", arfcn_text, cs->sel_lac, cs->c1, cs->c2,
			cs->rla_c_dbm, bargraph(cs->rla_c_dbm / 2, -55, -24));
	}

	/* loop through all neighbour cells and select best cell */
	llist_for_each_entry(nb, &cs->nb_list, entry) {
		LOGP(DNB, LOGL_INFO, "Checking cell of ARFCN %s for cell "
			"re-selection.\n", gsm_print_arfcn(nb->arfcn));
		s = cs->list[arfcn2index(nb->arfcn)].sysinfo;
		nb->checked_for_resel = 0;
		nb->suitable_allowable = 0;
		nb->c12_valid = 1;
		nb->prio_low = 0;

		if (nb->state == GSM322_NB_NOT_SUP) {
			LOGP(DNB, LOGL_INFO, "Skip cell: ARFCN not supported."
				"\n");
			if (ms->rrlayer.monitor) {
				snprintf(arfcn_text, 10, "%s         ",
					gsm_print_arfcn(nb->arfcn));
				arfcn_text[9] = '\0';
				vty_notify(ms, "MON: nb %2d   %s  ARFCN not "
					"supported\n", i + 1, arfcn_text);
			}
			goto cont;
		}
		/* check if we have successfully read BCCH */
		if (!s || nb->state != GSM322_NB_SYSINFO) {
			LOGP(DNB, LOGL_INFO, "Skip cell: There are no system "
				"informations available.\n");
			if (ms->rrlayer.monitor) {
				snprintf(arfcn_text, 10, "%s         ",
					gsm_print_arfcn(nb->arfcn));
				arfcn_text[9] = '\0';
				vty_notify(ms, "MON: nb %2d   %s           "
					"         %4d  %s\n",
					i + 1, arfcn_text, nb->rla_c_dbm,
					bargraph(nb->rla_c_dbm / 2, -55, -24));
			}
			goto cont;
		}

		/* get prio */
		if (s->sp && s->sp_cbq)
			nb->prio_low = 1;

		/* get C1 & C2 */
		band = gsm_arfcn2band(nb->arfcn);
		class = class_of_band(ms, band);
		nb->c1 = calculate_c1(DNB, nb->rla_c_dbm, s->rxlev_acc_min_db,
			ms_pwr_dbm(band, s->ms_txpwr_max_cch),
			ms_class_gmsk_dbm(band, class));
		nb->c2 = calculate_c2(nb->c1, 0,
			(cs->last_serving_valid
				&& cs->last_serving_arfcn == nb->arfcn),
				s->sp, s->sp_cro, now - nb->created, s->sp_pt,
				s->sp_to);
		nb->c12_valid = 1;

		/* calculate CRH depending on LAI */
		if (cs->sel_mcc == s->mcc && cs->sel_mnc == s->mnc
		 && cs->sel_lac == s->lac) {
			LOGP(DNB, LOGL_INFO, "-> Cell of is in the same LA, "
				"so CRH = 0\n");
			nb->crh = 0;
		} else if (any) {
			LOGP(DNB, LOGL_INFO, "-> Cell of is in a different LA, "
				"but service is limited, so CRH = 0\n");
			nb->crh = 0;
		} else {
			nb->crh = s->cell_resel_hyst_db;
			LOGP(DNB, LOGL_INFO, "-> Cell of is in a different LA, "
				"and service is normal, so CRH = %d\n",
				nb->crh);
		}

		if (ms->rrlayer.monitor) {
			snprintf(arfcn_text, 10, "%s         ",
				gsm_print_arfcn(nb->arfcn));
			arfcn_text[9] = '\0';
			vty_notify(ms, "MON: nb %2d   %s 0x%04x %3d %3d %2d"
				"  %4d  %s\n", i + 1, arfcn_text, s->lac,
				nb->c1, nb->c2, nb->crh, nb->rla_c_dbm,
				bargraph(nb->rla_c_dbm / 2, -55, -24));
		}

		/* if cell is barred and we don't override */
		if (s->cell_barr && !(s->sp && s->sp_cbq)) {
			LOGP(DNB, LOGL_INFO, "Skip cell: Cell is barred.\n");
			goto cont;
		}

		/* if we have no access to the cell and we don't override */
		if (!subscr->acc_barr
		 && !(acc_class & (s->class_barr ^ 0xffff))) {
			LOGP(DNB, LOGL_INFO, "Skip cell: Class is "
				"barred for our access. (access=%04x "
				"barred=%04x)\n", acc_class, s->class_barr);
			goto cont;
		}

		/* check if LA is forbidden */
		if (any && gsm322_is_forbidden_la(ms, s->mcc, s->mnc, s->lac)) {
			LOGP(DNB, LOGL_INFO, "Skip cell: Cell has "
				"forbidden LA.\n");
			goto cont;
		}

		/* check if we have same PLMN */
		if (!any && (cs->sel_mcc != s->mcc || cs->sel_mnc != s->mnc)) {
			LOGP(DNB, LOGL_INFO, "Skip cell: PLMN of cell "
				"does not match target PLMN. (cell: mcc=%s "
				"mnc=%s)\n", gsm_print_mcc(s->mcc),
				gsm_print_mnc(s->mnc));
			goto cont;
		}

		/* check criterion C1 */
		if (nb->c1 < 0) {
			LOGP(DNB, LOGL_INFO, "Skip cell: C1 criterion "
				" (>0) not met. (C1 = %d)\n", nb->c1);
			goto cont;
		}

		/* we can use this cell, if it is better */
		nb->suitable_allowable = 1;

		/* check priority */
		if (!cs->prio_low && nb->prio_low) {
			LOGP(DNB, LOGL_INFO, "Skip cell: cell has low "
				"priority, but serving cell has normal "
				"prio.\n");
			goto cont;
		}
		if (cs->prio_low && !nb->prio_low) {
			LOGP(DNB, LOGL_INFO, "Found cell: cell has normal "
				"priority, but serving cell has low prio.\n");
			reselect = 1;
			goto cont;
		}

		/* find better cell */
		if (nb->c2 - nb->crh > cs->c2) {
			LOGP(DNB, LOGL_INFO, "Found cell: cell is better "
				"than serving cell.\n");
			reselect = 1;
			goto cont;
		}

cont:
		if (++i == GSM58_NB_NUMBER)
			break;
	}

	if (!i) {
		if (ms->rrlayer.monitor)
			vty_notify(ms, "MON: no neighbour cells\n");
	}

	if (cs->resel_when + GSM58_RESEL_THRESHOLD >= now) {
		LOGP(DNB, LOGL_INFO, "Found better neighbour cell, but "
			"reselection threshold not reached.\n");
		reselect = 0;
	}

	if (reselect && set->stick) {
		LOGP(DNB, LOGL_INFO, "Don't trigger cell re-selection, because "
			"we stick to serving cell.\n");
		reselect = 0;
	}

	return reselect;
}

/* select a suitable and allowable cell */
static int gsm322_nb_scan(struct osmocom_ms *ms)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm_settings *set = &ms->settings;
	int i = 0;
	struct gsm322_neighbour *nb, *best_nb_low = NULL, *best_nb_normal = 0;
	int16_t best_low = -32768, best_normal = -32768;

	if (set->stick) {
		LOGP(DCS, LOGL_DEBUG, "Do not re-select cell, because we stick "
			" to a cell.\n");
		goto no_cell_found;
	}

	if (!cs->c12_valid) {
		LOGP(DCS, LOGL_DEBUG, "Do not re-select cell, because there "
			" are no valid C1 and C2.\n");
		goto no_cell_found;
	}

	/* loop through all neighbour cells and select best cell */
	llist_for_each_entry(nb, &cs->nb_list, entry) {
		LOGP(DCS, LOGL_INFO, "Checking cell with ARFCN %s for cell "
			"re-selection. (C2 = %d)\n", gsm_print_arfcn(nb->arfcn),
			nb->c2);
		/* track which cells have been checked do far */
		if (nb->checked_for_resel) {
			LOGP(DCS, LOGL_INFO, "Skip cell: alredy tried to "
				"select.\n");
			goto cont;
		}

		/* check if we can use this cell */
		if (!nb->suitable_allowable) {
			LOGP(DCS, LOGL_INFO, "Skip cell: not suitable and/or "
				"allowable.\n");
			goto cont;
		}

		/* check if cell is "better" */
		if (nb->prio_low) {
			if (nb->c2 - nb->crh > best_low) {
				best_low = nb->c2 - nb->crh;
				best_nb_low = nb;
			}
		} else {
			if (nb->c2 - nb->crh > best_normal) {
				best_normal = nb->c2 - nb->crh;
				best_nb_normal = nb;
			}
		}

cont:
		if (++i == GSM58_NB_NUMBER)
			break;
	}

	nb = NULL;
	if (best_nb_normal) {
		nb = best_nb_normal;
		LOGP(DCS, LOGL_INFO, "Best neighbour cell with ARFCN %s "
			"selected. (normal priority)\n",
			gsm_print_arfcn(nb->arfcn));
	}
	if (best_nb_low) {
		nb = best_nb_low;
		LOGP(DCS, LOGL_INFO, "Best neighbour cell with ARFCN %s "
			"selected. (low priority)\n",
			gsm_print_arfcn(nb->arfcn));
	}
	if (!nb) {
		struct msgb *nmsg;

		LOGP(DCS, LOGL_INFO, "No (more) acceptable neighbour cell "
			"available\n");

no_cell_found:
		/* Tell cell selection process to handle "no cell found". */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_NO_CELL_FOUND);
		if (!nmsg)
			return -ENOMEM;
		gsm322_cs_sendmsg(ms, nmsg);

		return 0;
	}
	nb->checked_for_resel = 1;

	/* NOTE: We might already have system information from previous
	 * scan. But we need recent informations, so we scan again!
	 */

	/* Tune to frequency for a while, to receive broadcasts. */
	cs->arfcn = nb->arfcn;
	cs->arfci = arfcn2index(cs->arfcn);
	LOGP(DCS, LOGL_DEBUG, "Scanning ARFCN %s of neighbour "
		"cell during cell reselection.\n", gsm_print_arfcn(cs->arfcn));
	/* Allocate/clean system information. */
	cs->list[cs->arfci].flags &= ~GSM322_CS_FLAG_SYSINFO;
	if (cs->list[cs->arfci].sysinfo)
		memset(cs->list[cs->arfci].sysinfo, 0,
			sizeof(struct gsm48_sysinfo));
	else
		cs->list[cs->arfci].sysinfo = talloc_zero(l23_ctx,
						struct gsm48_sysinfo);
	if (!cs->list[cs->arfci].sysinfo)
		exit(-ENOMEM);
	cs->si = cs->list[cs->arfci].sysinfo;
	cs->sync_retries = SYNC_RETRIES;
	return gsm322_sync_to_cell(cs, NULL, 0);
}

/* start/modify measurement process with the current list of neighbour cells.
 * only do that if: 1. we are camping  2. we are on serving cell */
static int gsm322_nb_start(struct osmocom_ms *ms, int synced)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = &cs->sel_si;
	struct gsm322_neighbour *nb, *nb2;
	int i, num;
	uint8_t map[128];
	uint16_t nc[32];
	uint8_t changed = 0;
	int refer_pcs, index;
	uint16_t arfcn;

	if (cs->ms->settings.no_neighbour)
		return 0;

	if (synced)
		cs->nb_meas_set = 0;

	refer_pcs = gsm_refer_pcs(cs->sel_arfcn, s);

	/* remove all neighbours that are not in list anymore */
	memset(map, 0, sizeof(map));
	llist_for_each_entry_safe(nb, nb2, &cs->nb_list, entry) {
		i = nb->arfcn & 1023;
		map[i >> 3] |= (1 << (i & 7));
#ifndef TEST_INCLUDE_SERV
		if (!(s->freq[i].mask & FREQ_TYPE_NCELL)) {
#else
		if (!(s->freq[i].mask & (FREQ_TYPE_NCELL | FREQ_TYPE_SERV))) {
#endif
			LOGP(DNB, LOGL_INFO, "Removing neighbour cell %s from "
				"list.\n", gsm_print_arfcn(nb->arfcn));
			gsm322_nb_free(nb);
			changed = 1;
			continue;
		}
#ifndef TEST_INCLUDE_SERV
		 if (nb->arfcn == cs->sel_arfcn) {
			LOGP(DNB, LOGL_INFO, "Removing serving cell %s (former "
				"neighbour cell).\n",
				gsm_print_arfcn(nb->arfcn));
			gsm322_nb_free(nb);
			changed = 1;
			continue;
		}
#endif
	}

	/* add missing entries to list */
	for (i = 0; i <= 1023; i++) {
#ifndef TEST_INCLUDE_SERV
		if ((s->freq[i].mask & FREQ_TYPE_NCELL) &&
		  !(map[i >> 3] & (1 << (i & 7)))) {
#else
		if ((s->freq[i].mask & (FREQ_TYPE_NCELL | FREQ_TYPE_SERV)) &&
		  !(map[i >> 3] & (1 << (i & 7)))) {
#endif
			index = i;
			if (refer_pcs && i >= 512 && i <= 810)
				index = i-512+1024;
			arfcn = index2arfcn(index);
#ifndef TEST_INCLUDE_SERV
			if (arfcn == cs->sel_arfcn) {
				LOGP(DNB, LOGL_INFO, "Omitting serving cell %s."
					"\n", gsm_print_arfcn(cs->arfcn));
				continue;
			}
#endif
			nb = gsm322_nb_alloc(cs, arfcn);
			LOGP(DNB, LOGL_INFO, "Adding neighbour cell %s to "
				"list.\n", gsm_print_arfcn(nb->arfcn));
			if (!(cs->list[index].flags & GSM322_CS_FLAG_SUPPORT))
				nb->state = GSM322_NB_NOT_SUP;
			changed = 1;
		}
	}

	/* if nothing has changed, we are done */
	if (!changed && cs->nb_meas_set)
		return 0;

	/* start neigbour cell measurement task */
	num = 0;
	llist_for_each_entry(nb, &cs->nb_list, entry) {
		if (nb->state == GSM322_NB_NOT_SUP)
			continue;
		/* it should not happen that there are more than 32 nb-cells */
		if (num == 32)
			break;
		nc[num] = nb->arfcn;
		num++;
	}
	LOGP(DNB, LOGL_INFO, "Sending list of neighbour cells to layer1.\n");
	l1ctl_tx_neigh_pm_req(ms, num, nc);
	cs->nb_meas_set = 1;

	return 1;
}


/* a complete set of measurements are received, calculate the RLA_C, sort */
static int gsm322_nb_trigger_event(struct gsm322_cellsel *cs)
{
	struct osmocom_ms *ms = cs->ms;
	struct gsm322_neighbour *nb, *nb_sync = NULL, *nb_again = NULL;
	int i = 0;
	time_t now;

	time(&now);

	/* check the list for reading neighbour cell's BCCH */
	llist_for_each_entry(nb, &cs->nb_list, entry) {
		if (nb->rla_c_dbm >= cs->ms->settings.min_rxlev_dbm) {
			/* select the strongest unsynced cell */
			if (nb->state == GSM322_NB_RLA_C) {
				nb_sync = nb;
				break;
			}
#if 0
if (nb->state == GSM322_NB_SYSINFO) {
printf("%d time to sync again: %u\n", nb->arfcn, now + GSM58_READ_AGAIN - nb->when);
}
#endif
			/* select the strongest cell to be read/try again */
			if (!nb_again) {
				if ((nb->state == GSM322_NB_NO_SYNC
				  || nb->state == GSM322_NB_NO_BCCH)
				 && nb->when + GSM58_TRY_AGAIN <= now)
					nb_again = nb;
				else
				if (nb->state == GSM322_NB_SYSINFO
				 && nb->when + GSM58_READ_AGAIN <= now)
					nb_again = nb;
			}
		}
		if (++i == GSM58_NB_NUMBER)
			break;
	}

	/* trigger sync to neighbour cell, priorize the untested cell */
	if (nb_sync || nb_again) {
		if (nb_sync) {
			nb = nb_sync;
			cs->arfcn = nb->arfcn;
			cs->arfci = arfcn2index(cs->arfcn);
			LOGP(DNB, LOGL_INFO, "Syncing to new neighbour cell "
				"%s.\n", gsm_print_arfcn(cs->arfcn));
		} else {
			nb = nb_again;
			cs->arfcn = nb->arfcn;
			cs->arfci = arfcn2index(cs->arfcn);
			LOGP(DNB, LOGL_INFO, "Syncing again to neighbour cell "
				"%s after timerout.\n",
				gsm_print_arfcn(cs->arfcn));
		}
		/* Allocate/clean system information. */
		cs->list[cs->arfci].flags &= ~GSM322_CS_FLAG_SYSINFO;
		if (cs->list[cs->arfci].sysinfo)
			memset(cs->list[cs->arfci].sysinfo, 0,
				sizeof(struct gsm48_sysinfo));
		else
			cs->list[cs->arfci].sysinfo = talloc_zero(l23_ctx,
							struct gsm48_sysinfo);
		if (!cs->list[cs->arfci].sysinfo)
			exit(-ENOMEM);
		cs->si = cs->list[cs->arfci].sysinfo;
		cs->sync_retries = SYNC_RETRIES;
		return gsm322_sync_to_cell(cs, nb, 0);
	}

	if (gsm322_nb_check(ms, (cs->state == GSM322_C7_CAMPED_ANY_CELL)) > 0) {
		struct msgb *nmsg;

		LOGP(DNB, LOGL_INFO, "Better neighbour cell triggers cell "
			"reselection.\n");

		if (ms->rrlayer.monitor)
			vty_notify(ms, "MON: trigger cell re-selection: "
				"better cell\n");

		cs->resel_when = now;

		/* unset selected cell */
		gsm322_unselect_cell(cs);

		nmsg = gsm322_msgb_alloc(GSM322_EVENT_CELL_RESEL);
		if (!nmsg)
			return -ENOMEM;
		gsm322_c_event(ms, nmsg);
		msgb_free(nmsg);
		return 0;
	}

	if (cs->neighbour) {
		cs->arfcn = cs->sel_arfcn;
		cs->arfci = arfcn2index(cs->arfcn);
		cs->si = cs->list[cs->arfci].sysinfo;
		if (!cs->si) {
			printf("No SI after neighbour scan, please fix!\n");
			exit(0L);
		}
		LOGP(DNB, LOGL_INFO, "Syncing back to serving cell\n");
		cs->sync_retries = SYNC_RETRIES_SERVING;
		return gsm322_sync_to_cell(cs, NULL, 0);
	}

	/* do nothing */
	return 0;
}


/* we (successfully) synced to a neighbour */
static int gsm322_nb_synced(struct gsm322_cellsel *cs, int yes)
{
	time_t now;

	LOGP(DNB, LOGL_INFO, "%s to neighbour cell %d.\n",
		(yes) ? "Synced" : "Failed to sync", cs->arfcn);

	if (yes) {
		start_cs_timer(cs, GSM322_NB_TIMEOUT, 0);
		return 0;
	}

	cs->neighbour->state = GSM322_NB_NO_SYNC;
	time(&now);
	cs->neighbour->when = now;

	return gsm322_nb_trigger_event(cs);
}

/* we (successfully) read the neighbour */
static int gsm322_nb_read(struct gsm322_cellsel *cs, int yes)
{
	time_t now;

	LOGP(DNB, LOGL_INFO, "%s from neighbour cell %d (rxlev %s).\n",
		(yes) ? "Read" : "Failed to read",
		cs->arfcn, gsm_print_rxlev(cs->list[cs->arfci].rxlev));

	cs->neighbour->state = (yes) ? GSM322_NB_SYSINFO : GSM322_NB_NO_BCCH;
	time(&now);
	cs->neighbour->when = now;

	return gsm322_nb_trigger_event(cs);
}

/* a complete set of measurements are received, calculate the RLA_C, sort */
static int gsm322_nb_new_rxlev(struct gsm322_cellsel *cs)
{
	struct gsm322_neighbour *nb, *strongest_nb;
	int i = 0;
	int8_t strongest;
	struct llist_head sorted;
	struct llist_head *lh, *lh2;
	struct gsm48_sysinfo *s = &cs->sel_si;
	int band = gsm_arfcn2band(cs->arfcn);
	int class = class_of_band(cs->ms, band);


	/* calculate the RAL_C of serving cell */
	if (cs->rxlev_count) {
		cs->rla_c_dbm = (cs->rxlev_sum_dbm + (cs->rxlev_count / 2))
					/ cs->rxlev_count;
		cs->rxlev_sum_dbm = 0;
		cs->rxlev_count = 0;
	}

	LOGP(DNB, LOGL_INFO, "RLA_C of serving cell: %d\n", cs->rla_c_dbm);

	/* calculate C1 criterion, SI 3 carries complete neighbour cell info */
	cs->prio_low = 0;
	if (s && (s->si3 || s->si4)) {
		cs->c1 = calculate_c1(DNB, cs->rla_c_dbm, s->rxlev_acc_min_db,
			ms_pwr_dbm(band, s->ms_txpwr_max_cch),
			ms_class_gmsk_dbm(band, class));
		cs->c2 = calculate_c2(cs->c1, 1, 0, s->sp, s->sp_cro, 0, s->sp_pt, s->sp_to);
		cs->c12_valid = 1;

		if (s->sp && s->sp_cbq)
			cs->prio_low = 1;
	}

	/* calculate the RAL_C of neighbours */
	llist_for_each_entry(nb, &cs->nb_list, entry) {
		if (nb->state == GSM322_NB_NOT_SUP)
			continue;
		/* if sysinfo is gone due to scanning, mark neighbour as
		 * unscanned. */
		if (nb->state == GSM322_NB_SYSINFO) {
			if (!cs->list[arfcn2index(nb->arfcn)].sysinfo) {
				nb->state = GSM322_NB_NO_BCCH;
				nb->when = 0;
			}
		}
		nb->rla_c_dbm =
			(nb->rxlev_sum_dbm + (nb->rxlev_count / 2))
				/ nb->rxlev_count;
		nb->rxlev_count = 0;
		nb->rxlev_sum_dbm = 0;
		if (nb->state == GSM322_NB_NEW)
			nb->state = GSM322_NB_RLA_C;
	}

	/* sort the 6 strongest */
	INIT_LLIST_HEAD(&sorted);

	/* detach up to 6 of the strongest neighbour cells from list and put
	 * them in the "sorted" list */
	while (!llist_empty(&cs->nb_list)) {
		strongest = -128;
		strongest_nb = NULL;
		llist_for_each_entry(nb, &cs->nb_list, entry) {
			if (nb->state == GSM322_NB_NOT_SUP)
				continue;
			if (nb->rla_c_dbm > strongest) {
				strongest = nb->rla_c_dbm;
				strongest_nb = nb;
			}
		}
		if (strongest_nb == NULL) /* this should not happen */
			break;
		LOGP(DNB, LOGL_INFO, "#%d ARFCN=%d RLA_C=%d\n",
			i+1, strongest_nb->arfcn, strongest_nb->rla_c_dbm);
		llist_del(&strongest_nb->entry);
		llist_add(&strongest_nb->entry, &sorted);
		if (++i == GSM58_NB_NUMBER)
			break;
	}

	/* take the sorted list and attat it to the head of the neighbour cell
	 * list */
	llist_for_each_safe(lh, lh2, &sorted) {
		llist_del(lh);
		llist_add(lh, &cs->nb_list);
	}

	return gsm322_nb_trigger_event(cs);
}

/* accumulate the measurement results and check if there is a complete set for
 * all neighbour cells received. */
static int gsm322_nb_meas_ind(struct osmocom_ms *ms, uint16_t arfcn,
	uint8_t rx_lev)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm322_neighbour *nb;
	int enough_results = 1, result = 0;

	llist_for_each_entry(nb, &cs->nb_list, entry) {
		if (nb->state == GSM322_NB_NOT_SUP)
			continue;
		if (arfcn != nb->arfcn) {
			if (nb->rxlev_count < RLA_C_NUM)
				enough_results = 0;
			continue;
		}
		nb->rxlev_sum_dbm += rx_lev - 110;
		nb->rxlev_count++;
		LOGP(DNB, LOGL_INFO, "Measurement result for ARFCN %s: %d\n",
			gsm_print_arfcn(arfcn), rx_lev - 110);

		if (nb->rxlev_count < RLA_C_NUM)
			enough_results = 0;

		result = 1;
	}

	if (!result)
		LOGP(DNB, LOGL_INFO, "Measurement result for ARFCN %s not "
			"requested. (not a bug)\n", gsm_print_arfcn(arfcn));

	if (enough_results)
		return gsm322_nb_new_rxlev(cs);

	return 0;
}

int gsm322_meas(struct osmocom_ms *ms, uint8_t rx_lev)
{
	struct gsm322_cellsel *cs = &ms->cellsel;

	if (cs->neighbour)
		return -EINVAL;

	cs->rxlev_sum_dbm += rx_lev - 110;
	cs->rxlev_count++;

	return 0;
}

/*
 * dump lists
 */

int gsm322_dump_sorted_plmn(struct osmocom_ms *ms)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_plmn_list *temp;

	LOGP(DPLMN, LOGL_INFO, "MCC    |MNC    |allowed|rx-lev\n");
	LOGP(DPLMN, LOGL_INFO, "-------+-------+-------+-------\n");
	llist_for_each_entry(temp, &plmn->sorted_plmn, entry) {
		LOGP(DPLMN, LOGL_INFO, "%s    |%s%s    |%s    |%s\n",
			gsm_print_mcc(temp->mcc), gsm_print_mnc(temp->mnc),
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

	print(priv, "ARFCN  |MCC    |MNC    |LAC    |cell ID|forb.LA|prio   |"
		"min-db |max-pwr|rx-lev\n");
	print(priv, "-------+-------+-------+-------+-------+-------+-------+"
		"-------+-------+-------\n");
	for (i = 0; i <= 1023+299; i++) {
		s = cs->list[i].sysinfo;
		if (!s || !(cs->list[i].flags & flags))
			continue;
		if (i >= 1024)
			print(priv, "%4dPCS|", i-1024+512);
		else if (i >= 512 && i <= 885)
			print(priv, "%4dDCS|", i);
		else
			print(priv, "%4d   |", i);
		if (s->mcc) {
			print(priv, "%s    |%s%s    |", gsm_print_mcc(s->mcc),
				gsm_print_mnc(s->mnc),
				((s->mnc & 0x00f) == 0x00f) ? " ":"");
			print(priv, "0x%04x |0x%04x |", s->lac, s->cell_id);
		} else
			print(priv, "n/a    |n/a    |n/a    |n/a    |");
		if ((cs->list[i].flags & GSM322_CS_FLAG_SYSINFO)) {
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
		} else
			print(priv, "n/a    |n/a    |");
		if (s->si3 || s->si4)
			print(priv, "%4d   |%4d   |%s\n", s->rxlev_acc_min_db,
				s->ms_txpwr_max_cch,
				gsm_print_rxlev(cs->list[i].rxlev));
		else
			print(priv, "n/a    |n/a    |n/a\n");
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
		for (i = 0; i <= 1023+299; i++) {
			if ((ba->freq[i >> 3] & (1 << (i & 7))))
				print(priv, " %s",
					gsm_print_arfcn(index2arfcn(i)));
		}
		print(priv, "\n");
	}

	return 0;
}

int gsm322_dump_nb_list(struct gsm322_cellsel *cs,
			void (*print)(void *, const char *, ...), void *priv)
{
	struct gsm48_sysinfo *s;
	struct gsm322_neighbour *nb;
	int i = 0;

	if (!cs->selected) {
		print(priv, "No serving cell selected (yet).\n");
		return 0;
	}
	print(priv, "Serving cell:\n\n");
	print(priv, "ARFCN=%s  ", gsm_print_arfcn(cs->sel_arfcn));
	print(priv, "RLA_C=%s  ", gsm_print_rxlev(cs->rla_c_dbm + 110));
	if (cs->c12_valid)
		print(priv, "C1=%d  C2=%d  ", cs->c1, cs->c1);
	else
		print(priv, "C1 -  C2 -  ");
	print(priv, "LAC=0x%04x\n\n", (cs->selected) ? cs->sel_si.lac : 0);

	print(priv, "Neighbour cells:\n\n");
	llist_for_each_entry(nb, &cs->nb_list, entry) {
		if (i == 0) {
			print(priv, "#      |ARFCN  |RLA_C  |C1     |C2     |"
				"CRH    |prio   |LAC    |cell ID|usable |"
				"state\n");
			print(priv, "----------------------------------------"
				"----------------------------------------"
				"-------\n");
		} else
		if (i == GSM58_NB_NUMBER)
			print(priv, "--- unmonitored cells: ---\n");
		i++;
		if (cs->last_serving_valid
		 && cs->last_serving_arfcn == nb->arfcn)
			print(priv, "%2d last|", i);
		else
			print(priv, "%2d     |", i);
		if ((nb->arfcn & ARFCN_PCS))
			print(priv, "%4dPCS|", nb->arfcn & 1023);
		else if (i >= 512 && i <= 885)
			print(priv, "%4dDCS|", nb->arfcn & 1023);
		else
			print(priv, "%4d   |", nb->arfcn);
		if (nb->state == GSM322_NB_NOT_SUP) {
			print(priv, "  ARFCN not supported\n");
			continue;
		}
		if (nb->rla_c_dbm > -128)
			print(priv, "%6s |",
				gsm_print_rxlev(nb->rla_c_dbm + 110));
		else
			print(priv, "-      |");
		if (nb->state == GSM322_NB_SYSINFO && nb->c12_valid)
			print(priv, "%4d   |%4d   |%4d   |", nb->c1, nb->c1,
				nb->crh);
		else
			print(priv, "-      |-      |-      |");
		s = cs->list[arfcn2index(nb->arfcn)].sysinfo;
		if (nb->state == GSM322_NB_SYSINFO && s) {
			print(priv, "%s |0x%04x |0x%04x |",
				(nb->prio_low) ? "low   ":"normal", s->lac,
				s->cell_id);
		} else
			print(priv, "-      |-      |-      |");

		print(priv, "%s    |",
			(nb->suitable_allowable) ? "yes" : "no ");
		print(priv, "%s\n", get_nb_state_name(nb->state));
	}

	if (i == 0)
		print(priv, "No neighbour cells available (yet).\n");

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
	char filename[PATH_MAX];
	int i;
	struct gsm322_ba_list *ba;
	uint8_t buf[4];
	char version[32];

	LOGP(DPLMN, LOGL_INFO, "init PLMN process\n");
	LOGP(DCS, LOGL_INFO, "init Cell Selection process\n");

	memset(plmn, 0, sizeof(*plmn));
	memset(cs, 0, sizeof(*cs));
	plmn->ms = ms;
	cs->ms = ms;

	/* set initial state */
	plmn->state = 0;
	cs->state = 0;

	/* init lists */
	INIT_LLIST_HEAD(&plmn->event_queue);
	INIT_LLIST_HEAD(&cs->event_queue);
	INIT_LLIST_HEAD(&plmn->sorted_plmn);
	INIT_LLIST_HEAD(&plmn->forbidden_la);
	INIT_LLIST_HEAD(&cs->ba_list);
	INIT_LLIST_HEAD(&cs->nb_list);

	/* set supported frequencies in cell selection list */
	for (i = 0; i <= 1023+299; i++)
		if ((ms->settings.freq_map[i >> 3] & (1 << (i & 7))))
			cs->list[i].flags |= GSM322_CS_FLAG_SUPPORT;

	/* read BA list */
	sprintf(filename, "%s/%s.ba", config_dir, ms->name);
	fp = fopen(filename, "r");
	if (fp) {
		int rc;
		char *s_rc;

		s_rc = fgets(version, sizeof(version), fp);
		version[sizeof(version) - 1] = '\0';
		if (!s_rc || !!strcmp(ba_version, version)) {
			LOGP(DCS, LOGL_NOTICE, "BA version missmatch, "
				"stored BA list becomes obsolete.\n");
		} else
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

	return 0;
}

int gsm322_exit(struct osmocom_ms *ms)
{
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct llist_head *lh, *lh2;
	struct msgb *msg;
	FILE *fp;
	char filename[PATH_MAX];
	struct gsm322_ba_list *ba;
	uint8_t buf[4];
	int i;

	LOGP(DPLMN, LOGL_INFO, "exit PLMN process\n");
	LOGP(DCS, LOGL_INFO, "exit Cell Selection process\n");

	/* stop cell selection process (if any) */
	new_c_state(cs, GSM322_C0_NULL);

	/* stop timers */
	stop_cs_timer(cs);
	stop_any_timer(cs);
	stop_plmn_timer(plmn);

	/* flush sysinfo */
	for (i = 0; i <= 1023+299; i++) {
		if (cs->list[i].sysinfo) {
			LOGP(DCS, LOGL_DEBUG, "free sysinfo ARFCN=%s\n",
				gsm_print_arfcn(index2arfcn(i)));
			talloc_free(cs->list[i].sysinfo);
			cs->list[i].sysinfo = NULL;
		}
		cs->list[i].flags = 0;
	}

	/* store BA list */
	sprintf(filename, "%s/%s.ba", config_dir, ms->name);
	fp = fopen(filename, "w");
	if (fp) {
		int rc;

		fputs(ba_version, fp);
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
	llist_for_each_safe(lh, lh2, &cs->nb_list)
		gsm322_nb_free(container_of(lh, struct gsm322_neighbour,
				entry));
	return 0;
}
