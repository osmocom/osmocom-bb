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
#include <arpa/inet.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/core/talloc.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/mobile/gsm48_cc.h>
#include <osmocom/bb/mobile/gsm480_ss.h>
#include <osmocom/bb/mobile/gsm411_sms.h>
#include <osmocom/bb/mobile/app_mobile.h>
#include <osmocom/bb/mobile/vty.h>

extern void *l23_ctx;

void mm_conn_free(struct gsm48_mm_conn *conn);
static int gsm48_rcv_rr(struct osmocom_ms *ms, struct msgb *msg);
static int gsm48_rcv_mmr(struct osmocom_ms *ms, struct msgb *msg);
static int gsm48_mm_ev(struct osmocom_ms *ms, int msg_type, struct msgb *msg);
static int gsm48_mm_tx_id_rsp(struct osmocom_ms *ms, uint8_t mi_type);
static int gsm48_mm_tx_loc_upd_req(struct osmocom_ms *ms);
static int gsm48_mm_loc_upd_failed(struct osmocom_ms *ms, struct msgb *msg);
static int gsm48_mm_conn_go_dedic(struct osmocom_ms *ms);
static int gsm48_mm_init_mm_reject(struct osmocom_ms *ms, struct msgb *msg);
static int gsm48_mm_data_ind(struct osmocom_ms *ms, struct msgb *msg);
static void new_mm_state(struct gsm48_mmlayer *mm, int state, int substate);
static int gsm48_mm_loc_upd_normal(struct osmocom_ms *ms, struct msgb *msg);
static int gsm48_mm_loc_upd_periodic(struct osmocom_ms *ms, struct msgb *msg);
static int gsm48_mm_loc_upd(struct osmocom_ms *ms, struct msgb *msg);

/*
 * notes
 */

/*
 * Notes on IMSI detach procedure:
 *
 * At the end of the procedure, the state of MM, RR, cell selection: No SIM.
 *
 * In MM IDLE state, cell available: RR is establised, IMSI detach specific
 * procedure is performed.
 *
 * In MM IDLE state, no cell: State is silently changed to No SIM.
 *
 * During any MM connection state, or Wait for network command: All MM
 * connections (if any) are released locally, and IMSI detach specific
 * procedure is performed.
 *
 * During IMSI detach processing: Request of IMSI detach is ignored.
 *
 * Any other state: The special 'delay_detach' flag is set only. If set, at any
 * state transition we will clear the flag and restart the procedure again.
 *
 * The procedure is not spec conform, but always succeeds.
 *
 */

/* Notes on Service states:
 *
 * There are two PLMN search states:
 *
 * - PLMN SEARCH NORMAL
 * - PLMN SEARCH
 *
 * They are entered, if: (4.2.1.2)
 * - ME is switched on
 * - SIM is inserted
 * - user has asked PLMN selection in certain Service states
 * - coverage is lost in certain Service states
 * - roaming is denied
 * - (optionally see 4.2.1.2)
 *
 * PLMN SEARCH NORMAL state is then entered, if all these conditions are met:
 *  - SIM is valid
 *  - SIM state is U1
 *  - SIM LAI valid
 *  - cell selected
 *  - cell == SIM LAI
 *
 * Otherwhise PLMN SEARCH is entered.
 *
 * During PLMN SEARCH NORMAL state: (4.2.2.5)
 *  - on expirery of T3212: Perform periodic location update, when back
 *    to NORMAL SERVICE state.
 *  - perform IMSI detach
 *  - perform MM connections
 *  - respond to paging (if possible)
 *
 * During PLMN SEARCH state: (4.2.2.6)
 *  - reject MM connection except for emergency calls
 *
 *
 * The NO CELL AVAILABLE state is entered, if:
 *  - no cell found during PLMN search
 *
 * During NO CELL AVAILABLE state:
 *  - reject any MM connection
 *
 * The NO IMSI state is entered if:
 *  - SIM is invalid
 *  - and cell is selected during PLMN SEARCH states
 *
 * During NO IMSO state: (4.2.2.4)
 *  - reject MM connection except for emergency calls
 *
 * The LIMITED SERVICE state is entered if:
 *  - SIM is valid
 *  - and SIM state is U3
 *  - and cell is selected
 *
 * During LIMITED SERVICE state: (4.2.2.3)
 *  - reject MM connection except for emergency calls
 *  - perform location update, if new LAI is entered
 *
 *
 * The LOCATION UPDATE NEEDED state is entered if:
 *  - SIM is valid
 *  - and location update must be performed for any reason
 *
 * During LOCATION UPDATE NEEDED state:
 *  - reject MM connection except for emergency calls
 *
 * In all IDLE states:
 *  - on expirery of T3211 or T3213: Perform location update, when back
 *    to NORMAL SERVICE state.
 *
 * This state is left if location update is possible and directly enter
 * state ATTEMPTING TO UPDATE and trigger location update.
 * The function gsm48_mm_loc_upd_possible() is used to check this on state
 * change.
 *
 *
 * The ATTEMPTING TO UPDATE state is entered if:
 *  - SIM is valid
 *  - and SIM state is U2
 *  - and cell is selected
 *
 * During ATTEMPTING TO UPDATE state: (4.2.2.2)
 *  - on expirery of T3211 or T3213: Perform location updated
 *  - on expirery of T3212: Perform location updated
 *  - on change of LAI: Perform location update
 *  - (abnormal cases unsupported)
 *  - accept MM connection for emergency calls
 *  - trigger location update on any other MM connection
 *  - respond to paging (with IMSI only, because in U2 TMSI is not valid)
 *
 *
 * The NORMAL SERVICE state is entered if:
 *  - SIM is valid
 *  - and SIM state is U1
 *  - and cell is selected
 *  - and SIM LAI == cell
 *
 * During NORMAL SERVICE state: (4.2.2.1)
 *  - on expirery of T3211 or T3213: Perform location updated
 *  - on expirery of T3212: Perform location updated
 *  - on change of LAI: Perform location update
 *  - perform IMSI detach
 *  - perform MM connections
 *  - respond to paging
 *
 *
 * gsm48_mm_set_plmn_search() is used enter PLMN SEARCH or PLMN SEARCH NORMAL
 * state. Depending on the conditions above, the appropiate state is selected.
 *
 *
 * gsm48_mm_return_idle() is used to select the Service state when returning
 * to MM IDLE state after cell reselection.
 *
 *
 * If cell selection process indicates NO_CELL_FOUND:
 *
 * - NO CELL AVAILABLE state is entered, if not already.
 *
 * gsm48_mm_no_cell_found() is used to select the Service state.
 *
 *
 * If cell selection process indicates CELL_SELECTED:
 *
 * - NO IMSI state is entered, if no SIM valid.
 * - Otherwise NORMAL SERVICES state is entered, if
 *   SIM state is U1, SIM LAI == cell, IMSI is attached, T3212 not expired.
 * - Otherwise NORMAL SERVICES state is entered, if
 *   SIM state is U1, SIM LAI == cell, attach not required, T3212 not expired.
 * - Otherwise LIMITED SERVICE state is entered, if
 *   CS mode is automatic, cell is forbidden PLMN or forbidden LA.
 * - Otherwise LIMITED SERVICE state is entered, if
 *   CS mode is manual, cell is not the selected one.
 * - Otherwise LOCATION UPDATE NEEDED state is entered.
 *
 * gsm48_mm_cell_selected() is used to select the Service state.
 *
 */

/*
 * support functions
 */

/* get supported power level of given arfcn */
uint8_t gsm48_current_pwr_lev(struct gsm_settings *set, uint16_t arfcn)
{
	uint8_t pwr_lev;

	if (arfcn >= (512 | ARFCN_PCS) && arfcn <= (810 | ARFCN_PCS))
		pwr_lev = set->class_pcs - 1;
	else if (arfcn >= 512 && arfcn <= 885)
		pwr_lev = set->class_dcs - 1;
	else if (arfcn >= 259 && arfcn <= 340)
		pwr_lev = set->class_400 - 1;
	else if (arfcn >= 128 && arfcn <= 251)
		pwr_lev = set->class_850 - 1;
	else
		pwr_lev = set->class_900 - 1;

	return pwr_lev;
}

/* decode network name */
static int decode_network_name(char *name, int name_len,
	const uint8_t *lv)
{
	uint8_t in_len = lv[0];
	int length, padding;

	name[0] = '\0';
	if (in_len < 1)
		return -EINVAL;

	/* must be CB encoded */
	if ((lv[1] & 0x70) != 0x00)
		return -ENOTSUP;

	padding = lv[1] & 0x03;
	length = ((in_len - 1) * 8 - padding) / 7;
	if (length <= 0)
		return 0;
	if (length >= name_len)
		length = name_len - 1;
	gsm_7bit_decode(name, lv + 2, length);
	name[length] = '\0';

	return length;
}

/* encode 'mobile identity' */
int gsm48_encode_mi(uint8_t *buf, struct msgb *msg, struct osmocom_ms *ms,
	uint8_t mi_type)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm_settings *set = &ms->settings;
	uint8_t *ie;

	switch(mi_type) {
	case GSM_MI_TYPE_TMSI:
		gsm48_generate_mid_from_tmsi(buf, subscr->tmsi);
		break;
	case GSM_MI_TYPE_IMSI:
		gsm48_generate_mid_from_imsi(buf, subscr->imsi);
		break;
	case GSM_MI_TYPE_IMEI:
		gsm48_generate_mid_from_imsi(buf, set->imei);
		break;
	case GSM_MI_TYPE_IMEISV:
		gsm48_generate_mid_from_imsi(buf, set->imeisv);
		break;
	case GSM_MI_TYPE_NONE:
	default:
	        buf[0] = GSM48_IE_MOBILE_ID;
	        buf[1] = 1;
	        buf[2] = 0xf0;
		break;
	}
	/* alter MI type */
	buf[2] = (buf[2] & ~GSM_MI_TYPE_MASK) | mi_type;

	if (msg) {
		/* MI as LV */
		ie = msgb_put(msg, 1 + buf[1]);
		memcpy(ie, buf + 1, 1 + buf[1]);
	}

	return 0;
}

/* encode 'classmark 1' */
int gsm48_encode_classmark1(struct gsm48_classmark1 *cm, uint8_t rev_lev,
	uint8_t es_ind, uint8_t a5_1, uint8_t pwr_lev)
{
	memset(cm, 0, sizeof(*cm));
	cm->rev_lev = rev_lev;
	cm->es_ind = es_ind;
	cm->a5_1 = !a5_1;
	cm->pwr_lev = pwr_lev;

	return 0;
}

/*
 * timers
 */

static void timeout_mm_t3210(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	LOGP(DMM, LOGL_INFO, "timer T3210 (loc. upd. timeout) has fired\n");
	gsm48_mm_ev(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3210, NULL);
}

static void timeout_mm_t3211(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	LOGP(DSUM, LOGL_INFO, "Location update retry\n");
	LOGP(DMM, LOGL_INFO, "timer T3211 (loc. upd. retry delay) has fired\n");
	gsm48_mm_ev(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3211, NULL);
}

static void timeout_mm_t3212(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	LOGP(DSUM, LOGL_INFO, "Periodic location update\n");
	LOGP(DMM, LOGL_INFO, "timer T3212 (periodic loc. upd. delay) has "
		"fired\n");

	/* reset attempt counter when attempting to update (4.4.4.5) */
	if (mm->state == GSM48_MM_ST_MM_IDLE
	 && mm->substate == GSM48_MM_SST_ATTEMPT_UPDATE)
		mm->lupd_attempt = 0;

	gsm48_mm_ev(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3212, NULL);
}

static void timeout_mm_t3213(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	LOGP(DSUM, LOGL_INFO, "Location update retry\n");
	LOGP(DMM, LOGL_INFO, "timer T3213 (delay after RA failure) has "
		"fired\n");
	gsm48_mm_ev(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3213, NULL);
}

static void timeout_mm_t3230(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	LOGP(DMM, LOGL_INFO, "timer T3230 (MM connection timeout) has "
		"fired\n");
	gsm48_mm_ev(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3230, NULL);
}

static void timeout_mm_t3220(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	LOGP(DMM, LOGL_INFO, "timer T3220 (IMSI detach keepalive) has "
		"fired\n");
	gsm48_mm_ev(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3220, NULL);
}

static void timeout_mm_t3240(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	LOGP(DMM, LOGL_INFO, "timer T3240 (RR release timeout) has fired\n");
	gsm48_mm_ev(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3240, NULL);
}

static void start_mm_t3210(struct gsm48_mmlayer *mm)
{
	LOGP(DMM, LOGL_INFO, "starting T3210 (loc. upd. timeout) with %d.%d "
		"seconds\n", GSM_T3210_MS);
	mm->t3210.cb = timeout_mm_t3210;
	mm->t3210.data = mm;
	osmo_timer_schedule(&mm->t3210, GSM_T3210_MS);
}

static void start_mm_t3211(struct gsm48_mmlayer *mm)
{
	LOGP(DMM, LOGL_INFO, "starting T3211 (loc. upd. retry delay) with "
		"%d.%d seconds\n", GSM_T3211_MS);
	mm->t3211.cb = timeout_mm_t3211;
	mm->t3211.data = mm;
	osmo_timer_schedule(&mm->t3211, GSM_T3211_MS);
}

static void start_mm_t3212(struct gsm48_mmlayer *mm, int sec)
{
	/* don't start, if is not available */
	if (!sec)
		return;

	LOGP(DMM, LOGL_INFO, "starting T3212 (periodic loc. upd. delay) with "
		"%d seconds\n", sec);
	mm->t3212.cb = timeout_mm_t3212;
	mm->t3212.data = mm;
	osmo_timer_schedule(&mm->t3212, sec, 0);
}

static void start_mm_t3213(struct gsm48_mmlayer *mm)
{
	LOGP(DMM, LOGL_INFO, "starting T3213 (delay after RA failure) with "
		"%d.%d seconds\n", GSM_T3213_MS);
	mm->t3213.cb = timeout_mm_t3213;
	mm->t3213.data = mm;
	osmo_timer_schedule(&mm->t3213, GSM_T3213_MS);
}

static void start_mm_t3220(struct gsm48_mmlayer *mm)
{
	LOGP(DMM, LOGL_INFO, "starting T3220 (IMSI detach keepalive) with "
		"%d.%d seconds\n", GSM_T3220_MS);
	mm->t3220.cb = timeout_mm_t3220;
	mm->t3220.data = mm;
	osmo_timer_schedule(&mm->t3220, GSM_T3220_MS);
}

static void start_mm_t3230(struct gsm48_mmlayer *mm)
{
	LOGP(DMM, LOGL_INFO, "starting T3230 (MM connection timeout) with "
		"%d.%d seconds\n", GSM_T3230_MS);
	mm->t3230.cb = timeout_mm_t3230;
	mm->t3230.data = mm;
	osmo_timer_schedule(&mm->t3230, GSM_T3230_MS);
}

static void start_mm_t3240(struct gsm48_mmlayer *mm)
{
	LOGP(DMM, LOGL_INFO, "starting T3240 (RR release timeout) with %d.%d "
		"seconds\n", GSM_T3240_MS);
	mm->t3240.cb = timeout_mm_t3240;
	mm->t3240.data = mm;
	osmo_timer_schedule(&mm->t3240, GSM_T3240_MS);
}

static void stop_mm_t3210(struct gsm48_mmlayer *mm)
{
	if (osmo_timer_pending(&mm->t3210)) {
		LOGP(DMM, LOGL_INFO, "stopping pending (loc. upd. timeout) "
			"timer T3210\n");
		osmo_timer_del(&mm->t3210);
	}
}

static void stop_mm_t3211(struct gsm48_mmlayer *mm)
{
	if (osmo_timer_pending(&mm->t3211)) {
		LOGP(DMM, LOGL_INFO, "stopping pending (loc. upd. retry "
			"delay) timer T3211\n");
		osmo_timer_del(&mm->t3211);
	}
}

static void stop_mm_t3212(struct gsm48_mmlayer *mm)
{
	if (osmo_timer_pending(&mm->t3212)) {
		LOGP(DMM, LOGL_INFO, "stopping pending (periodic loc. upd. "
			"delay) timer T3212\n");
		osmo_timer_del(&mm->t3212);
	}
}

static void stop_mm_t3213(struct gsm48_mmlayer *mm)
{
	if (osmo_timer_pending(&mm->t3213)) {
		LOGP(DMM, LOGL_INFO, "stopping pending (delay after RA "
			"failure) timer T3213\n");
		osmo_timer_del(&mm->t3213);
	}
}

static void stop_mm_t3220(struct gsm48_mmlayer *mm)
{
	if (osmo_timer_pending(&mm->t3220)) {
		LOGP(DMM, LOGL_INFO, "stopping pending (IMSI detach keepalive) "
			"timer T3220\n");
		osmo_timer_del(&mm->t3220);
	}
}

static void stop_mm_t3230(struct gsm48_mmlayer *mm)
{
	if (osmo_timer_pending(&mm->t3230)) {
		LOGP(DMM, LOGL_INFO, "stopping pending (MM connection timeout) "
			"timer T3230\n");
		osmo_timer_del(&mm->t3230);
	}
}

static void stop_mm_t3240(struct gsm48_mmlayer *mm)
{
	if (osmo_timer_pending(&mm->t3240)) {
		LOGP(DMM, LOGL_INFO, "stopping pending (RR release timeout) "
			"timer T3240\n");
		osmo_timer_del(&mm->t3240);
	}
}

static void stop_mm_t3241(struct gsm48_mmlayer *mm)
{
	/* not implemented, not required */
}

/*
 * messages
 */

/* names of MM events */
static const struct value_string gsm48_mmevent_names[] = {
	{ GSM48_MM_EVENT_CELL_SELECTED,	"MM_EVENT_CELL_SELECTED" },
	{ GSM48_MM_EVENT_NO_CELL_FOUND,	"MM_EVENT_NO_CELL_FOUND" },
	{ GSM48_MM_EVENT_TIMEOUT_T3210,	"MM_EVENT_TIMEOUT_T3210" },
	{ GSM48_MM_EVENT_TIMEOUT_T3211,	"MM_EVENT_TIMEOUT_T3211" },
	{ GSM48_MM_EVENT_TIMEOUT_T3212,	"MM_EVENT_TIMEOUT_T3212" },
	{ GSM48_MM_EVENT_TIMEOUT_T3213,	"MM_EVENT_TIMEOUT_T3213" },
	{ GSM48_MM_EVENT_TIMEOUT_T3220,	"MM_EVENT_TIMEOUT_T3220" },
	{ GSM48_MM_EVENT_TIMEOUT_T3230,	"MM_EVENT_TIMEOUT_T3230" },
	{ GSM48_MM_EVENT_TIMEOUT_T3240,	"MM_EVENT_TIMEOUT_T3240" },
	{ GSM48_MM_EVENT_IMSI_DETACH,	"MM_EVENT_IMSI_DETACH" },
	{ GSM48_MM_EVENT_POWER_OFF,	"MM_EVENT_POWER_OFF" },
	{ GSM48_MM_EVENT_PAGING,	"MM_EVENT_PAGING" },
	{ GSM48_MM_EVENT_AUTH_RESPONSE,	"MM_EVENT_AUTH_RESPONSE" },
	{ GSM48_MM_EVENT_SYSINFO,	"MM_EVENT_SYSINFO" },
	{ GSM48_MM_EVENT_USER_PLMN_SEL,	"MM_EVENT_USER_PLMN_SEL" },
	{ GSM48_MM_EVENT_LOST_COVERAGE,	"MM_EVENT_LOST_COVERAGE" },
	{ 0,				NULL }
};

const char *get_mmevent_name(int value)
{
	return get_value_string(gsm48_mmevent_names, value);
}

/* names of MM-SAP */
static const struct value_string gsm48_mm_msg_names[] = {
	{ GSM48_MT_MM_IMSI_DETACH_IND,	"MT_MM_IMSI_DETACH_IND" },
	{ GSM48_MT_MM_LOC_UPD_ACCEPT,	"MT_MM_LOC_UPD_ACCEPT" },
	{ GSM48_MT_MM_LOC_UPD_REJECT,	"MT_MM_LOC_UPD_REJECT" },
	{ GSM48_MT_MM_LOC_UPD_REQUEST,	"MT_MM_LOC_UPD_REQUEST" },
	{ GSM48_MT_MM_AUTH_REJ,		"MT_MM_AUTH_REJ" },
	{ GSM48_MT_MM_AUTH_REQ,		"MT_MM_AUTH_REQ" },
	{ GSM48_MT_MM_AUTH_RESP,	"MT_MM_AUTH_RESP" },
	{ GSM48_MT_MM_ID_REQ,		"MT_MM_ID_REQ" },
	{ GSM48_MT_MM_ID_RESP,		"MT_MM_ID_RESP" },
	{ GSM48_MT_MM_TMSI_REALL_CMD,	"MT_MM_TMSI_REALL_CMD" },
	{ GSM48_MT_MM_TMSI_REALL_COMPL,	"MT_MM_TMSI_REALL_COMPL" },
	{ GSM48_MT_MM_CM_SERV_ACC,	"MT_MM_CM_SERV_ACC" },
	{ GSM48_MT_MM_CM_SERV_REJ,	"MT_MM_CM_SERV_REJ" },
	{ GSM48_MT_MM_CM_SERV_ABORT,	"MT_MM_CM_SERV_ABORT" },
	{ GSM48_MT_MM_CM_SERV_REQ,	"MT_MM_CM_SERV_REQ" },
	{ GSM48_MT_MM_CM_SERV_PROMPT,	"MT_MM_CM_SERV_PROMPT" },
	{ GSM48_MT_MM_CM_REEST_REQ,	"MT_MM_CM_REEST_REQ" },
	{ GSM48_MT_MM_ABORT,		"MT_MM_ABORT" },
	{ GSM48_MT_MM_NULL,		"MT_MM_NULL" },
	{ GSM48_MT_MM_STATUS,		"MT_MM_STATUS" },
	{ GSM48_MT_MM_INFO,		"MT_MM_INFO" },
	{ 0,				NULL }
};

const char *get_mm_name(int value)
{
	return get_value_string(gsm48_mm_msg_names, value);
}

/* names of MMxx-SAP */
static const struct value_string gsm48_mmxx_msg_names[] = {
	{ GSM48_MMCC_EST_REQ,		"MMCC_EST_REQ" },
	{ GSM48_MMCC_EST_IND,		"MMCC_EST_IND" },
	{ GSM48_MMCC_EST_CNF,		"MMCC_EST_CNF" },
	{ GSM48_MMCC_REL_REQ,		"MMCC_REL_REQ" },
	{ GSM48_MMCC_REL_IND,		"MMCC_REL_IND" },
	{ GSM48_MMCC_DATA_REQ,		"MMCC_DATA_REQ" },
	{ GSM48_MMCC_DATA_IND,		"MMCC_DATA_IND" },
	{ GSM48_MMCC_UNIT_DATA_REQ,	"MMCC_UNIT_DATA_REQ" },
	{ GSM48_MMCC_UNIT_DATA_IND,	"MMCC_UNIT_DATA_IND" },
	{ GSM48_MMCC_SYNC_IND,		"MMCC_SYNC_IND" },
	{ GSM48_MMCC_REEST_REQ,		"MMCC_REEST_REQ" },
	{ GSM48_MMCC_REEST_CNF,		"MMCC_REEST_CNF" },
	{ GSM48_MMCC_ERR_IND,		"MMCC_ERR_IND" },
	{ GSM48_MMCC_PROMPT_IND,	"MMCC_PROMPT_IND" },
	{ GSM48_MMCC_PROMPT_REJ,	"MMCC_PROMPT_REJ" },
	{ GSM48_MMSS_EST_REQ,		"MMSS_EST_REQ" },
	{ GSM48_MMSS_EST_IND,		"MMSS_EST_IND" },
	{ GSM48_MMSS_EST_CNF,		"MMSS_EST_CNF" },
	{ GSM48_MMSS_REL_REQ,		"MMSS_REL_REQ" },
	{ GSM48_MMSS_REL_IND,		"MMSS_REL_IND" },
	{ GSM48_MMSS_DATA_REQ,		"MMSS_DATA_REQ" },
	{ GSM48_MMSS_DATA_IND,		"MMSS_DATA_IND" },
	{ GSM48_MMSS_UNIT_DATA_REQ,	"MMSS_UNIT_DATA_REQ" },
	{ GSM48_MMSS_UNIT_DATA_IND,	"MMSS_UNIT_DATA_IND" },
	{ GSM48_MMSS_REEST_REQ,		"MMSS_REEST_REQ" },
	{ GSM48_MMSS_REEST_CNF,		"MMSS_REEST_CNF" },
	{ GSM48_MMSS_ERR_IND,		"MMSS_ERR_IND" },
	{ GSM48_MMSS_PROMPT_IND,	"MMSS_PROMPT_IND" },
	{ GSM48_MMSS_PROMPT_REJ,	"MMSS_PROMPT_REJ" },
	{ GSM48_MMSMS_EST_REQ,		"MMSMS_EST_REQ" },
	{ GSM48_MMSMS_EST_IND,		"MMSMS_EST_IND" },
	{ GSM48_MMSMS_EST_CNF,		"MMSMS_EST_CNF" },
	{ GSM48_MMSMS_REL_REQ,		"MMSMS_REL_REQ" },
	{ GSM48_MMSMS_REL_IND,		"MMSMS_REL_IND" },
	{ GSM48_MMSMS_DATA_REQ,		"MMSMS_DATA_REQ" },
	{ GSM48_MMSMS_DATA_IND,		"MMSMS_DATA_IND" },
	{ GSM48_MMSMS_UNIT_DATA_REQ,	"MMSMS_UNIT_DATA_REQ" },
	{ GSM48_MMSMS_UNIT_DATA_IND,	"MMSMS_UNIT_DATA_IND" },
	{ GSM48_MMSMS_REEST_REQ,	"MMSMS_REEST_REQ" },
	{ GSM48_MMSMS_REEST_CNF,	"MMSMS_REEST_CNF" },
	{ GSM48_MMSMS_ERR_IND,		"MMSMS_ERR_IND" },
	{ GSM48_MMSMS_PROMPT_IND,	"MMSMS_PROMPT_IND" },
	{ GSM48_MMSMS_PROMPT_REJ,	"MMSMS_PROMPT_REJ" },
	{ 0,				NULL }
};

const char *get_mmxx_name(int value)
{
	return get_value_string(gsm48_mmxx_msg_names, value);
}

/* names of MMR-SAP */
static const struct value_string gsm48_mmr_msg_names[] = {
	{ GSM48_MMR_REG_REQ,		"MMR_REG_REQ" },
	{ GSM48_MMR_REG_CNF,		"MMR_REG_CNF" },
	{ GSM48_MMR_NREG_REQ,		"MMR_NREG_REQ" },
	{ GSM48_MMR_NREG_IND,		"MMR_NREG_IND" },
	{ 0,				NULL }
};

const char *get_mmr_name(int value)
{
	return get_value_string(gsm48_mmr_msg_names, value);
}

/* allocate GSM 04.08 message (MMxx-SAP) */
struct msgb *gsm48_mmxx_msgb_alloc(int msg_type, uint32_t ref,
	uint8_t transaction_id, uint8_t sapi)
{
	struct msgb *msg;
	struct gsm48_mmxx_hdr *mmh;

	msg = msgb_alloc_headroom(MMXX_ALLOC_SIZE+MMXX_ALLOC_HEADROOM,
		MMXX_ALLOC_HEADROOM, "GSM 04.08 MMxx");
	if (!msg)
		return NULL;

	mmh = (struct gsm48_mmxx_hdr *)msgb_put(msg, sizeof(*mmh));
	mmh->msg_type = msg_type;
	mmh->ref = ref;
	mmh->transaction_id = transaction_id;
	mmh->sapi = sapi;

	return msg;
}

/* allocate MM event message */
struct msgb *gsm48_mmevent_msgb_alloc(int msg_type)
{
	struct msgb *msg;
	struct gsm48_mm_event *mme;

	msg = msgb_alloc_headroom(sizeof(*mme), 0, "GSM 04.08 MM event");
	if (!msg)
		return NULL;

	mme = (struct gsm48_mm_event *)msgb_put(msg, sizeof(*mme));
	mme->msg_type = msg_type;

	return msg;
}

/* allocate MMR message */
struct msgb *gsm48_mmr_msgb_alloc(int msg_type)
{
	struct msgb *msg;
	struct gsm48_mmr *mmr;

	msg = msgb_alloc_headroom(sizeof(*mmr), 0, "GSM 04.08 MMR");
	if (!msg)
		return NULL;

	mmr = (struct gsm48_mmr *)msgb_put(msg, sizeof(*mmr));
	mmr->msg_type = msg_type;

	return msg;
}

/* queue message (MMxx-SAP) */
int gsm48_mmxx_upmsg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	msgb_enqueue(&mm->mmxx_upqueue, msg);

	return 0;
}

/* queue message (MMR-SAP) */
int gsm48_mmr_downmsg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	msgb_enqueue(&mm->mmr_downqueue, msg);

	return 0;
}

/* queue MM event message */
int gsm48_mmevent_msg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	msgb_enqueue(&mm->event_queue, msg);

	return 0;
}

/* dequeue messages (MMxx-SAP) */
int gsm48_mmxx_dequeue(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct msgb *msg;
	struct gsm48_mmxx_hdr *mmh;
	int work = 0;

	while ((msg = msgb_dequeue(&mm->mmxx_upqueue))) {
		mmh = (struct gsm48_mmxx_hdr *) msg->data;
		switch (mmh->msg_type & GSM48_MMXX_MASK) {
		case GSM48_MMCC_CLASS:
			gsm48_rcv_cc(ms, msg);
			break;
		case GSM48_MMSS_CLASS:
			gsm480_rcv_ss(ms, msg);
			break;
		case GSM48_MMSMS_CLASS:
			gsm411_rcv_sms(ms, msg);
			break;
		}
		msgb_free(msg);
		work = 1; /* work done */
	}

	return work;
}

/* dequeue messages (MMR-SAP) */
int gsm48_mmr_dequeue(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct msgb *msg;
	struct gsm48_mmr *mmr;
	int work = 0;

	while ((msg = msgb_dequeue(&mm->mmr_downqueue))) {
		mmr = (struct gsm48_mmr *) msg->data;
		gsm48_rcv_mmr(ms, msg);
		msgb_free(msg);
		work = 1; /* work done */
	}

	return work;
}

/* dequeue messages (RR-SAP) */
int gsm48_rr_dequeue(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct msgb *msg;
	int work = 0;

	while ((msg = msgb_dequeue(&mm->rr_upqueue))) {
		/* msg is freed there */
		gsm48_rcv_rr(ms, msg);
		work = 1; /* work done */
	}

	return work;
}

/* dequeue MM event messages */
int gsm48_mmevent_dequeue(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mm_event *mme;
	struct msgb *msg;
	int work = 0;

	while ((msg = msgb_dequeue(&mm->event_queue))) {
		mme = (struct gsm48_mm_event *) msg->data;
		gsm48_mm_ev(ms, mme->msg_type, msg);
		msgb_free(msg);
		work = 1; /* work done */
	}

	return work;
}

/* push RR header and send to RR */
static int gsm48_mm_to_rr(struct osmocom_ms *ms, struct msgb *msg, int msg_type,
	uint8_t sapi, uint8_t cause)
{
	struct gsm48_rr_hdr *rrh;

	/* push RR header */
	msgb_push(msg, sizeof(struct gsm48_rr_hdr));
	rrh = (struct gsm48_rr_hdr *) msg->data;
	rrh->msg_type = msg_type;
	rrh->sapi = sapi;
	rrh->cause = cause;

	/* send message to RR */
	return gsm48_rr_downmsg(ms, msg);
}

/*
 * state transition
 */

const char *gsm48_mm_state_names[] = {
	"NULL",
	"undefined 1",
	"undefined 2",
	"location updating initiated",
	"undefined 4",
	"wait for outgoing MM connection",
	"MM connection active",
	"IMSI detach initiated",
	"process CM service prompt",
	"wait for network command",
	"location updating reject",
	"undefined 11",
	"undefined 12",
	"wait for RR connection (location updating)",
	"wait for RR connection (MM connection)",
	"wait for RR connection (IMSI detach)",
	"undefined 16",
	"wait for re-establishment",
	"wait for RR connection active",
	"MM idle",
	"wait for additional outgoing MM connection",
	"MM_CONN_ACTIVE_VGCS",
	"WAIT_RR_CONN_VGCS",
	"location updating pending",
	"IMSI detach pending",
	"RR connection release not allowed"
};

const char *gsm48_mm_substate_names[] = {
	"NULL",
	"normal service",
	"attempting to update",
	"limited service",
	"no IMSI",
	"no cell available",
	"location updating needed",
	"PLMN search",
	"PLMN search (normal)",
	"RX_VGCS_NORMAL",
	"RX_VGCS_LIMITED"
};

/* change state from LOCATION UPDATE NEEDED to ATTEMPTING TO UPDATE */
static int gsm48_mm_loc_upd_possible(struct gsm48_mmlayer *mm)
{
	// TODO: check if really possible
	new_mm_state(mm, GSM48_MM_ST_MM_IDLE, GSM48_MM_SST_ATTEMPT_UPDATE);
	return gsm48_mm_loc_upd_normal(mm->ms, NULL);
}

/* Set new MM state, also new substate in case of MM IDLE state. */
static void new_mm_state(struct gsm48_mmlayer *mm, int state, int substate)
{
	struct osmocom_ms *ms = mm->ms;
	struct gsm322_plmn *plmn = &ms->plmn;

	/* IDLE -> IDLE */
	if (mm->state == GSM48_MM_ST_MM_IDLE && state == mm->state)
		LOGP(DMM, LOGL_INFO, "new MM IDLE state %s -> %s\n",
			gsm48_mm_substate_names[mm->substate],
			gsm48_mm_substate_names[substate]);
	/* IDLE -> non-IDLE */
	else if (mm->state == GSM48_MM_ST_MM_IDLE)
		LOGP(DMM, LOGL_INFO, "new state MM IDLE, %s -> %s\n",
			gsm48_mm_substate_names[mm->substate],
			gsm48_mm_state_names[state]);
	/* non-IDLE -> IDLE */
	else if (state == GSM48_MM_ST_MM_IDLE)
		LOGP(DMM, LOGL_INFO, "new state %s -> MM IDLE, %s\n",
			gsm48_mm_state_names[mm->state],
			gsm48_mm_substate_names[substate]);
	/* non-IDLE -> non-IDLE */
	else
		LOGP(DMM, LOGL_INFO, "new state %s -> %s\n",
			gsm48_mm_state_names[mm->state],
			gsm48_mm_state_names[state]);

	/* display service on new IDLE state */
	if (state == GSM48_MM_ST_MM_IDLE
	 && (mm->state != GSM48_MM_ST_MM_IDLE || mm->substate != substate)) {
		switch (substate) {
		case GSM48_MM_SST_NORMAL_SERVICE:
			vty_notify(ms, NULL);
			vty_notify(ms, "On Network, normal service: %s, %s\n",
				gsm_get_mcc(plmn->mcc),
				gsm_get_mnc(plmn->mcc, plmn->mnc));
			break;
		case GSM48_MM_SST_LIMITED_SERVICE:
			vty_notify(ms, NULL);
			vty_notify(ms, "Limited service, emergency calls are "
				"possible.\n");
			break;
		case GSM48_MM_SST_PLMN_SEARCH_NORMAL:
		case GSM48_MM_SST_PLMN_SEARCH:
			vty_notify(ms, NULL);
			vty_notify(ms, "Searching network...\n");
			break;
		case GSM48_MM_SST_NO_IMSI:
			vty_notify(ms, NULL);
			vty_notify(ms, "No SIM, emergency calls are "
				"possible.\n");
			break;
		case GSM48_MM_SST_NO_CELL_AVAIL:
			vty_notify(ms, NULL);
			vty_notify(ms, "No service.\n");
			break;
		case GSM48_MM_SST_ATTEMPT_UPDATE:
			vty_notify(ms, NULL);
			vty_notify(ms, "Trying to registering with "
				"network...\n");
			break;
		}
	}

	/* remember most recent substate */
	if (mm->state == GSM48_MM_ST_MM_IDLE)
		mm->mr_substate = mm->substate;

	mm->state = state;
	mm->substate = substate;

	/* resend detach event, if flag is set */
	if (state == GSM48_MM_ST_MM_IDLE && mm->delay_detach) {
		struct msgb *nmsg;

		mm->delay_detach = 0;

		nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_IMSI_DETACH);
		if (!nmsg)
			return;
		gsm48_mmevent_msg(ms, nmsg);
	}

	/* 4.4.2 start T3212 in MM IDLE mode if not started or has expired */
	if (state == GSM48_MM_ST_MM_IDLE
	 && (substate == GSM48_MM_SST_NORMAL_SERVICE
	  || substate == GSM48_MM_SST_ATTEMPT_UPDATE)) {
		struct gsm48_sysinfo *s = &mm->ms->cellsel.sel_si;

	  	/* start periodic location update timer */
		if (s->t3212 && !osmo_timer_pending(&mm->t3212)) {
			mm->t3212_value = s->t3212;
			start_mm_t3212(mm, mm->t3212_value);
		}
		/* perform pending location update */
		if (mm->lupd_retry) {
			LOGP(DMM, LOGL_INFO, "Loc. upd. pending (type %d)\n",
				mm->lupd_type);
			mm->lupd_retry = 0;
			gsm48_mm_loc_upd(ms, NULL);
			/* must exit, because this function can be called
			 * recursively
			 */
			return;
		}
		if (mm->lupd_periodic) {
			LOGP(DMM, LOGL_INFO, "Periodic loc. upd. pending "
				"(type %d)\n", mm->lupd_type);
			mm->lupd_periodic = 0;
			if (s->t3212) /* still required? */
				gsm48_mm_loc_upd_periodic(ms, NULL);
			else
				LOGP(DMM, LOGL_INFO, "but not requred\n");
			/* must exit, because this function can be called
			 * recursively
			 */
			return;
		}
	}

	/* check if location update is possible */
	if (state == GSM48_MM_ST_MM_IDLE
	 && substate == GSM48_MM_SST_LOC_UPD_NEEDED) {
		gsm48_mm_loc_upd_possible(mm);
		/* must exit, because this function can be called recursively */
		return;
	}
}

/* return PLMN SEARCH or PLMN SEARCH NORMAL state */
static int gsm48_mm_set_plmn_search(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm322_cellsel *cs = &ms->cellsel;

	/* SIM not inserted */
	if (!subscr->sim_valid) {
		LOGP(DMM, LOGL_INFO, "Selecting PLMN SEARCH state, because "
			"no SIM.\n");
		return GSM48_MM_SST_PLMN_SEARCH;
	}

	/* SIM not updated */
	if (subscr->ustate != GSM_SIM_U1_UPDATED) {
		LOGP(DMM, LOGL_INFO, "Selecting PLMN SEARCH state, because "
			"SIM not updated.\n");
		return GSM48_MM_SST_PLMN_SEARCH;
	}
	if (subscr->lac == 0x0000 || subscr->lac >= 0xfffe) {
		LOGP(DMM, LOGL_INFO, "Selecting PLMN SEARCH state, because "
			"LAI in SIM not valid.\n");
		return GSM48_MM_SST_PLMN_SEARCH;
	}

	/* no cell selected */
	if (!cs->selected) {
		LOGP(DMM, LOGL_INFO, "Selecting PLMN SEARCH state, because "
			"no cell selected.\n");
		return GSM48_MM_SST_PLMN_SEARCH;
	}

	/* selected cell's LAI not equal to LAI stored on the sim */
	if (cs->sel_mcc != subscr->mcc
	 || cs->sel_mnc != subscr->mnc
	 || cs->sel_lac != subscr->lac) {
		LOGP(DMM, LOGL_INFO, "Selecting PLMN SEARCH state, because "
			"LAI of selected cell (MCC %s MNC %s LAC 0x%04x) "
			"!= LAI in SIM (MCC %s MNC %s LAC 0x%04x).\n",
			gsm_print_mcc(cs->sel_mcc), gsm_print_mnc(cs->sel_mnc),
			cs->sel_lac, gsm_print_mcc(subscr->mcc),
			gsm_print_mnc(subscr->mnc), subscr->lac);
		return GSM48_MM_SST_PLMN_SEARCH;
	}

	/* SIM is updated in this LA */
	LOGP(DMM, LOGL_INFO, "Selecting PLMN SEARCH NORMAL state.\n");
	return GSM48_MM_SST_PLMN_SEARCH_NORMAL;
}

/* 4.2.3 when returning to MM IDLE state, this function is called */
static int gsm48_mm_return_idle(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = &cs->sel_si;

	if (cs->state != GSM322_C3_CAMPED_NORMALLY
	 && cs->state != GSM322_C7_CAMPED_ANY_CELL) {
		LOGP(DMM, LOGL_INFO, "Not camping, wait for CS process to "
			"camp, it sends us CELL_SELECTED then.\n");
		return 0;
	}

	/* 4.4.4.9 start T3211 when RR is released */
	if (mm->start_t3211) {
		LOGP(DMM, LOGL_INFO, "Starting T3211 after RR release.\n");
		mm->start_t3211 = 0;
		start_mm_t3211(mm);
	}

	/* return from location update with "Roaming not allowed" */
	if (mm->state == GSM48_MM_ST_LOC_UPD_REJ && mm->lupd_rej_cause == 13) {
		LOGP(DMM, LOGL_INFO, "Roaming not allowed as returning to "
			"MM IDLE\n");
		new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
			gsm48_mm_set_plmn_search(ms));

		return 0;
	}

	/* no SIM present or invalid */
	if (!subscr->sim_valid) {
		LOGP(DMM, LOGL_INFO, "SIM invalid as returning to MM IDLE\n");

		/* stop periodic location updating */
		mm->lupd_pending = 0;
		stop_mm_t3212(mm); /* 4.4.2 */

		new_mm_state(mm, GSM48_MM_ST_MM_IDLE, GSM48_MM_SST_NO_IMSI);

		return 0;
	}

	/* if we are attached and selected cell equals the registered LAI */
	if (subscr->imsi_attached
	 && subscr->lac /* valid */
	 && cs->sel_mcc == subscr->mcc
	 && cs->sel_mnc == subscr->mnc
	 && cs->sel_lac == subscr->lac) {
		LOGP(DMM, LOGL_INFO, "We are in registered LAI as returning "
			"to MM IDLE\n");
		/* if SIM not updated (abnormal case as described in 4.4.4.9) */
		if (subscr->ustate != GSM_SIM_U1_UPDATED)
			new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
				GSM48_MM_SST_ATTEMPT_UPDATE);
		else
			new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
				GSM48_MM_SST_NORMAL_SERVICE);

		return 0;
	}

	if (cs->state == GSM322_C3_CAMPED_NORMALLY) {
		LOGP(DMM, LOGL_INFO, "We are camping normally as returning to "
			"MM IDLE\n");
		if (gsm_subscr_is_forbidden_plmn(subscr, cs->sel_mcc,
				cs->sel_mnc)) {
			/* location update not allowed */
			LOGP(DMM, LOGL_INFO, "Loc. upd. not allowed PLMN.\n");
			new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
				GSM48_MM_SST_LIMITED_SERVICE);
		} else
		if (gsm322_is_forbidden_la(ms, cs->sel_mcc, cs->sel_mnc,
				cs->sel_lac)) {
			/* location update not allowed */
			LOGP(DMM, LOGL_INFO, "Loc. upd. not allowed LA.\n");
			new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
				GSM48_MM_SST_LIMITED_SERVICE);
		} else
		/* 4.4.4.9 if cell is barred, don't start */
		if ((!subscr->acc_barr && s->cell_barr)
		 || (!subscr->acc_barr && !((subscr->acc_class & 0xfbff) &
					(s->class_barr ^ 0xffff)))) {
			LOGP(DMM, LOGL_INFO, "Loc. upd. no access.\n");
			new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
				GSM48_MM_SST_LIMITED_SERVICE);
		} else {
			/* location update allowed */
			LOGP(DMM, LOGL_INFO, "Loc. upd. allowed.\n");
			new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
				GSM48_MM_SST_LOC_UPD_NEEDED);
		}
	} else {
		/* location update not allowed */
		LOGP(DMM, LOGL_INFO, "We are camping on any cell as returning "
			"to MM IDLE\n");
		new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
			GSM48_MM_SST_LIMITED_SERVICE);
	}

	return 0;
}

/* 4.2.1.1 Service state PLMN SEARCH (NORMAL) is left if no cell found */
static int gsm48_mm_no_cell_found(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	new_mm_state(mm, GSM48_MM_ST_MM_IDLE, GSM48_MM_SST_NO_CELL_AVAIL);

	return 0;
}

/* 4.2.1.1 Service state PLMN SEARCH (NORMAL) / NO CELL AVAILABLE is left
 * if cell selected
 */
static int gsm48_mm_cell_selected(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = &cs->sel_si;
	struct gsm_settings *set = &ms->settings;

	/* no SIM is inserted */
	if (!subscr->sim_valid) {
		LOGP(DMM, LOGL_INFO, "SIM invalid as cell is selected.\n");
		new_mm_state(mm, GSM48_MM_ST_MM_IDLE, GSM48_MM_SST_NO_IMSI);

		return 0;
	}

	/* SIM not updated in this LA */
	if (subscr->ustate == GSM_SIM_U1_UPDATED
	 && subscr->lac /* valid */
	 && cs->sel_mcc == subscr->mcc
	 && cs->sel_mnc == subscr->mnc
	 && cs->sel_lac == subscr->lac
	 && !mm->lupd_periodic) {
		if (subscr->imsi_attached) {
			struct msgb *nmsg;

			LOGP(DMM, LOGL_INFO, "Valid in location area.\n");
			new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
				GSM48_MM_SST_NORMAL_SERVICE);

			/* send message to PLMN search process */
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_REG_SUCCESS);
			if (!nmsg)
				return -ENOMEM;
			gsm322_plmn_sendmsg(ms, nmsg);

			return 0;
		}
		if (!s->att_allowed) {
			struct msgb *nmsg;

			LOGP(DMM, LOGL_INFO, "Attachment not required.\n");
			new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
				GSM48_MM_SST_NORMAL_SERVICE);

			/* send message to PLMN search process */
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_REG_SUCCESS);
			if (!nmsg)
				return -ENOMEM;
			gsm322_plmn_sendmsg(ms, nmsg);

			return 0;
		}
		/* else, continue */
	}

	/* PLMN mode auto and selected cell is forbidden */
	if (set->plmn_mode == PLMN_MODE_AUTO
	 && (!cs->selected
	  || gsm_subscr_is_forbidden_plmn(subscr, cs->sel_mcc, cs->sel_mnc)
	  || gsm322_is_forbidden_la(ms, cs->sel_mcc, cs->sel_mnc,
	 	cs->sel_lac))) {
		struct msgb *nmsg;

		LOGP(DMM, LOGL_INFO, "Selected cell is forbidden.\n");
		new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
			GSM48_MM_SST_LIMITED_SERVICE);

		/* send message to PLMN search process */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_REG_FAILED);
		if (!nmsg)
			return -ENOMEM;
		gsm322_plmn_sendmsg(ms, nmsg);

		return 0;
	}

	/* PLMN mode manual and selected cell not selected PLMN.
	 * in M3 state the PLMN is not selected for registration. */
	if (set->plmn_mode == PLMN_MODE_MANUAL
	 && (!cs->selected
	  || plmn->mcc != cs->sel_mcc
	  || plmn->mnc != cs->sel_mnc
	  || plmn->state == GSM322_M3_NOT_ON_PLMN)) {
		struct msgb *nmsg;

		LOGP(DMM, LOGL_INFO, "Selected cell not found.\n");
		new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
			GSM48_MM_SST_LIMITED_SERVICE);

		/* send message to PLMN search process */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_REG_FAILED);
		if (!nmsg)
			return -ENOMEM;
		gsm322_plmn_sendmsg(ms, nmsg);

		return 0;
	}

	/* other cases */
	new_mm_state(mm, GSM48_MM_ST_MM_IDLE, GSM48_MM_SST_LOC_UPD_NEEDED);

	return 0;
}

/* 4.2.1.2 Service state PLMN SEARCH (NORMAL) is entered */
static int gsm48_mm_plmn_search(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	new_mm_state(mm, GSM48_MM_ST_MM_IDLE, gsm48_mm_set_plmn_search(ms));

	return 0;
}

/*
 * init and exit
 */

/* initialize Mobility Management process */
int gsm48_mm_init(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	memset(mm, 0, sizeof(*mm));
	mm->ms = ms;

	LOGP(DMM, LOGL_INFO, "init Mobility Management process\n");

	/* 4.2.1.1 */
	mm->state = GSM48_MM_ST_MM_IDLE;
	mm->substate = gsm48_mm_set_plmn_search(ms);

	/* init lists */
	INIT_LLIST_HEAD(&mm->mm_conn);
	INIT_LLIST_HEAD(&mm->rr_upqueue);
	INIT_LLIST_HEAD(&mm->mmxx_upqueue);
	INIT_LLIST_HEAD(&mm->mmr_downqueue);
	INIT_LLIST_HEAD(&mm->event_queue);

	return 0;
}

/* exit MM process */
int gsm48_mm_exit(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mm_conn *conn;
	struct msgb *msg;

	LOGP(DMM, LOGL_INFO, "exit Mobility Management process\n");

	/* flush lists */
	while (!llist_empty(&mm->mm_conn)) {
		conn = llist_entry(mm->mm_conn.next,
			struct gsm48_mm_conn, list);
		mm_conn_free(conn);
	}
	while ((msg = msgb_dequeue(&mm->rr_upqueue)))
		msgb_free(msg);
	while ((msg = msgb_dequeue(&mm->mmxx_upqueue)))
		msgb_free(msg);
	while ((msg = msgb_dequeue(&mm->mmr_downqueue)))
		msgb_free(msg);
	while ((msg = msgb_dequeue(&mm->event_queue)))
		msgb_free(msg);

	/* stop timers */
	stop_mm_t3210(mm);
	stop_mm_t3211(mm);
	stop_mm_t3212(mm);
	stop_mm_t3213(mm);
	stop_mm_t3220(mm);
	stop_mm_t3230(mm);
	stop_mm_t3240(mm);

	return 0;
}

/*
 * MM connection management
 */

static const char *gsm48_mmxx_state_names[] = {
	"IDLE",
	"CONN_PEND",
	"DEDICATED",
	"CONN_SUSP",
	"REESTPEND"
};

uint32_t mm_conn_new_ref = 0x80000001;

/* new MM connection state */
static void new_conn_state(struct gsm48_mm_conn *conn, int state)
{
	LOGP(DMM, LOGL_INFO, "(ref %x) new state %s -> %s\n", conn->ref,
		gsm48_mmxx_state_names[conn->state],
		gsm48_mmxx_state_names[state]);
	conn->state = state;
}

/* find MM connection by protocol+ID */
struct gsm48_mm_conn *mm_conn_by_id(struct gsm48_mmlayer *mm,
				   uint8_t proto, uint8_t transaction_id)
{
	struct gsm48_mm_conn *conn;

	llist_for_each_entry(conn, &mm->mm_conn, list) {
		if (conn->protocol == proto &&
		    conn->transaction_id == transaction_id)
			return conn;
	}
	return NULL;
}

/* find MM connection by reference */
struct gsm48_mm_conn *mm_conn_by_ref(struct gsm48_mmlayer *mm,
					uint32_t ref)
{
	struct gsm48_mm_conn *conn;

	llist_for_each_entry(conn, &mm->mm_conn, list) {
		if (conn->ref == ref)
			return conn;
	}
	return NULL;
}

/* create MM connection instance */
static struct gsm48_mm_conn* mm_conn_new(struct gsm48_mmlayer *mm,
	int proto, uint8_t transaction_id, uint8_t sapi, uint32_t ref)
{
	struct gsm48_mm_conn *conn = talloc_zero(l23_ctx, struct gsm48_mm_conn);

	if (!conn)
		return NULL;

	LOGP(DMM, LOGL_INFO, "New MM Connection (proto 0x%02x trans_id %d "
		"sapi %d ref %x)\n", proto, transaction_id, sapi, ref);

	conn->mm = mm;
	conn->state = GSM48_MMXX_ST_IDLE;
	conn->transaction_id = transaction_id;
	conn->protocol = proto;
	conn->sapi = sapi;
	conn->ref = ref;

	llist_add(&conn->list, &mm->mm_conn);

	return conn;
}

/* destroy MM connection instance */
void mm_conn_free(struct gsm48_mm_conn *conn)
{
	LOGP(DMM, LOGL_INFO, "Freeing MM Connection\n");

	new_conn_state(conn, GSM48_MMXX_ST_IDLE);

	llist_del(&conn->list);

	talloc_free(conn);
}

/* support function to release pending/all ongoing MM connections */
static int gsm48_mm_release_mm_conn(struct osmocom_ms *ms, int abort_any,
				    uint8_t cause, int error, uint8_t sapi)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mm_conn *conn, *conn2;
	struct msgb *nmsg;
	struct gsm48_mmxx_hdr *nmmh;

	/* Note: For SAPI 0 all connections are released */

	if (abort_any)
		LOGP(DMM, LOGL_INFO, "Release any MM Connection "
			"(sapi = %d)\n", sapi);
	else
		LOGP(DMM, LOGL_INFO, "Release pending MM Connections "
			"(sapi = %d)\n", sapi);

	/* release MM connection(s) */
	llist_for_each_entry_safe(conn, conn2, &mm->mm_conn, list) {
		/* abort any OR the pending connection */
		if ((abort_any || conn->state == GSM48_MMXX_ST_CONN_PEND)
		 && (sapi == conn->sapi || sapi == 0)) {
			/* send MMxx-REL-IND */
			nmsg = NULL;
			switch(conn->protocol) {
			case GSM48_PDISC_CC:
				nmsg = gsm48_mmxx_msgb_alloc(
					error ? GSM48_MMCC_ERR_IND
					: GSM48_MMCC_REL_IND, conn->ref,
						conn->transaction_id,
						conn->sapi);
				break;
			case GSM48_PDISC_NC_SS:
				nmsg = gsm48_mmxx_msgb_alloc(
					error ? GSM48_MMSS_ERR_IND
					: GSM48_MMSS_REL_IND, conn->ref,
						conn->transaction_id,
						conn->sapi);
				break;
			case GSM48_PDISC_SMS:
				nmsg = gsm48_mmxx_msgb_alloc(
					error ? GSM48_MMSMS_ERR_IND
					: GSM48_MMSMS_REL_IND, conn->ref,
						conn->transaction_id,
						conn->sapi);
				break;
			}
			if (!nmsg) {
				/* this should not happen */
				mm_conn_free(conn);
				continue; /* skip if not of CC type */
			}
			nmmh = (struct gsm48_mmxx_hdr *)nmsg->data;
			nmmh->cause = cause;
			gsm48_mmxx_upmsg(ms, nmsg);

			mm_conn_free(conn);
		}
	}
	return 0;
}

/*
 * process handlers (Common procedures)
 */

/* sending MM STATUS message */
static int gsm48_mm_tx_mm_status(struct osmocom_ms *ms, uint8_t cause)
{
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	uint8_t *reject_cause;

	LOGP(DMM, LOGL_INFO, "MM STATUS (cause #%d)\n", cause);

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));
	reject_cause = msgb_put(nmsg, 1);

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_STATUS;
	*reject_cause = cause;

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, GSM48_RR_DATA_REQ, 0, 0);
}

/* 4.3.1.2 sending TMSI REALLOCATION COMPLETE message */
static int gsm48_mm_tx_tmsi_reall_cpl(struct osmocom_ms *ms)
{
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;

	LOGP(DMM, LOGL_INFO, "TMSI REALLOCATION COMPLETE\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_TMSI_REALL_COMPL;

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, GSM48_RR_DATA_REQ, 0, 0);
}

/* 4.3.1 TMSI REALLOCATION COMMAND is received */
static int gsm48_mm_rx_tmsi_realloc_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm48_loc_area_id *lai = (struct gsm48_loc_area_id *) gh->data;
	uint8_t mi_type, *mi;
	uint32_t tmsi;

	if (payload_len < sizeof(struct gsm48_loc_area_id) + 2) {
		short_read:
		LOGP(DMM, LOGL_NOTICE, "Short read of TMSI REALLOCATION "
			"COMMAND message error.\n");
		return -EINVAL;
	}
	/* LAI */
	gsm48_decode_lai_hex(lai, &subscr->mcc, &subscr->mnc, &subscr->lac);
	/* MI */
	mi = gh->data + sizeof(struct gsm48_loc_area_id);
	mi_type = mi[1] & GSM_MI_TYPE_MASK;
	switch (mi_type) {
	case GSM_MI_TYPE_TMSI:
		if (payload_len + sizeof(struct gsm48_loc_area_id) < 6
		 || mi[0] < 5)
			goto short_read;
		memcpy(&tmsi, mi+2, 4);
		subscr->tmsi = ntohl(tmsi);
		LOGP(DMM, LOGL_INFO, "TMSI 0x%08x (%u) assigned.\n",
			subscr->tmsi, subscr->tmsi);
		gsm48_mm_tx_tmsi_reall_cpl(ms);
		break;
	case GSM_MI_TYPE_IMSI:
		subscr->tmsi = 0xffffffff;
		LOGP(DMM, LOGL_INFO, "TMSI removed.\n");
		gsm48_mm_tx_tmsi_reall_cpl(ms);
		break;
	default:
		subscr->tmsi = 0xffffffff;
		LOGP(DMM, LOGL_NOTICE, "TMSI reallocation with unknown MI "
			"type %d.\n", mi_type);
		gsm48_mm_tx_mm_status(ms, GSM48_REJECT_INCORRECT_MESSAGE);
	}

	/* store LOCI on sim */
	gsm_subscr_write_loci(ms);

	return 0;
}

/* 4.3.2.2 AUTHENTICATION REQUEST is received */
static int gsm48_mm_rx_auth_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm_settings *set = &ms->settings;
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm48_auth_req *ar = (struct gsm48_auth_req *) gh->data;
	uint8_t no_sim = 0;

	if (payload_len < sizeof(struct gsm48_auth_req)) {
		LOGP(DMM, LOGL_NOTICE, "Short read of AUTHENTICATION REQUEST "
			"message error.\n");
		return -EINVAL;
	}

	/* SIM is not available */
	if (!subscr->sim_valid) {
		LOGP(DMM, LOGL_INFO, "AUTHENTICATION REQUEST without SIM\n");
		return gsm48_mm_tx_mm_status(ms,
			GSM48_REJECT_MSG_NOT_COMPATIBLE);
	}

	LOGP(DMM, LOGL_INFO, "AUTHENTICATION REQUEST (seq %d)\n", ar->key_seq);

	/* key_seq and random
	 * in case of test card, there is a dummy response.
	 * authentication request is possible during emergency call, if
	 * IMSI is known to the network. in case of emergency IMSI, we need to
	 * send a dummy response also.
	 */
	if (mm->est_cause == RR_EST_CAUSE_EMERGENCY && set->emergency_imsi[0])
		no_sim = 1;
	gsm_subscr_generate_kc(ms, ar->key_seq, ar->rand, no_sim);

	/* wait for auth response event from SIM */
	return 0;
}

/* 4.3.2.2 sending AUTHENTICATION RESPONSE */
static int gsm48_mm_tx_auth_rsp(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mm_event *mme = (struct gsm48_mm_event *) msg->data;
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	uint8_t *sres;

	LOGP(DMM, LOGL_INFO, "AUTHENTICATION RESPONSE\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_AUTH_RESP;

	/* SRES */
	sres = msgb_put(nmsg, 4);
	memcpy(sres, mme->sres, 4);

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, GSM48_RR_DATA_REQ, 0, 0);
}

/* 4.3.2.5 AUTHENTICATION REJECT is received */
static int gsm48_mm_rx_auth_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	LOGP(DMM, LOGL_INFO, "AUTHENTICATION REJECT\n");

	stop_mm_t3212(mm); /* 4.4.2 */

	/* SIM invalid */
	subscr->sim_valid = 0;

	/* TMSI and LAI invalid */
	subscr->tmsi = 0xffffffff;
	subscr->lac = 0x0000;

	/* key is invalid */
	subscr->key_seq = 7;

	/* update status */
	new_sim_ustate(subscr, GSM_SIM_U3_ROAMING_NA);

	/* store LOCI on sim */
	gsm_subscr_write_loci(ms);

	/* abort IMSI detach procedure */
	if (mm->state == GSM48_MM_ST_IMSI_DETACH_INIT) {
		struct msgb *nmsg;
		struct gsm48_rr_hdr *nrrh;

		/* abort RR connection */
		nmsg = gsm48_rr_msgb_alloc(GSM48_RR_ABORT_REQ);
		if (!nmsg)
			return -ENOMEM;
		nrrh = (struct gsm48_rr_hdr *) nmsg->data;
		nrrh->cause = GSM48_RR_CAUSE_NORMAL;
		gsm48_rr_downmsg(ms, nmsg);

		/* return to MM IDLE */
		return gsm48_mm_return_idle(ms, NULL);
	}

	return 0;
}

/* 4.3.3.1 IDENTITY REQUEST is received */
static int gsm48_mm_rx_id_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	uint8_t mi_type;

	if (payload_len < 1) {
		LOGP(DMM, LOGL_NOTICE, "Short read of IDENTITY REQUEST message "
			"error.\n");
		return -EINVAL;
	}

	/* id type */
	mi_type = *gh->data;
	LOGP(DMM, LOGL_INFO, "IDENTITY REQUEST (mi_type %d)\n", mi_type);

	/* check if request can be fulfilled */
	if (!subscr->sim_valid && mi_type != GSM_MI_TYPE_IMEI
	 && mi_type != GSM_MI_TYPE_IMEISV) {
		LOGP(DMM, LOGL_INFO, "IDENTITY REQUEST without SIM\n");
		return gsm48_mm_tx_mm_status(ms,
			GSM48_REJECT_MSG_NOT_COMPATIBLE);
	}
	if (mi_type == GSM_MI_TYPE_TMSI && subscr->tmsi == 0xffffffff) {
		LOGP(DMM, LOGL_INFO, "IDENTITY REQUEST of TMSI, but we have no "
			"TMSI\n");
		return gsm48_mm_tx_mm_status(ms,
			GSM48_REJECT_MSG_NOT_COMPATIBLE);
	}

	return gsm48_mm_tx_id_rsp(ms, mi_type);
}

/* send IDENTITY RESPONSE message */
static int gsm48_mm_tx_id_rsp(struct osmocom_ms *ms, uint8_t mi_type)
{
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	uint8_t buf[11];

	LOGP(DMM, LOGL_INFO, "IDENTITY RESPONSE\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_ID_RESP;

	/* MI */
	gsm48_encode_mi(buf, nmsg, ms, mi_type);

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, GSM48_RR_DATA_REQ, 0, 0);
}

/* 4.3.4.1 sending IMSI DETACH INDICATION message */
static int gsm48_mm_tx_imsi_detach(struct osmocom_ms *ms, int rr_prim)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm_support *sup = &ms->support;
	struct gsm_settings *set = &ms->settings;
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	uint8_t pwr_lev;
	uint8_t buf[11];
	struct gsm48_classmark1 cm;


	LOGP(DMM, LOGL_INFO, "IMSI DETACH INDICATION\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_IMSI_DETACH_IND;

	/* classmark 1 */
	if (rr_prim == GSM48_RR_EST_REQ)
		pwr_lev = gsm48_current_pwr_lev(set, cs->sel_arfcn);
	else
		pwr_lev = gsm48_current_pwr_lev(set, rr->cd_now.arfcn);
	gsm48_encode_classmark1(&cm, sup->rev_lev, sup->es_ind, set->a5_1,
		pwr_lev);
        msgb_v_put(nmsg, *((uint8_t *)&cm));
	/* MI */
	if (subscr->tmsi != 0xffffffff) { /* have TMSI ? */
		gsm48_encode_mi(buf, nmsg, ms, GSM_MI_TYPE_TMSI);
		LOGP(DMM, LOGL_INFO, " using TMSI 0x%08x\n", subscr->tmsi);
	} else {
		gsm48_encode_mi(buf, nmsg, ms, GSM_MI_TYPE_IMSI);
		LOGP(DMM, LOGL_INFO, " using IMSI %s\n", subscr->imsi);
	}

	/* push RR header and send down */
	mm->est_cause = RR_EST_CAUSE_OTHER_SDCCH;
	return gsm48_mm_to_rr(ms, nmsg, rr_prim, 0, mm->est_cause);
}

/* detach has ended */
static int gsm48_mm_imsi_detach_end(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;

	LOGP(DMM, LOGL_INFO, "IMSI has been detached.\n");

	/* stop IMSI detach timer (if running) */
	stop_mm_t3220(mm);

	/* SIM invalid */
	subscr->sim_valid = 0;

	/* wait for RR idle and then power off when IMSI is detached */
	if (ms->shutdown) {
		if (mm->state == GSM48_MM_ST_MM_IDLE) {
			mobile_exit(ms, 1);
			return 0;
		}
		/* power off when MM idle */
		mm->power_off_idle = 1;

		/* return to MM IDLE */
		return gsm48_mm_return_idle(ms, NULL);
	}

	/* send SIM remove event to gsm322 */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_SIM_REMOVE);
	if (!nmsg)
		return -ENOMEM;
	gsm322_plmn_sendmsg(ms, nmsg);

	/* return to MM IDLE */
	return gsm48_mm_return_idle(ms, NULL);
}

/* abort radio connection */
static int gsm48_mm_imsi_detach_abort(struct osmocom_ms *ms, struct msgb *msg)
{
	struct msgb *nmsg;
	struct gsm48_rr_hdr *nrrh;

	/* abort RR if timer fired */
	nmsg = gsm48_rr_msgb_alloc(GSM48_RR_ABORT_REQ);
	if (!nmsg)
		return -ENOMEM;
	nrrh = (struct gsm48_rr_hdr *) nmsg->data;
	nrrh->cause = GSM48_RR_CAUSE_NORMAL;
	gsm48_rr_downmsg(ms, nmsg);

	/* imsi detach has ended now */
	return gsm48_mm_imsi_detach_end(ms, msg);
}

/* start an IMSI detach in MM IDLE */
static int gsm48_mm_imsi_detach_start(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_sysinfo *s = &ms->cellsel.sel_si;

	/* we may silently finish IMSI detach */
	if (!s->att_allowed || !subscr->imsi_attached) {
		LOGP(DMM, LOGL_INFO, "IMSI detach not required.\n");

		return gsm48_mm_imsi_detach_end(ms, msg);
	}
	LOGP(DMM, LOGL_INFO, "IMSI detach started (MM IDLE)\n");

	new_mm_state(mm, GSM48_MM_ST_WAIT_RR_CONN_IMSI_D, 0);

	/* establish RR and send IMSI detach */
	return gsm48_mm_tx_imsi_detach(ms, GSM48_RR_EST_REQ);
}

/* IMSI detach has been sent, wait for RR release */
static int gsm48_mm_imsi_detach_sent(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* start T3220 (4.3.4.1) */
	start_mm_t3220(mm);

	LOGP(DMM, LOGL_INFO, "IMSI detach started (Wait for RR release)\n");

	new_mm_state(mm, GSM48_MM_ST_IMSI_DETACH_INIT, 0);

	return 0;
}

/* release MM connection and proceed with IMSI detach */
static int gsm48_mm_imsi_detach_release(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_sysinfo *s = &ms->cellsel.sel_si;

	/* stop MM connection timer */
	stop_mm_t3230(mm);

	/* release all connections */
	gsm48_mm_release_mm_conn(ms, 1, 16, 0, 0);

	/* wait for release of RR */
	if (!s->att_allowed || !subscr->imsi_attached) {
		LOGP(DMM, LOGL_INFO, "IMSI detach not required.\n");
		new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);

		/* power off */
		if (ms->shutdown) {
			mobile_exit(ms, 1);
			return 0;
		}

		return 0;
	}

	/* send IMSI detach */
	gsm48_mm_tx_imsi_detach(ms, GSM48_RR_DATA_REQ);

	/* go to sent state */
	return gsm48_mm_imsi_detach_sent(ms, msg);
}

/* ignore ongoing IMSI detach */
static int gsm48_mm_imsi_detach_ignore(struct osmocom_ms *ms, struct msgb *msg)
{
	return 0;
}

/* delay until state change (and then retry) */
static int gsm48_mm_imsi_detach_delay(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	LOGP(DMM, LOGL_INFO, "IMSI detach delayed.\n");

	/* remember to detach later */
	mm->delay_detach = 1;

	return 0;
}

/* 4.3.5.2 ABORT is received */
static int gsm48_mm_rx_abort(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	uint8_t reject_cause;

	if (payload_len < 1) {
		LOGP(DMM, LOGL_NOTICE, "Short read of ABORT message error.\n");
		return -EINVAL;
	}

	reject_cause = *gh->data;

	if (llist_empty(&mm->mm_conn)) {
		LOGP(DMM, LOGL_NOTICE, "ABORT (cause #%d) while no MM "
			"connection is established.\n", reject_cause);
		return gsm48_mm_tx_mm_status(ms,
			GSM48_REJECT_MSG_NOT_COMPATIBLE);
	} else {
		LOGP(DMM, LOGL_NOTICE, "ABORT (cause #%d) while MM connection "
			"is established.\n", reject_cause);
		/* stop MM connection timer */
		stop_mm_t3230(mm);

		gsm48_mm_release_mm_conn(ms, 1, 16, 0, 0);
	}

	if (reject_cause == GSM48_REJECT_ILLEGAL_ME) {
		/* SIM invalid */
		subscr->sim_valid = 0;

		/* TMSI and LAI invalid */
		subscr->tmsi = 0xffffffff;
		subscr->lac = 0x0000;

		/* key is invalid */
		subscr->key_seq = 7;

		/* update status */
		new_sim_ustate(subscr, GSM_SIM_U3_ROAMING_NA);

		/* store LOCI on sim */
		gsm_subscr_write_loci(ms);

		/* return to MM IDLE */
		return gsm48_mm_return_idle(ms, NULL);
	}

	return 0;
}

/* 4.3.6.2 MM INFORMATION is received */
static int gsm48_mm_rx_info(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;

	if (payload_len < 0) {
		LOGP(DMM, LOGL_NOTICE, "Short read of MM INFORMATION message "
			"error.\n");
		return -EINVAL;
	}
	tlv_parse(&tp, &gsm48_mm_att_tlvdef, gh->data, payload_len, 0, 0);

	/* long name */
	if (TLVP_PRESENT(&tp, GSM48_IE_NAME_LONG)) {
		decode_network_name(mm->name_long, sizeof(mm->name_long),
				TLVP_VAL(&tp, GSM48_IE_NAME_LONG)-1);
	}
	/* short name */
	if (TLVP_PRESENT(&tp, GSM48_IE_NAME_SHORT)) {
		decode_network_name(mm->name_short, sizeof(mm->name_short),
				TLVP_VAL(&tp, GSM48_IE_NAME_SHORT)-1);
	}

	return 0;
}

/*
 * process handlers for Location Update + IMSI attach (MM specific procedures)
 */

/* 4.4.2 received sysinfo change event */
static int gsm48_mm_sysinfo(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_sysinfo *s = &ms->cellsel.sel_si;

	/* t3212 not changed in these states */
	if (mm->state == GSM48_MM_ST_MM_IDLE
	 && (mm->substate == GSM48_MM_SST_NO_CELL_AVAIL
	  || mm->substate == GSM48_MM_SST_LIMITED_SERVICE
	  || mm->substate == GSM48_MM_SST_PLMN_SEARCH
	  || mm->substate == GSM48_MM_SST_PLMN_SEARCH_NORMAL))
		return 0;

	/* new periodic location update timer timeout */
	if (s->t3212 && s->t3212 != mm->t3212_value) {
		if (osmo_timer_pending(&mm->t3212)) {
			int t;
			struct timeval current_time;

			/* get rest time */
			gettimeofday(&current_time, NULL);
			t = mm->t3212.timeout.tv_sec - current_time.tv_sec;
			if (t < 0)
				t = 0;
			LOGP(DMM, LOGL_INFO, "New T3212 while timer is running "
				"(value %d rest %d)\n", s->t3212, t);

			/* rest time modulo given value */
			mm->t3212.timeout.tv_sec = current_time.tv_sec
				+ (t % s->t3212);
		} else {
			uint32_t rand = random();

			LOGP(DMM, LOGL_INFO, "New T3212 while timer is not "
				"running (value %d)\n", s->t3212);

			/* value between 0 and given value */
			start_mm_t3212(mm, rand % (s->t3212 + 1));
		}
		mm->t3212_value = s->t3212;
	}

	return 0;
}

/* 4.4.4.1 (re)start location update
 *
 * this function is called by
 * - normal location update
 * - periodic location update
 * - imsi attach (normal loc. upd. function)
 * - retry timers (T3211 and T3213)
 */
static int gsm48_mm_loc_upd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = &cs->sel_si;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm_settings *set = &ms->settings;
	struct msgb *nmsg;
	int msg_type;

	/* (re)start only if we still require location update */
	if (!mm->lupd_pending) {
		LOGP(DMM, LOGL_INFO, "No loc. upd. pending.\n");
		/* use MM IDLE to selecte the idle state */
		return gsm48_mm_return_idle(ms, NULL);
	}

	/* must camp normally */
	if (cs->state != GSM322_C3_CAMPED_NORMALLY) {
		LOGP(DMM, LOGL_INFO, "Loc. upd. not camping normally.\n");
		msg_type = GSM322_EVENT_REG_FAILED;
		stop:
		LOGP(DSUM, LOGL_INFO, "Location updating not possible\n");
		_stop:
		mm->lupd_pending = 0;

#if 0
		/* don't send message, if we got not triggered by PLMN search */
		if (!msg)
			return 0;
#endif

		/* send message to PLMN search process */
		nmsg = gsm322_msgb_alloc(msg_type);
		if (!nmsg)
			return -ENOMEM;
		gsm322_plmn_sendmsg(ms, nmsg);
		/* use MM IDLE to selecte the idle state */
		return gsm48_mm_return_idle(ms, NULL);
	}

	/* deny network, if disabled */
	if (set->no_lupd) {
		LOGP(DMM, LOGL_INFO, "Loc. upd. disabled, adding "
			"forbidden PLMN.\n");
		LOGP(DSUM, LOGL_INFO, "Location updating is disabled by "
			"configuration\n");
		gsm_subscr_add_forbidden_plmn(subscr, cs->sel_mcc,
			cs->sel_mnc, GSM48_REJECT_PLMN_NOT_ALLOWED);
		msg_type = GSM322_EVENT_REG_FAILED;
		goto _stop;
	}

	/* if LAI is forbidden, don't start */
	if (gsm_subscr_is_forbidden_plmn(subscr, cs->sel_mcc, cs->sel_mnc)) {
		LOGP(DMM, LOGL_INFO, "Loc. upd. not allowed PLMN.\n");
		msg_type = GSM322_EVENT_REG_FAILED;
		goto stop;
	}
	if (gsm322_is_forbidden_la(ms, cs->sel_mcc,
		cs->sel_mnc, cs->sel_lac)) {
		LOGP(DMM, LOGL_INFO, "Loc. upd. not allowed LA.\n");
		msg_type = GSM322_EVENT_REG_FAILED;
		goto stop;
	}

	/* 4.4.4.9 if cell is barred, don't start */
	if ((!subscr->acc_barr && s->cell_barr)
	 || (!subscr->acc_barr && !((subscr->acc_class & 0xfbff) &
	 				(s->class_barr ^ 0xffff)))) {
		LOGP(DMM, LOGL_INFO, "Loc. upd. no access.\n");
		msg_type = GSM322_EVENT_REG_FAILED;
		goto stop;
	}

	mm->lupd_mcc = cs->sel_mcc;
	mm->lupd_mnc = cs->sel_mnc;
	mm->lupd_lac = cs->sel_lac;

	LOGP(DSUM, LOGL_INFO, "Perform location update (MCC %s, MNC %s "
		"LAC 0x%04x)\n", gsm_print_mcc(mm->lupd_mcc),
		gsm_print_mnc(mm->lupd_mnc), mm->lupd_lac);

	return gsm48_mm_tx_loc_upd_req(ms);
}

/* initiate a normal location update / imsi attach */
static int gsm48_mm_loc_upd_normal(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = &cs->sel_si;
	struct msgb *nmsg;

	/* in case we already have a location update going on */
	if (mm->lupd_pending) {
		LOGP(DMM, LOGL_INFO, "Loc. upd. already pending.\n");

		return -EBUSY;
	}

	/* no location update, if limited service */
	if (cs->state != GSM322_C3_CAMPED_NORMALLY) {
		LOGP(DMM, LOGL_INFO, "Loc. upd. not allowed.\n");

#if 0
		/* don't send message, if we got not triggered by PLMN search */
		if (!msg)
			return 0;
#endif

		/* send message to PLMN search process */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_REG_FAILED);
		if (!nmsg)
			return -ENOMEM;
		gsm322_plmn_sendmsg(ms, nmsg);

		return 0;
	}

	/* if location update is not required */
	if (subscr->ustate == GSM_SIM_U1_UPDATED
	 && cs->selected
	 && cs->sel_mcc == subscr->mcc
	 && cs->sel_mnc == subscr->mnc
	 && cs->sel_lac == subscr->lac
	 && (subscr->imsi_attached
	  || !s->att_allowed)) {
		LOGP(DMM, LOGL_INFO, "Loc. upd. not required.\n");
	  	subscr->imsi_attached = 1;

		/* go straight to normal service state */
		new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
				GSM48_MM_SST_NORMAL_SERVICE);

#if 0
		/* don't send message, if we got not triggered by PLMN search */
		if (!msg)
			return 0;
#endif

		/* send message to PLMN search process */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_REG_SUCCESS);
		if (!nmsg)
			return -ENOMEM;
		gsm322_plmn_sendmsg(ms, nmsg);

		return 0;
	}

	/* 4.4.3 is attachment required? */
	if (subscr->ustate == GSM_SIM_U1_UPDATED
	 && cs->selected
	 && cs->sel_mcc == subscr->mcc
	 && cs->sel_mnc == subscr->mnc
	 && cs->sel_lac == subscr->lac
	 && !subscr->imsi_attached
	 && s->att_allowed) {
		/* do location update for IMSI attach */
		LOGP(DMM, LOGL_INFO, "Do Loc. upd. for IMSI attach.\n");
		mm->lupd_type = 2;
	} else {
		/* do normal location update */
		LOGP(DMM, LOGL_INFO, "Do normal Loc. upd.\n");
		mm->lupd_type = 0;
	}

	/* start location update */
	mm->lupd_attempt = 0;
	mm->lupd_pending = 1;
	mm->lupd_ra_failure = 0;

	return gsm48_mm_loc_upd(ms, msg);
}

/* initiate a periodic location update */
static int gsm48_mm_loc_upd_periodic(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* in case we already have a location update going on */
	if (mm->lupd_pending) {
		LOGP(DMM, LOGL_INFO, "Loc. upd. already pending.\n");
		return -EBUSY;
	}

	/* start periodic location update */
	mm->lupd_type = 1;
	mm->lupd_pending = 1;
	mm->lupd_ra_failure = 0;

	return gsm48_mm_loc_upd(ms, msg);
}

/* ignore location update */
static int gsm48_mm_loc_upd_ignore(struct osmocom_ms *ms, struct msgb *msg)
{
	return 0;
}

/* 9.2.15 send LOCATION UPDATING REQUEST message */
static int gsm48_mm_tx_loc_upd_req(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm_settings *set = &ms->settings;
	struct gsm_support *sup = &ms->support;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	struct gsm48_loc_upd_req *nlu; /* NOTE: mi_len is part of struct */
	uint8_t pwr_lev;
	uint8_t buf[11];

	LOGP(DMM, LOGL_INFO, "LOCATION UPDATING REQUEST\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));
	nlu = (struct gsm48_loc_upd_req *)msgb_put(nmsg, sizeof(*nlu));

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_LOC_UPD_REQUEST;

	/* location updating type */
	nlu->type = mm->lupd_type;
	/* cipering key */
	nlu->key_seq = gsm_subscr_get_key_seq(ms, subscr);
	/* LAI (last SIM stored LAI)
	 *
	 * NOTE: The TMSI is only valid within a LAI!
	 */
	gsm48_encode_lai_hex(&nlu->lai, subscr->mcc, subscr->mnc, subscr->lac);
	LOGP(DMM, LOGL_INFO, " using LAI (mcc %s mnc %s " "lac 0x%04x)\n",
		gsm_print_mcc(subscr->mcc),
		gsm_print_mnc(subscr->mnc), subscr->lac);
	/* classmark 1 */
	pwr_lev = gsm48_current_pwr_lev(set, cs->sel_arfcn);
	gsm48_encode_classmark1(&nlu->classmark1, sup->rev_lev, sup->es_ind,
		set->a5_1, pwr_lev);
	/* MI */
	if (subscr->tmsi != 0xffffffff) { /* have TMSI ? */
		gsm48_encode_mi(buf, NULL, ms, GSM_MI_TYPE_TMSI);
		LOGP(DMM, LOGL_INFO, " using TMSI 0x%08x\n", subscr->tmsi);
	} else {
		gsm48_encode_mi(buf, NULL, ms, GSM_MI_TYPE_IMSI);
		LOGP(DMM, LOGL_INFO, " using IMSI %s\n", subscr->imsi);
	}
	msgb_put(nmsg, buf[1]); /* length is part of nlu */
	memcpy(&nlu->mi_len, buf + 1, 1 + buf[1]);

	new_mm_state(mm, GSM48_MM_ST_WAIT_RR_CONN_LUPD, 0);

	/* push RR header and send down */
	mm->est_cause = RR_EST_CAUSE_LOC_UPD;
	return gsm48_mm_to_rr(ms, nmsg, GSM48_RR_EST_REQ, 0, mm->est_cause);
}

/* 4.4.4.1 RR is esablised during location update */
static int gsm48_mm_est_loc_upd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* start location update timer */
	start_mm_t3210(mm);

	new_mm_state(mm, GSM48_MM_ST_LOC_UPD_INIT, 0);

	return 0;
}

/* 4.4.4.6 LOCATION UPDATING ACCEPT is received */
static int gsm48_mm_rx_loc_upd_acc(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_loc_area_id *lai = (struct gsm48_loc_area_id *) gh->data;
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct msgb *nmsg;

	if (payload_len < sizeof(struct gsm48_loc_area_id)) {
		short_read:
		LOGP(DMM, LOGL_NOTICE, "Short read of LOCATION UPDATING ACCEPT "
			"message error.\n");
		return -EINVAL;
	}
	tlv_parse(&tp, &gsm48_mm_att_tlvdef,
		gh->data + sizeof(struct gsm48_loc_area_id),
		payload_len - sizeof(struct gsm48_loc_area_id), 0, 0);

	/* update has finished */
	mm->lupd_pending = 0;

	/* RA was successfull */
	mm->lupd_ra_failure = 0;

	/* stop periodic location updating timer */
	stop_mm_t3212(mm); /* 4.4.2 */

	/* LAI */
	gsm48_decode_lai_hex(lai, &subscr->mcc, &subscr->mnc, &subscr->lac);

	/* stop location update timer */
	stop_mm_t3210(mm);

	/* reset attempt counter */
	mm->lupd_attempt = 0;

	/* mark SIM as attached */
	subscr->imsi_attached = 1;

	/* set the status in the sim to updated */
	new_sim_ustate(subscr, GSM_SIM_U1_UPDATED);

	/* store LOCI on sim */
	gsm_subscr_write_loci(ms);

	/* set last registered PLMN */
	if (subscr->lac > 0x0000 && subscr->lac < 0xfffe) {
		subscr->plmn_valid = 1;
		subscr->plmn_mcc = subscr->mcc;
		subscr->plmn_mnc = subscr->mnc;
	}

	LOGP(DSUM, LOGL_INFO, "Location update accepted\n");
	LOGP(DMM, LOGL_INFO, "LOCATION UPDATING ACCEPT (mcc %s mnc %s "
		"lac 0x%04x)\n", gsm_print_mcc(subscr->mcc),
		gsm_print_mnc(subscr->mnc), subscr->lac);

	/* remove LA from forbidden list */
	gsm322_del_forbidden_la(ms, subscr->mcc, subscr->mnc, subscr->lac);

	/* MI */
	if (TLVP_PRESENT(&tp, GSM48_IE_MOBILE_ID)) {
		const uint8_t *mi;
		uint8_t mi_type;
		uint32_t tmsi;

		mi = TLVP_VAL(&tp, GSM48_IE_MOBILE_ID)-1;
		if (mi[0] < 1)
			goto short_read;
		mi_type = mi[1] & GSM_MI_TYPE_MASK;
		switch (mi_type) {
		case GSM_MI_TYPE_TMSI:
			if (payload_len + sizeof(struct gsm48_loc_area_id) < 6
			 || mi[0] < 5)
				goto short_read;
			memcpy(&tmsi, mi+2, 4);
			subscr->tmsi = ntohl(tmsi);
			LOGP(DMM, LOGL_INFO, "got TMSI 0x%08x (%u)\n",
				subscr->tmsi, subscr->tmsi);

			/* store LOCI on sim */
			gsm_subscr_write_loci(ms);

			/* send TMSI REALLOCATION COMPLETE */
			gsm48_mm_tx_tmsi_reall_cpl(ms);
			break;
		case GSM_MI_TYPE_IMSI:
			LOGP(DMM, LOGL_INFO, "TMSI removed\n");
			subscr->tmsi = 0xffffffff;

			/* store LOCI on sim */
			gsm_subscr_write_loci(ms);

			/* send TMSI REALLOCATION COMPLETE */
			gsm48_mm_tx_tmsi_reall_cpl(ms);
			break;
		default:
			LOGP(DMM, LOGL_NOTICE, "TMSI reallocation with unknown "
				"MI type %d.\n", mi_type);
		}
	}

	/* send message to PLMN search process */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_REG_SUCCESS);
	if (!nmsg)
		return -ENOMEM;
	gsm322_plmn_sendmsg(ms, nmsg);

	/* follow on proceed */
	if (TLVP_PRESENT(&tp, GSM48_IE_MOBILE_ID))
		LOGP(DMM, LOGL_NOTICE, "follow-on proceed not supported.\n");

	/* start RR release timer */
	start_mm_t3240(mm);

	new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);

	return 0;
}

/* 4.4.4.7 LOCATION UPDATING REJECT is received */
static int gsm48_mm_rx_loc_upd_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);

	if (payload_len < 1) {
		LOGP(DMM, LOGL_NOTICE, "Short read of LOCATION UPDATING REJECT "
			"message error.\n");
		return -EINVAL;
	}

	/* RA was successfull */
	mm->lupd_ra_failure = 0;

	/* stop periodic location updating timer */
	stop_mm_t3212(mm); /* 4.4.2 */

	/* stop location update timer */
	stop_mm_t3210(mm);

	/* store until RR is released */
	mm->lupd_rej_cause = *gh->data;

	/* start RR release timer */
	start_mm_t3240(mm);

	new_mm_state(mm, GSM48_MM_ST_LOC_UPD_REJ, 0);

	return 0;
}

/* 4.4.4.7 RR is released after location update reject */
static int gsm48_mm_rel_loc_upd_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	struct gsm322_msg *ngm;

	LOGP(DMM, LOGL_INFO, "Loc. upd. rejected (cause %d)\n",
		mm->lupd_rej_cause);

	/* stop RR release timer */
	stop_mm_t3240(mm);

	/* new status */
	switch (mm->lupd_rej_cause) {
	case GSM48_REJECT_IMSI_UNKNOWN_IN_HLR:
	case GSM48_REJECT_ILLEGAL_MS:
	case GSM48_REJECT_ILLEGAL_ME:
		/* reset attempt counter */
		mm->lupd_attempt = 0;

		/* SIM invalid */
		subscr->sim_valid = 0;

		// fall through
	case GSM48_REJECT_PLMN_NOT_ALLOWED:
	case GSM48_REJECT_LOC_NOT_ALLOWED:
	case GSM48_REJECT_ROAMING_NOT_ALLOWED:
		/* TMSI and LAI invalid */
		subscr->tmsi = 0xffffffff;
		subscr->lac = 0x0000;

		/* key is invalid */
		subscr->key_seq = 7;

		/* update status */
		new_sim_ustate(subscr, GSM_SIM_U3_ROAMING_NA);

		/* store LOCI on sim */
		gsm_subscr_write_loci(ms);

		/* update has finished */
		mm->lupd_pending = 0;
	}

	/* send event to PLMN search process */
	switch(mm->lupd_rej_cause) {
	case GSM48_REJECT_ROAMING_NOT_ALLOWED:
	case GSM48_REJECT_PLMN_NOT_ALLOWED:
	case GSM48_REJECT_LOC_NOT_ALLOWED:
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_ROAMING_NA);
		break;
	case GSM48_REJECT_IMSI_UNKNOWN_IN_HLR:
	case GSM48_REJECT_ILLEGAL_MS:
	case GSM48_REJECT_ILLEGAL_ME:
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_INVALID_SIM);
		break;
	default:
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_REG_FAILED);
	}
	if (!nmsg)
		return -ENOMEM;
	ngm = (struct gsm322_msg *)nmsg->data;
	ngm->reject = mm->lupd_rej_cause;
	gsm322_plmn_sendmsg(ms, nmsg);

	/* forbidden list */
	switch (mm->lupd_rej_cause) {
	case GSM48_REJECT_IMSI_UNKNOWN_IN_HLR:
		LOGP(DSUM, LOGL_INFO, "Location update failed (IMSI unknown "
			"in HLR)\n");
		break;
	case GSM48_REJECT_ILLEGAL_MS:
		LOGP(DSUM, LOGL_INFO, "Location update failed (Illegal MS)\n");
		break;
	case GSM48_REJECT_ILLEGAL_ME:
		LOGP(DSUM, LOGL_INFO, "Location update failed (Illegal ME)\n");
		break;
	case GSM48_REJECT_PLMN_NOT_ALLOWED:
		gsm_subscr_add_forbidden_plmn(subscr, mm->lupd_mcc,
			mm->lupd_mnc, mm->lupd_rej_cause);
		LOGP(DSUM, LOGL_INFO, "Location update failed (PLMN not "
			"allowed)\n");
		break;
	case GSM48_REJECT_LOC_NOT_ALLOWED:
	case GSM48_REJECT_ROAMING_NOT_ALLOWED:
		gsm322_add_forbidden_la(ms, mm->lupd_mcc, mm->lupd_mnc,
			mm->lupd_lac, mm->lupd_rej_cause);
		LOGP(DSUM, LOGL_INFO, "Location update failed (LAI not "
			"allowed)\n");
		break;
	default:
		/* 4.4.4.9 continue with failure handling */
		return gsm48_mm_loc_upd_failed(ms, NULL);
	}

	/* return to MM IDLE */
	return gsm48_mm_return_idle(ms, NULL);
}

/* 4.2.2 delay a location update */
static int gsm48_mm_loc_upd_delay_per(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	LOGP(DMM, LOGL_INFO, "Schedule a pending periodic loc. upd.\n");
	mm->lupd_periodic = 1;

	return 0;
}

/* delay a location update retry */
static int gsm48_mm_loc_upd_delay_retry(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	LOGP(DMM, LOGL_INFO, "Schedule a pending periodic loc. upd.\n");
	mm->lupd_retry = 1;

	return 0;
}

/* process failues as described in the lower part of 4.4.4.9 */
static int gsm48_mm_loc_upd_failed(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm_subscriber *subscr = &ms->subscr;

	LOGP(DSUM, LOGL_INFO, "Location update failed\n");

	/* stop location update timer, if running */
	stop_mm_t3210(mm);

	if (subscr->ustate == GSM_SIM_U1_UPDATED
	 && mm->lupd_mcc == subscr->mcc
	 && mm->lupd_mnc == subscr->mnc
	 && mm->lupd_lac == subscr->lac) {
		if (mm->lupd_attempt < 4) {
			LOGP(DSUM, LOGL_INFO, "Try location update later\n");
			LOGP(DMM, LOGL_INFO, "Loc. upd. failed, retry #%d\n",
				mm->lupd_attempt);

			/* start update retry timer */
			start_mm_t3211(mm);

			/* return to MM IDLE */
			return gsm48_mm_return_idle(ms, NULL);
		} else
			LOGP(DMM, LOGL_INFO, "Loc. upd. failed too often.\n");
	}

	/* TMSI and LAI invalid */
	subscr->tmsi = 0xffffffff;
	subscr->lac = 0x0000;

	/* key is invalid */
	subscr->key_seq = 7;

	/* update status */
	new_sim_ustate(subscr, GSM_SIM_U2_NOT_UPDATED);

	/* store LOCI on sim */
	gsm_subscr_write_loci(ms);

	/* start update retry timer (RR connection is released) */
	if (mm->lupd_attempt < 4) {
		mm->start_t3211 = 1;
		LOGP(DSUM, LOGL_INFO, "Try location update later\n");
	}

	/* return to MM IDLE */
	return gsm48_mm_return_idle(ms, NULL);
}

/* abort a location update due to radio failure or release */
static int gsm48_mm_rel_loc_upd_abort(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_rr_hdr *rrh = (struct gsm48_rr_hdr *)msg->data;

	/* stop RR release timer */
	stop_mm_t3240(mm);

	if (rrh->msg_type == GSM48_RR_REL_IND) {
		LOGP(DMM, LOGL_INFO, "RR link released after loc. upd.\n");

		/* continue with failure handling */
		return gsm48_mm_loc_upd_failed(ms, NULL);
	}

	LOGP(DMM, LOGL_INFO, "Loc. upd. aborted by radio (cause #%d)\n",
		rrh->cause);

	/* random access failure, but not two successive failures */
	if (rrh->cause == RR_REL_CAUSE_RA_FAILURE && !mm->lupd_ra_failure) {
		mm->lupd_ra_failure = 1;

		/* start RA failure timer */
		start_mm_t3213(mm);

		return 0;
	}

	/* RA was successfull or sent twice */
	mm->lupd_ra_failure = 0;

	/* continue with failure handling */
	return gsm48_mm_loc_upd_failed(ms, NULL);
}

/* location update has timed out */
static int gsm48_mm_loc_upd_timeout(struct osmocom_ms *ms, struct msgb *msg)
{
	struct msgb *nmsg;
	struct gsm48_rr_hdr *nrrh;

	/* abort RR connection */
	nmsg = gsm48_rr_msgb_alloc(GSM48_RR_ABORT_REQ);
	if (!nmsg)
		return -ENOMEM;
	nrrh = (struct gsm48_rr_hdr *) nmsg->data;
	nrrh->cause = GSM48_RR_CAUSE_ABNORMAL_TIMER;
	gsm48_rr_downmsg(ms, nmsg);

	/* continue with failure handling */
	return gsm48_mm_loc_upd_failed(ms, NULL);
}

/*
 * process handlers for MM connections
 */

/* cm reestablish request message from upper layer */
static int gsm48_mm_tx_cm_serv_req(struct osmocom_ms *ms, int rr_prim,
	uint8_t cm_serv)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm_settings *set = &ms->settings;
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	struct gsm48_service_request *nsr; /* NOTE: includes MI length */
	uint8_t *cm2lv;
	uint8_t buf[11];

	LOGP(DMM, LOGL_INFO, "CM SERVICE REQUEST (cause %d)\n", mm->est_cause);

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));
	nsr = (struct gsm48_service_request *)msgb_put(nmsg, sizeof(*nsr));
	cm2lv = (uint8_t *)&nsr->classmark;

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_CM_SERV_REQ;

	/* type and key */
	nsr->cm_service_type = cm_serv;
	nsr->cipher_key_seq = gsm_subscr_get_key_seq(ms, subscr);
	/* classmark 2 */
	cm2lv[0] = sizeof(struct gsm48_classmark2);
	gsm48_rr_enc_cm2(ms, (struct gsm48_classmark2 *)(cm2lv + 1),
		(rr_prim == GSM48_RR_EST_REQ)	? cs->sel_arfcn
						: rr->cd_now.arfcn);
	/* MI */
	if (mm->est_cause == RR_EST_CAUSE_EMERGENCY && set->emergency_imsi[0]) {
		LOGP(DMM, LOGL_INFO, "-> Using IMSI %s for emergency\n",
			set->emergency_imsi);
		gsm48_generate_mid_from_imsi(buf, set->emergency_imsi);
	} else
	if (!subscr->sim_valid) { /* have no SIM ? */
		LOGP(DMM, LOGL_INFO, "-> Using IMEI %s\n",
			set->imei);
		gsm48_encode_mi(buf, NULL, ms, GSM_MI_TYPE_IMEI);
	} else
	if (subscr->tmsi != 0xffffffff) { /* have TMSI ? */
		gsm48_encode_mi(buf, NULL, ms, GSM_MI_TYPE_TMSI);
		LOGP(DMM, LOGL_INFO, "-> Using TMSI\n");
	} else {
		gsm48_encode_mi(buf, NULL, ms, GSM_MI_TYPE_IMSI);
		LOGP(DMM, LOGL_INFO, "-> Using IMSI %s\n",
			subscr->imsi);
	}
	msgb_put(nmsg, buf[1]); /* length is part of nsr */
	memcpy(&nsr->mi_len, buf + 1, 1 + buf[1]);
	/* prio is optional for eMLPP */

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, rr_prim, 0, mm->est_cause);
}

/* cm service abort message from upper layer
 * NOTE: T3240 is started by the calling function
 */
static int gsm48_mm_tx_cm_service_abort(struct osmocom_ms *ms)
{
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;

	LOGP(DMM, LOGL_INFO, "CM SERVICE ABORT\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_CM_SERV_ABORT;

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, GSM48_RR_DATA_REQ, 0, 0);
}

/* cm service acknowledge is received from lower layer */
static int gsm48_mm_rx_cm_service_acc(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* stop MM connection timer */
	stop_mm_t3230(mm);

	new_mm_state(mm, GSM48_MM_ST_MM_CONN_ACTIVE, 0);

	return gsm48_mm_conn_go_dedic(ms);
}

/* 9.2.6 CM SERVICE REJECT message received */
static int gsm48_mm_rx_cm_service_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	uint8_t abort_any = 0;
	uint8_t reject_cause;

	if (payload_len < 1) {
		LOGP(DMM, LOGL_NOTICE, "Short read of cm service reject "
			"message error.\n");
		return -EINVAL;
	}

	/* reject cause */
	reject_cause = *gh->data;

	LOGP(DMM, LOGL_INFO, "CM SERVICE REJECT (cause %d)\n", reject_cause);

	/* stop MM connection timer */
	stop_mm_t3230(mm);

	/* selection action on cause value */
	switch (reject_cause) {
	case GSM48_REJECT_IMSI_UNKNOWN_IN_VLR:
	case GSM48_REJECT_ILLEGAL_ME:
		abort_any = 1;

		/* TMSI and LAI invalid */
		subscr->tmsi = 0xffffffff;
		subscr->lac = 0x0000;

		/* key is invalid */
		subscr->key_seq = 7;

		/* update status */
		new_sim_ustate(subscr, GSM_SIM_U2_NOT_UPDATED);

		/* store LOCI on sim */
		gsm_subscr_write_loci(ms);

		/* change to WAIT_NETWORK_CMD state impied by abort_any == 1 */

		if (reject_cause == GSM48_REJECT_ILLEGAL_ME)
			subscr->sim_valid = 0;

		break;
	default:
		/* state implied by the number of remaining connections */
		;
	}

	/* release MM connection(s) */
	gsm48_mm_release_mm_conn(ms, abort_any, 16, 0, 0);

	/* state depends on the existance of remaining MM connections */
	if (llist_empty(&mm->mm_conn))
		new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);
	else
		new_mm_state(mm, GSM48_MM_ST_MM_CONN_ACTIVE, 0);

	return 0;
}

/* initiate an MM connection 4.5.1.1
 *
 * this function is called when:
 * - no RR connection exists
 * - an RR connection exists, but this is the first MM connection
 * - an RR connection exists, and there are already MM connection(s)
 */
static int gsm48_mm_init_mm(struct osmocom_ms *ms, struct msgb *msg,
	int rr_prim)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	int msg_type = mmh->msg_type;
	int emergency = 0;
	uint8_t cause = 0, cm_serv = 0, proto = 0;
	struct msgb *nmsg;
	struct gsm48_mmxx_hdr *nmmh;
	struct gsm48_mm_conn *conn, *conn_found = NULL;
	uint8_t sapi = mmh->sapi;

	/* reset loc. upd. counter on CM service request */
	mm->lupd_attempt = 0;

	/* find if there is already a pending connection */
	llist_for_each_entry(conn, &mm->mm_conn, list) {
		if (conn->state == GSM48_MMXX_ST_CONN_PEND) {
			conn_found = conn;
			break;
		}
	}

	/* if pending connection */
	if (conn_found) {
		LOGP(DMM, LOGL_INFO, "Init MM Connection, but already have "
			"pending MM Connection.\n");
		cause = 17;
		/* use sapi from connection. if no connection, use sapi from
		 * message.
		 */
		sapi = conn_found->sapi;
		reject:
		nmsg = NULL;
		switch(msg_type) {
		case GSM48_MMCC_EST_REQ:
			nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMCC_REL_IND,
				mmh->ref, mmh->transaction_id, sapi);
			break;
		case GSM48_MMSS_EST_REQ:
			nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSS_REL_IND,
				mmh->ref, mmh->transaction_id, sapi);
			break;
		case GSM48_MMSMS_EST_REQ:
			nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSMS_REL_IND,
				mmh->ref, mmh->transaction_id, sapi);
			break;
		}
		if (!nmsg)
			return -ENOMEM;
		nmmh = (struct gsm48_mmxx_hdr *)nmsg->data;
		nmmh->cause = cause;
		gsm48_mmxx_upmsg(ms, nmsg);

		return -EBUSY;
	}
	/* in case of an emergency setup */
	if (msg_type == GSM48_MMCC_EST_REQ && mmh->emergency)
		emergency = 1;

	/* if sim is not updated */
	if (!emergency && subscr->ustate != GSM_SIM_U1_UPDATED) {
		LOGP(DMM, LOGL_INFO, "Init MM Connection, but SIM not "
			"updated.\n");
		cause = 21;
		goto reject;
	}

	if (mm->state == GSM48_MM_ST_MM_IDLE) {
		/* current MM idle state */
		switch (mm->substate) {
		case GSM48_MM_SST_NORMAL_SERVICE:
		case GSM48_MM_SST_PLMN_SEARCH_NORMAL:
			LOGP(DMM, LOGL_INFO, "Init MM Connection.\n");
			break; /* allow when normal */
		case GSM48_MM_SST_LOC_UPD_NEEDED:
		case GSM48_MM_SST_ATTEMPT_UPDATE:
			/* store mm request if attempting to update */
			if (!emergency) {
				LOGP(DMM, LOGL_INFO, "Init MM Connection, but "
					"attempting to update.\n");
				cause = 21;
				goto reject;
				/* TODO: implement delay and start loc upd. */
			}
			break;
		default:
			/* reject if not emergency */
			if (!emergency) {
				LOGP(DMM, LOGL_INFO, "Init MM Connection, not "
					"in normal state.\n");
				cause = 21;
				goto reject;
			}
			break;
		}
	} else
		LOGP(DMM, LOGL_INFO, "Init another MM Connection.\n");

	/* set cause, service, proto */
	switch(msg_type) {
	case GSM48_MMCC_EST_REQ:
		if (emergency) {
			cause = RR_EST_CAUSE_EMERGENCY;
			cm_serv = GSM48_CMSERV_EMERGENCY;
		} else {
			cause = RR_EST_CAUSE_ORIG_TCHF;
			cm_serv = GSM48_CMSERV_MO_CALL_PACKET;
		}
		proto = GSM48_PDISC_CC;
		break;
	case GSM48_MMSS_EST_REQ:
		cause = RR_EST_CAUSE_OTHER_SDCCH;
		cm_serv = GSM48_CMSERV_SUP_SERV;
		proto = GSM48_PDISC_NC_SS;
		break;
	case GSM48_MMSMS_EST_REQ:
		cause = RR_EST_CAUSE_OTHER_SDCCH;
		cm_serv = GSM48_CMSERV_SMS;
		proto = GSM48_PDISC_SMS;
		break;
	}

	/* create MM connection instance */
	conn = mm_conn_new(mm, proto, mmh->transaction_id, mmh->sapi, mmh->ref);
	if (!conn)
		return -ENOMEM;

	new_conn_state(conn, GSM48_MMXX_ST_CONN_PEND);

	/* send CM SERVICE REQUEST */
	if (rr_prim) {
		mm->est_cause = cause;
		return gsm48_mm_tx_cm_serv_req(ms, rr_prim, cm_serv);
	} else
		return 0;
}

/* 4.5.1.1 a) MM connection request triggers RR connection */
static int gsm48_mm_init_mm_no_rr(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	int rc;

	/* start MM connection by requesting RR connection */
	rc = gsm48_mm_init_mm(ms, msg, GSM48_RR_EST_REQ);
	if (rc)
		return rc;

	new_mm_state(mm, GSM48_MM_ST_WAIT_RR_CONN_MM_CON, 0);

	return 0;
}

/* 4.5.1.1 a) RR is esablised during mm connection, wait for CM accepted */
static int gsm48_mm_est_mm_con(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* 4.5.1.7 if there is no more MM connection */
	if (llist_empty(&mm->mm_conn)) {
		LOGP(DMM, LOGL_INFO, "MM Connection, are already gone.\n");

		/* start RR release timer */
		start_mm_t3240(mm);

		new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);

		/* send abort */
		return gsm48_mm_tx_cm_service_abort(ms);
	}

	/* start MM connection timer */
	start_mm_t3230(mm);

	new_mm_state(mm, GSM48_MM_ST_WAIT_OUT_MM_CONN, 0);

	return 0;
}

/* 4.5.1.1 b) MM connection request on existing RR connection */
static int gsm48_mm_init_mm_first(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	int rc;

	/* start MM connection by sending data */
	rc = gsm48_mm_init_mm(ms, msg, GSM48_RR_DATA_REQ);
	if (rc)
		return rc;

	/* stop "RR connection release not allowed" timer */
	stop_mm_t3241(mm);

	/* start MM connection timer */
	start_mm_t3230(mm);

	new_mm_state(mm, GSM48_MM_ST_WAIT_OUT_MM_CONN, 0);

	return 0;
}

/* 4.5.1.1 b) another MM connection request on existing RR connection */
static int gsm48_mm_init_mm_more(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	int rc;

	/* start MM connection by sending data */
	rc = gsm48_mm_init_mm(ms, msg, GSM48_RR_DATA_REQ);
	if (rc)
		return rc;

	/* start MM connection timer */
	start_mm_t3230(mm);

	new_mm_state(mm, GSM48_MM_ST_WAIT_ADD_OUT_MM_CON, 0);

	return 0;
}

/* 4.5.1.1 b) delay on WAIT FOR NETWORK COMMAND state */
static int gsm48_mm_init_mm_wait(struct osmocom_ms *ms, struct msgb *msg)
{
	/* reject */
	gsm48_mm_init_mm_reject(ms, msg);
#if 0
	this requires handling when leaving this state...

	struct gsm48_mmlayer *mm = &ms->mmlayer;
	int rc;

	/* just create the MM connection in pending state */
	rc = gsm48_mm_init_mm(ms, msg, 0);
	if (rc)
		return rc;

	/* start MM connection timer */
	start_mm_t3230(mm);

	new_mm_state(mm, GSM48_MM_ST_WAIT_ADD_OUT_MM_CON, 0);
#endif

	return 0;
}

/* initiate an mm connection other cases */
static int gsm48_mm_init_mm_reject(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	int msg_type = mmh->msg_type;
	int sapi = mmh->sapi;
	struct msgb *nmsg;
	struct gsm48_mmxx_hdr *nmmh;

	/* reject */
	nmsg = NULL;
	switch(msg_type) {
	case GSM48_MMCC_EST_REQ:
		nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMCC_REL_IND, mmh->ref,
			mmh->transaction_id, sapi);
		break;
	case GSM48_MMSS_EST_REQ:
		nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSS_REL_IND, mmh->ref,
			mmh->transaction_id, sapi);
		break;
	case GSM48_MMSMS_EST_REQ:
		nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSMS_REL_IND, mmh->ref,
			mmh->transaction_id, sapi);
		break;
	}
	if (!nmsg)
		return -ENOMEM;
	nmmh = (struct gsm48_mmxx_hdr *)nmsg->data;
	nmmh->cause = 17;
	gsm48_mmxx_upmsg(ms, nmsg);

	return 0;
}

/* accepting pending connection, got dedicated mode
 *
 * this function is called:
 * - when ciphering command is received
 * - when cm service is accepted
 */
static int gsm48_mm_conn_go_dedic(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mm_conn *conn, *conn_found = NULL;
	struct msgb *nmsg;
	struct gsm48_mmxx_hdr *nmmh;

	/* the first and only pending connection is the recent requested */
	llist_for_each_entry(conn, &mm->mm_conn, list) {
		if (conn->state == GSM48_MMXX_ST_CONN_PEND) {
			conn_found = conn;
			break;
		}
	}

	/* if no pending connection (anymore) */
	if (!conn_found) {
		LOGP(DMM, LOGL_INFO, "No pending MM Connection.\n");

		return 0;
	}

	new_conn_state(conn, GSM48_MMXX_ST_DEDICATED);

	/* send establishment confirm */
	nmsg = NULL;
	switch(conn_found->protocol) {
	case GSM48_PDISC_CC:
		nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMCC_EST_CNF,
			conn_found->ref, conn_found->transaction_id,
			conn_found->sapi);
		break;
	case GSM48_PDISC_NC_SS:
		nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSS_EST_CNF,
			conn_found->ref, conn_found->transaction_id,
			conn_found->sapi);
		break;
	case GSM48_PDISC_SMS:
		if (!mm->sapi3_link) {
			LOGP(DMM, LOGL_INFO, "Sapi 3 link down, requesting "
				"link, waiting for confirm.\n");
			nmsg = gsm48_l3_msgb_alloc();
			if (!nmsg)
				return -ENOMEM;
			return gsm48_mm_to_rr(ms, nmsg, GSM48_RR_EST_REQ,
				conn_found->sapi, 0);
		}
		nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSMS_EST_CNF,
			conn_found->ref, conn_found->transaction_id,
			conn_found->sapi);
		break;
	}
	if (!nmsg)
		return -ENOMEM;
	nmmh = (struct gsm48_mmxx_hdr *)nmsg->data;
	gsm48_mmxx_upmsg(ms, nmsg);

	return 0;
}

/* a RR-SYNC-IND is received during MM connection establishment */
static int gsm48_mm_sync_ind_wait(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_rr_hdr *rrh = (struct gsm48_rr_hdr *)msg->data;

	if (rrh->cause != RR_SYNC_CAUSE_CIPHERING) {
		LOGP(DMM, LOGL_NOTICE, "Ignore sync indication, not waiting "
			"for CM service\n");
		return -EINVAL;
	}

	/* stop MM connection timer */
	stop_mm_t3230(mm);

	new_mm_state(mm, GSM48_MM_ST_MM_CONN_ACTIVE, 0);

	return gsm48_mm_conn_go_dedic(ms);
}

/* a RR-SYNC-IND is received during MM connection active */
static int gsm48_mm_sync_ind_active(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mm_conn *conn;
	struct msgb *nmsg;
	struct gsm48_mmxx_hdr *nmmh;

	/* stop MM connection timer */
	stop_mm_t3230(mm);

	/* broadcast all MMCC connection(s) */
	llist_for_each_entry(conn, &mm->mm_conn, list) {
		/* send MMCC-SYNC-IND */
		nmsg = NULL;
		switch(conn->protocol) {
		case GSM48_PDISC_CC:
			nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMCC_SYNC_IND,
				conn->ref, conn->transaction_id, conn->sapi);
			break;
		}
		if (!nmsg)
			continue; /* skip if not of CC type */
		nmmh = (struct gsm48_mmxx_hdr *)nmsg->data;
		/* copy L3 message */
		nmsg->l3h = msgb_put(nmsg, msgb_l3len(msg));
		memcpy(nmsg->l3h, msg->l3h, msgb_l3len(msg));
		gsm48_mmxx_upmsg(ms, nmsg);
	}

	return 0;
}

/* 4.5.1.2 RR abort/release is received during MM connection establishment */
static int gsm48_mm_abort_mm_con(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_rr_hdr *rrh = (struct gsm48_rr_hdr *)msg->data;
	int cause;

	/* stop RR release timer */
	stop_mm_t3240(mm);

	/* this conversion is not of any standard */
	switch(rrh->cause) {
	case RR_REL_CAUSE_NOT_AUTHORIZED:
	case RR_REL_CAUSE_EMERGENCY_ONLY:
	case RR_REL_CAUSE_TRY_LATER:
		cause = 21;
		break;
	case RR_REL_CAUSE_NORMAL:
		cause = 16;
		break;
	default:
		cause = 47;
	}

	/* stop MM connection timer */
	stop_mm_t3230(mm);

	/* release all connections */
	gsm48_mm_release_mm_conn(ms, 1, cause, 1, 0);

	/* return to MM IDLE */
	return gsm48_mm_return_idle(ms, NULL);
}

/* 4.5.1.2 timeout is received during MM connection establishment */
static int gsm48_mm_timeout_mm_con(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* release pending connection */
	gsm48_mm_release_mm_conn(ms, 0, 102, 0, 0);

	/* state depends on the existance of remaining MM connections */
	if (llist_empty(&mm->mm_conn)) {
		/* start RR release timer */
		start_mm_t3240(mm);

		new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);
	} else
		new_mm_state(mm, GSM48_MM_ST_MM_CONN_ACTIVE, 0);

	return 0;
}

/* respond to paging */
static int gsm48_mm_est(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	mm->est_cause = RR_EST_CAUSE_ANS_PAG_ANY;
	new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);

	return 0;
}

/* send CM data */
static int gsm48_mm_data(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	struct gsm48_mm_conn *conn;
	int msg_type = mmh->msg_type;

	/* get connection, if not exist (anymore), release */
	conn = mm_conn_by_ref(mm, mmh->ref);
	if (!conn) {
		LOGP(DMM, LOGL_INFO, "MMXX_DATA_REQ with unknown (already "
			"released) ref=%x, sending MMXX_REL_IND\n", mmh->ref);
		switch(msg_type & GSM48_MMXX_MASK) {
		case GSM48_MMCC_CLASS:
			mmh->msg_type = GSM48_MMCC_REL_IND;
			break;
		case GSM48_MMSS_CLASS:
			mmh->msg_type = GSM48_MMSS_REL_IND;
			break;
		case GSM48_MMSMS_CLASS:
			mmh->msg_type = GSM48_MMSMS_REL_IND;
			break;
		}
		mmh->cause = 31;

		/* mirror message with REL_IND + cause */
		return gsm48_mmxx_upmsg(ms, msg);
	}

	/* set SAPI, if upper layer does not do it correctly */
	mmh->sapi = conn->sapi;

	/* pull MM header */
	msgb_pull(msg, sizeof(struct gsm48_mmxx_hdr));

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, msg, GSM48_RR_DATA_REQ, conn->sapi, 0);
}

/* release of MM connection (active state) */
static int gsm48_mm_release_active(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	struct gsm48_mm_conn *conn;

	/* get connection, if not exist (anymore), release */
	conn = mm_conn_by_ref(mm, mmh->ref);
	if (conn)
		mm_conn_free(conn);

	/* state depends on the existance of remaining MM connections */
	if (llist_empty(&mm->mm_conn)) {
		/* start RR release timer */
		start_mm_t3240(mm);

		new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);
	} else
		new_mm_state(mm, GSM48_MM_ST_MM_CONN_ACTIVE, 0);

	return 0;
}

/* release of MM connection (wait for additional state) */
static int gsm48_mm_release_wait_add(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	struct gsm48_mm_conn *conn;

	/* get connection, if not exist (anymore), release */
	conn = mm_conn_by_ref(mm, mmh->ref);
	if (conn)
		mm_conn_free(conn);

	return 0;
}

/* release of MM connection (wait for active state) */
static int gsm48_mm_release_wait_active(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	struct gsm48_mm_conn *conn;

	/* get connection, if not exist (anymore), release */
	conn = mm_conn_by_ref(mm, mmh->ref);
	if (conn)
		mm_conn_free(conn);

	/* 4.5.1.7 if there is no MM connection during wait for active state */
	if (llist_empty(&mm->mm_conn)) {
		LOGP(DMM, LOGL_INFO, "No MM Connection during 'wait for "
			"active' state.\n");

		/* start RR release timer */
		start_mm_t3240(mm);

		new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);

		/* send abort */
		return gsm48_mm_tx_cm_service_abort(ms);
	}

	return 0;
}

/* release of MM connection (wait for RR state) */
static int gsm48_mm_release_wait_rr(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	struct gsm48_mm_conn *conn;

	/* get connection, if not exist (anymore), release */
	conn = mm_conn_by_ref(mm, mmh->ref);
	if (conn)
		mm_conn_free(conn);

	/* later, if RR connection is established, the CM SERIVE ABORT
	 * message will be sent
	 */
	return 0;
}

/* abort RR connection (due to T3240) */
static int gsm48_mm_abort_rr(struct osmocom_ms *ms, struct msgb *msg)
{
	struct msgb *nmsg;
	struct gsm48_rr_hdr *nrrh;

	/* send abort to RR */
	nmsg = gsm48_rr_msgb_alloc(GSM48_RR_ABORT_REQ);
	if (!nmsg)
		return -ENOMEM;
	nrrh = (struct gsm48_rr_hdr *) nmsg->data;
	nrrh->cause = GSM48_RR_CAUSE_ABNORMAL_TIMER;
	gsm48_rr_downmsg(ms, nmsg);

	/* return to MM IDLE */
	return gsm48_mm_return_idle(ms, NULL);
}

/*
 * other processes
 */

/* RR is released in other states */
static int gsm48_mm_rel_other(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* stop RR release timer (if running) */
	stop_mm_t3240(mm);

	/* return to MM IDLE */
	return gsm48_mm_return_idle(ms, NULL);
}

/*
 * sapi 3
 */

static int gsm48_rcv_rr_sapi3(struct osmocom_ms *ms, struct msgb *msg,
	int msg_type, uint8_t sapi)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mm_conn *conn;

	switch (msg_type) {
	case GSM48_RR_EST_CNF:
		LOGP(DMM, LOGL_INFO, "SAPI 3 link up, confirming conns.\n");
		mm->sapi3_link = 1;
		/* indicate establishment to sapi 3 connections */
		llist_for_each_entry(conn, &mm->mm_conn, list) {
			if (conn->sapi == sapi
			 && conn->state == GSM48_MMXX_ST_DEDICATED) {
				struct gsm48_mmxx_hdr *nmmh;
				struct msgb *nmsg;

				nmsg = gsm48_mmxx_msgb_alloc(
					GSM48_MMSMS_EST_CNF, conn->ref,
					conn->transaction_id, conn->sapi);
				if (!nmsg)
					return -ENOMEM;
				nmmh = (struct gsm48_mmxx_hdr *)nmsg->data;
				gsm48_mmxx_upmsg(ms, nmsg);
			}
		}
		break;
	case GSM48_RR_DATA_IND:
		return gsm48_mm_data_ind(ms, msg);
	case GSM48_RR_REL_IND:
		LOGP(DMM, LOGL_INFO, "SAPI 3 link down, releasing conns.\n");
		mm->sapi3_link = 0;
		gsm48_mm_release_mm_conn(ms, 1, 16, 0, sapi);
		break;
	}
	msgb_free(msg);

	return 0;
}

/*
 * state machines
 */

/* state trasitions for MMxx-SAP messages from upper layers */
static struct downstate {
	uint32_t	states;
	uint32_t	substates;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} downstatelist[] = {
	/* 4.2.2.1 Normal service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MMSS_EST_REQ, gsm48_mm_init_mm_no_rr},

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MMSMS_EST_REQ, gsm48_mm_init_mm_no_rr},

	/* 4.2.2.2 Attempt to update / Loc. Upd. needed */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE) |
				SBIT(GSM48_MM_SST_LOC_UPD_NEEDED),
	 GSM48_MMCC_EST_REQ, gsm48_mm_init_mm_no_rr}, /* emergency only */

	/* 4.2.2.3 Limited service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_LIMITED_SERVICE),
	 GSM48_MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},

	/* 4.2.2.4 No IMSI */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NO_IMSI),
	 GSM48_MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},

	/* 4.2.2.5 PLMN search, normal service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MMSS_EST_REQ, gsm48_mm_init_mm_no_rr},

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MMSMS_EST_REQ, gsm48_mm_init_mm_no_rr},

	/* 4.2.2.6 PLMN search */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH),
	 GSM48_MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},

	/* 4.5.1.1 MM Connection (EST) */
	{SBIT(GSM48_MM_ST_RR_CONN_RELEASE_NA), ALL_STATES,
	 GSM48_MMCC_EST_REQ, gsm48_mm_init_mm_first},

	{SBIT(GSM48_MM_ST_RR_CONN_RELEASE_NA), ALL_STATES,
	 GSM48_MMSS_EST_REQ, gsm48_mm_init_mm_first},

	{SBIT(GSM48_MM_ST_RR_CONN_RELEASE_NA), ALL_STATES,
	 GSM48_MMSMS_EST_REQ, gsm48_mm_init_mm_first},

	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), ALL_STATES,
	 GSM48_MMCC_EST_REQ, gsm48_mm_init_mm_more},

	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), ALL_STATES,
	 GSM48_MMSS_EST_REQ, gsm48_mm_init_mm_more},

	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), ALL_STATES,
	 GSM48_MMSMS_EST_REQ, gsm48_mm_init_mm_more},

	{SBIT(GSM48_MM_ST_WAIT_NETWORK_CMD), ALL_STATES,
	 GSM48_MMCC_EST_REQ, gsm48_mm_init_mm_wait},

	{SBIT(GSM48_MM_ST_WAIT_NETWORK_CMD), ALL_STATES,
	 GSM48_MMSS_EST_REQ, gsm48_mm_init_mm_wait},

	{SBIT(GSM48_MM_ST_WAIT_NETWORK_CMD), ALL_STATES,
	 GSM48_MMSMS_EST_REQ, gsm48_mm_init_mm_wait},

	{ALL_STATES, ALL_STATES,
	 GSM48_MMCC_EST_REQ, gsm48_mm_init_mm_reject},

	{ALL_STATES, ALL_STATES,
	 GSM48_MMSS_EST_REQ, gsm48_mm_init_mm_reject},

	{ALL_STATES, ALL_STATES,
	 GSM48_MMSMS_EST_REQ, gsm48_mm_init_mm_reject},

	/* 4.5.2.1 MM Connection (DATA) */
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 GSM48_MMCC_DATA_REQ, gsm48_mm_data},

	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 GSM48_MMSS_DATA_REQ, gsm48_mm_data},

	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 GSM48_MMSMS_DATA_REQ, gsm48_mm_data},

	/* 4.5.2.1 MM Connection (REL) */
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), ALL_STATES,
	 GSM48_MMCC_REL_REQ, gsm48_mm_release_active},

	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), ALL_STATES,
	 GSM48_MMSS_REL_REQ, gsm48_mm_release_active},

	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), ALL_STATES,
	 GSM48_MMSMS_REL_REQ, gsm48_mm_release_active},

	{SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 GSM48_MMCC_REL_REQ, gsm48_mm_release_wait_add},

	{SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 GSM48_MMSS_REL_REQ, gsm48_mm_release_wait_add},

	{SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 GSM48_MMSMS_REL_REQ, gsm48_mm_release_wait_add},

	{SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN), ALL_STATES,
	 GSM48_MMCC_REL_REQ, gsm48_mm_release_wait_active},

	{SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN), ALL_STATES,
	 GSM48_MMSS_REL_REQ, gsm48_mm_release_wait_active},

	{SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN), ALL_STATES,
	 GSM48_MMSMS_REL_REQ, gsm48_mm_release_wait_active},

	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON), ALL_STATES,
	 GSM48_MMCC_REL_REQ, gsm48_mm_release_wait_rr},

	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON), ALL_STATES,
	 GSM48_MMSS_REL_REQ, gsm48_mm_release_wait_rr},

	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON), ALL_STATES,
	 GSM48_MMSMS_REL_REQ, gsm48_mm_release_wait_rr},
};

#define DOWNSLLEN \
	(sizeof(downstatelist) / sizeof(struct downstate))

int gsm48_mmxx_downmsg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	int msg_type = mmh->msg_type;
	struct gsm48_mm_conn *conn;
	int i, rc;

	/* keep up to date with the transaction ID */
	conn = mm_conn_by_ref(mm, mmh->ref);
	if (conn)
		conn->transaction_id = mmh->transaction_id;

	LOGP(DMM, LOGL_INFO, "(ms %s) Received '%s' event in state %s\n",
		ms->name, get_mmxx_name(msg_type),
		gsm48_mm_state_names[mm->state]);
	if (mm->state == GSM48_MM_ST_MM_IDLE)
		LOGP(DMM, LOGL_INFO, "-> substate %s\n",
			gsm48_mm_substate_names[mm->substate]);
	LOGP(DMM, LOGL_INFO, "-> callref %x, transaction_id %d\n",
		mmh->ref, mmh->transaction_id);

	/* Find function for current state and message */
	for (i = 0; i < DOWNSLLEN; i++)
		if ((msg_type == downstatelist[i].type)
		 && ((1 << mm->state) & downstatelist[i].states)
		 && ((1 << mm->substate) & downstatelist[i].substates))
			break;
	if (i == DOWNSLLEN) {
		LOGP(DMM, LOGL_NOTICE, "Message unhandled at this state.\n");
		msgb_free(msg);
		return 0;
	}

	rc = downstatelist[i].rout(ms, msg);

	if (downstatelist[i].rout != gsm48_mm_data)
		msgb_free(msg);

	return rc;
}

/* state trasitions for radio ressource messages (lower layer) */
static struct rrdatastate {
	uint32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} rrdatastatelist[] = {
	/* paging */
	{SBIT(GSM48_MM_ST_MM_IDLE),
	 GSM48_RR_EST_IND, gsm48_mm_est},

	/* imsi detach */
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_IMSI_D), /* 4.3.4.4 */
	 GSM48_RR_EST_CNF, gsm48_mm_imsi_detach_sent},

	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_IMSI_D), /* 4.3.4.4 (unsuc.) */
	 GSM48_RR_REL_IND, gsm48_mm_imsi_detach_end},
	 	/* also this may happen if SABM is ackwnowledged with DISC */

	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_IMSI_D), /* 4.3.4.4 (lost) */
	 GSM48_RR_ABORT_IND, gsm48_mm_imsi_detach_end},

	{SBIT(GSM48_MM_ST_IMSI_DETACH_INIT), /* 4.3.4.4 (unsuc.) */
	 GSM48_RR_REL_IND, gsm48_mm_imsi_detach_end},

	{SBIT(GSM48_MM_ST_IMSI_DETACH_INIT), /* 4.3.4.4 (lost) */
	 GSM48_RR_ABORT_IND, gsm48_mm_imsi_detach_end},

	/* location update */
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_LUPD), /* 4.4.4.1 */
	 GSM48_RR_EST_CNF, gsm48_mm_est_loc_upd},

	{SBIT(GSM48_MM_ST_LOC_UPD_INIT) |
	 SBIT(GSM48_MM_ST_WAIT_RR_CONN_LUPD), /* 4.4.4.9 */
	 GSM48_RR_REL_IND, gsm48_mm_rel_loc_upd_abort},

	{SBIT(GSM48_MM_ST_LOC_UPD_INIT) |
	 SBIT(GSM48_MM_ST_WAIT_RR_CONN_LUPD), /* 4.4.4.9 */
	 GSM48_RR_ABORT_IND, gsm48_mm_rel_loc_upd_abort},

	{SBIT(GSM48_MM_ST_LOC_UPD_REJ), /* 4.4.4.7 */
	 GSM48_RR_REL_IND, gsm48_mm_rel_loc_upd_rej},

	{SBIT(GSM48_MM_ST_LOC_UPD_REJ), /* 4.4.4.7 */
	 GSM48_RR_ABORT_IND, gsm48_mm_rel_loc_upd_rej},

	/* MM connection (EST) */
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON), /* 4.5.1.1 */
	 GSM48_RR_EST_CNF, gsm48_mm_est_mm_con},

	/* MM connection (DATA) */
	{ALL_STATES,
	 GSM48_RR_DATA_IND, gsm48_mm_data_ind},

	/* MM connection (SYNC) */
	{SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), /* 4.5.1.1 */
	 GSM48_RR_SYNC_IND, gsm48_mm_sync_ind_wait},

	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE),
	 GSM48_RR_SYNC_IND, gsm48_mm_sync_ind_active},

	/* MM connection (REL/ABORT) */
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON) |
	 SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), /* 4.5.1.2 */
	 GSM48_RR_REL_IND, gsm48_mm_abort_mm_con},

	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON) |
	 SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), /* 4.5.1.2 */
	 GSM48_RR_ABORT_IND, gsm48_mm_abort_mm_con},

	/* MM connection (REL/ABORT with re-establishment possibility) */
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), /* not supported */
	 GSM48_RR_REL_IND, gsm48_mm_abort_mm_con},

	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), /* not supported */
	 GSM48_RR_ABORT_IND, gsm48_mm_abort_mm_con},

	/* other (also wait for network command) */
	{ALL_STATES,
	 GSM48_RR_REL_IND, gsm48_mm_rel_other},

	{ALL_STATES,
	 GSM48_RR_ABORT_IND, gsm48_mm_rel_other},
};

#define RRDATASLLEN \
	(sizeof(rrdatastatelist) / sizeof(struct rrdatastate))

static int gsm48_rcv_rr(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_rr_hdr *rrh = (struct gsm48_rr_hdr *)msg->data;
	int msg_type = rrh->msg_type;
	int sapi = rrh->sapi;
	int i, rc;

	LOGP(DMM, LOGL_INFO, "(ms %s) Received '%s' from RR in state %s "
		"(sapi %d)\n", ms->name, get_rr_name(msg_type),
		gsm48_mm_state_names[mm->state], sapi);

	if (sapi)
		return gsm48_rcv_rr_sapi3(ms, msg, msg_type, sapi);

	/* find function for current state and message */
	for (i = 0; i < RRDATASLLEN; i++)
		if ((msg_type == rrdatastatelist[i].type)
		 && ((1 << mm->state) & rrdatastatelist[i].states))
			break;
	if (i == RRDATASLLEN) {
		LOGP(DMM, LOGL_NOTICE, "Message unhandled at this state.\n");
		msgb_free(msg);
		return 0;
	}

	rc = rrdatastatelist[i].rout(ms, msg);

	if (rrdatastatelist[i].rout != gsm48_mm_data_ind)
		msgb_free(msg);

	return rc;
}

/* state trasitions for mobile managemnt messages (lower layer) */
static struct mmdatastate {
	uint32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} mmdatastatelist[] = {
	{ALL_STATES, /* 4.3.1.2 */
	 GSM48_MT_MM_TMSI_REALL_CMD, gsm48_mm_rx_tmsi_realloc_cmd},

	{ALL_STATES, /* 4.3.2.2 */
	 GSM48_MT_MM_AUTH_REQ, gsm48_mm_rx_auth_req},

	{ALL_STATES, /* 4.3.2.5 */
	 GSM48_MT_MM_AUTH_REJ, gsm48_mm_rx_auth_rej},

	{ALL_STATES, /* 4.3.3.2 */
	 GSM48_MT_MM_ID_REQ, gsm48_mm_rx_id_req},

	{ALL_STATES, /* 4.3.5.2 */
	 GSM48_MT_MM_ABORT, gsm48_mm_rx_abort},

	{ALL_STATES, /* 4.3.6.2 */
	 GSM48_MT_MM_INFO, gsm48_mm_rx_info},

	{SBIT(GSM48_MM_ST_LOC_UPD_INIT), /* 4.4.4.6 */
	 GSM48_MT_MM_LOC_UPD_ACCEPT, gsm48_mm_rx_loc_upd_acc},

	{SBIT(GSM48_MM_ST_LOC_UPD_INIT), /* 4.4.4.7 */
	 GSM48_MT_MM_LOC_UPD_REJECT, gsm48_mm_rx_loc_upd_rej},

	{ALL_STATES, /* 4.5.1.1 */
	 GSM48_MT_MM_CM_SERV_ACC, gsm48_mm_rx_cm_service_acc},

	{ALL_STATES, /* 4.5.1.1 */
	 GSM48_MT_MM_CM_SERV_REJ, gsm48_mm_rx_cm_service_rej},
};

#define MMDATASLLEN \
	(sizeof(mmdatastatelist) / sizeof(struct mmdatastate))

static int gsm48_mm_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_rr_hdr *rrh = (struct gsm48_rr_hdr *)msg->data;
	int sapi = rrh->sapi;
	struct gsm48_hdr *gh = msgb_l3(msg);
	uint8_t pdisc = gh->proto_discr & 0x0f;
	uint8_t msg_type = gh->msg_type & 0xbf;
	struct gsm48_mmxx_hdr *mmh;
	int msg_supported = 0; /* determine, if message is supported at all */
	int rr_prim = -1, rr_est = -1; /* no prim set */
	uint8_t skip_ind;
	int i, rc;

	/* 9.2.19 */
	if (msg_type == GSM48_MT_MM_NULL) {
		msgb_free(msg);
		return 0;
	}

	if (mm->state == GSM48_MM_ST_IMSI_DETACH_INIT) {
		LOGP(DMM, LOGL_NOTICE, "DATA IND ignored during IMSI "
			"detach.\n");
		msgb_free(msg);
		return 0;
	}
	/* pull the RR header */
	msgb_pull(msg, sizeof(struct gsm48_rr_hdr));

	/* create transaction (if not exists) and push message */
	switch (pdisc) {
	case GSM48_PDISC_CC:
		rr_prim = GSM48_MMCC_DATA_IND;
		rr_est = GSM48_MMCC_EST_IND;
		break;
	case GSM48_PDISC_NC_SS:
		rr_prim = GSM48_MMSS_DATA_IND;
		rr_est = GSM48_MMSS_EST_IND;
		break;
	case GSM48_PDISC_SMS:
		rr_prim = GSM48_MMSMS_DATA_IND;
		rr_est = GSM48_MMSMS_EST_IND;
		break;
	}
	if (rr_prim != -1) {
		uint8_t transaction_id = ((gh->proto_discr & 0xf0) ^ 0x80) >> 4;
			/* flip */
		struct gsm48_mm_conn *conn;

		/* find transaction, if any */
		conn = mm_conn_by_id(mm, pdisc, transaction_id);

		/* create MM connection instance */
		if (!conn) {
			conn = mm_conn_new(mm, pdisc, transaction_id, sapi,
				mm_conn_new_ref++);
			rr_prim = rr_est;
		}
		if (!conn) {
			msgb_free(msg);
			return -ENOMEM;
		}

		/* push new header */
		msgb_push(msg, sizeof(struct gsm48_mmxx_hdr));
		mmh = (struct gsm48_mmxx_hdr *)msg->data;
		mmh->msg_type = rr_prim;
		mmh->ref = conn->ref;
		mmh->transaction_id = conn->transaction_id;
		mmh->sapi = conn->sapi;

		/* go MM CONN ACTIVE state */
		if (mm->state == GSM48_MM_ST_WAIT_NETWORK_CMD
		 || mm->state == GSM48_MM_ST_RR_CONN_RELEASE_NA) {
			/* stop RR release timer */
			stop_mm_t3240(mm);

			/* stop "RR connection release not allowed" timer */
			stop_mm_t3241(mm);

			new_mm_state(mm, GSM48_MM_ST_MM_CONN_ACTIVE, 0);
		}
	}

	/* forward message */
	switch (pdisc) {
	case GSM48_PDISC_MM:
		skip_ind = (gh->proto_discr & 0xf0) >> 4;

		/* ignore if skip indicator is not B'0000' */
		if (skip_ind) {
			msgb_free(msg);
			return 0;
		}
		break; /* follow the selection proceedure below */

	case GSM48_PDISC_CC:
		rc = gsm48_rcv_cc(ms, msg);
		msgb_free(msg);
		return rc;

	case GSM48_PDISC_NC_SS:
		rc = gsm480_rcv_ss(ms, msg);
		msgb_free(msg);
		return rc;

	case GSM48_PDISC_SMS:
		rc = gsm411_rcv_sms(ms, msg);
		msgb_free(msg);
		return rc;

	case 0x0f: /* test TS 04.14 */
		LOGP(DMM, LOGL_NOTICE, "Test protocol 0x%02x according to "
			"TS 04.14 is not supported.\n", pdisc);
		goto status;
	default:
		LOGP(DMM, LOGL_NOTICE, "Protocol type 0x%02x unsupported.\n",
			pdisc);
status:
		msgb_free(msg);
		return gsm48_mm_tx_mm_status(ms,
			GSM48_REJECT_MSG_TYPE_NOT_IMPLEMENTED);
	}

	LOGP(DMM, LOGL_INFO, "(ms %s) Received '%s' in MM state %s\n", ms->name,
		get_mm_name(msg_type), gsm48_mm_state_names[mm->state]);

	stop_mm_t3212(mm); /* 4.4.2 */

	/* 11.2 re-start pending RR release timer */
	if (osmo_timer_pending(&mm->t3240)) {
		stop_mm_t3240(mm);
		start_mm_t3240(mm);
	}

	/* find function for current state and message */
	for (i = 0; i < MMDATASLLEN; i++) {
		if (msg_type == mmdatastatelist[i].type)
			msg_supported = 1;
		if ((msg_type == mmdatastatelist[i].type)
		 && ((1 << mm->state) & mmdatastatelist[i].states))
			break;
	}
	if (i == MMDATASLLEN) {
		msgb_free(msg);
		if (msg_supported) {
			LOGP(DMM, LOGL_NOTICE, "Message unhandled at this "
				"state.\n");
			return gsm48_mm_tx_mm_status(ms,
				GSM48_REJECT_MSG_TYPE_NOT_COMPATIBLE);
		} else {
			LOGP(DMM, LOGL_NOTICE, "Message not supported.\n");
			return gsm48_mm_tx_mm_status(ms,
				GSM48_REJECT_MSG_TYPE_NOT_IMPLEMENTED);
		}
	}

	rc = mmdatastatelist[i].rout(ms, msg);

	msgb_free(msg);

	return rc;
}

/* state trasitions for mobile management events */
static struct eventstate {
	uint32_t	states;
	uint32_t	substates;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} eventstatelist[] = {
	/* 4.2.3 return to MM IDLE */
	{ALL_STATES - SBIT(GSM48_MM_ST_MM_IDLE), ALL_STATES,
	 GSM48_MM_EVENT_NO_CELL_FOUND, gsm48_mm_no_cell_found},

	{ALL_STATES - SBIT(GSM48_MM_ST_MM_IDLE), ALL_STATES,
	 GSM48_MM_EVENT_CELL_SELECTED, gsm48_mm_return_idle},

	/* 4.2.2.1 Normal service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_USER_PLMN_SEL, gsm48_mm_plmn_search}, /* 4.2.1.2 */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_LOST_COVERAGE, gsm48_mm_plmn_search}, /* 4.2.1.2 */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_CELL_SELECTED, gsm48_mm_loc_upd_normal}, /* change */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_TIMEOUT_T3211, gsm48_mm_loc_upd},

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_TIMEOUT_T3213, gsm48_mm_loc_upd},

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_periodic},

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_start},

	/* 4.2.2.2 Attempt to update / Loc. upd. needed */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE) |
				SBIT(GSM48_MM_SST_LOC_UPD_NEEDED),
	 GSM48_MM_EVENT_USER_PLMN_SEL, gsm48_mm_plmn_search}, /* 4.2.1.2 */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE) |
				SBIT(GSM48_MM_SST_LOC_UPD_NEEDED),
	 GSM48_MM_EVENT_LOST_COVERAGE, gsm48_mm_plmn_search}, /* 4.2.1.2 */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE) |
				SBIT(GSM48_MM_SST_LOC_UPD_NEEDED),
	 GSM48_MM_EVENT_CELL_SELECTED, gsm48_mm_loc_upd_normal}, /* change */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 GSM48_MM_EVENT_TIMEOUT_T3211, gsm48_mm_loc_upd},

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 GSM48_MM_EVENT_TIMEOUT_T3213, gsm48_mm_loc_upd},

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_periodic},

	/* 4.2.2.3 Limited service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_LIMITED_SERVICE),
	 GSM48_MM_EVENT_USER_PLMN_SEL, gsm48_mm_plmn_search}, /* 4.2.1.2 */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_LIMITED_SERVICE),
	 GSM48_MM_EVENT_LOST_COVERAGE, gsm48_mm_plmn_search}, /* 4.2.1.2 */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_LIMITED_SERVICE),
	 GSM48_MM_EVENT_CELL_SELECTED, gsm48_mm_loc_upd_normal}, /* if allow. */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_LIMITED_SERVICE),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_delay_per}, /* 4.4.2 */

	/* 4.2.2.4 No IMSI */
	/* 4.2.2.5 PLMN search, normal service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_NO_CELL_FOUND, gsm48_mm_no_cell_found}, /* 4.2.1.1 */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_CELL_SELECTED, gsm48_mm_cell_selected}, /* 4.2.1.1 */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_delay_per}, /* 4.4.2 */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_start},

	/* 4.2.2.6 PLMN search */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH),
	 GSM48_MM_EVENT_NO_CELL_FOUND, gsm48_mm_no_cell_found}, /* 4.2.1.1 */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH),
	 GSM48_MM_EVENT_CELL_SELECTED, gsm48_mm_cell_selected}, /* 4.2.1.1 */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_delay_per}, /* 4.4.2 */

	/* No cell available */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NO_CELL_AVAIL),
	 GSM48_MM_EVENT_CELL_SELECTED, gsm48_mm_cell_selected}, /* 4.2.1.1 */

	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NO_CELL_AVAIL),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_delay_per}, /* 4.4.2 */

	/* IMSI detach in other cases */
	{SBIT(GSM48_MM_ST_MM_IDLE), ALL_STATES, /* silently detach */
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_end},

	{SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN) |
	 SBIT(GSM48_MM_ST_MM_CONN_ACTIVE) |
	 SBIT(GSM48_MM_ST_PROCESS_CM_SERV_P) |
	 SBIT(GSM48_MM_ST_WAIT_REEST) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON) |
	 SBIT(GSM48_MM_ST_MM_CONN_ACTIVE_VGCS) |
	 SBIT(GSM48_MM_ST_WAIT_NETWORK_CMD), ALL_STATES, /* we can release */
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_release},

	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_IMSI_D) |
	 SBIT(GSM48_MM_ST_IMSI_DETACH_INIT) |
	 SBIT(GSM48_MM_ST_IMSI_DETACH_PEND), ALL_STATES, /* ignore */
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_ignore},

	{ALL_STATES, ALL_STATES,
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_delay},

	{SBIT(GSM48_MM_ST_IMSI_DETACH_INIT), ALL_STATES,
	 GSM48_MM_EVENT_TIMEOUT_T3220, gsm48_mm_imsi_detach_abort},

	/* location update in other cases */
	{SBIT(GSM48_MM_ST_MM_IDLE), ALL_STATES,
	 GSM48_MM_EVENT_TIMEOUT_T3211, gsm48_mm_loc_upd_delay_retry},

	{SBIT(GSM48_MM_ST_MM_IDLE), ALL_STATES,
	 GSM48_MM_EVENT_TIMEOUT_T3213, gsm48_mm_loc_upd_delay_retry},

	{ALL_STATES, ALL_STATES,
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_ignore},

	{ALL_STATES - SBIT(GSM48_MM_ST_MM_IDLE), ALL_STATES,
	 GSM48_MM_EVENT_TIMEOUT_T3210, gsm48_mm_loc_upd_timeout},

	{ALL_STATES - SBIT(GSM48_MM_ST_MM_IDLE), ALL_STATES,
	 GSM48_MM_EVENT_TIMEOUT_T3213, gsm48_mm_loc_upd_failed},
		/* 4.4.4.9 c) (but without retry) */

	/* SYSINFO event */
	{ALL_STATES, ALL_STATES,
	 GSM48_MM_EVENT_SYSINFO, gsm48_mm_sysinfo},

	/* T3240 timed out */
	{SBIT(GSM48_MM_ST_WAIT_NETWORK_CMD) |
	 SBIT(GSM48_MM_ST_LOC_UPD_REJ), ALL_STATES, /* 4.4.4.8 */
	 GSM48_MM_EVENT_TIMEOUT_T3240, gsm48_mm_abort_rr},

	/* T3230 timed out */
	{SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 GSM48_MM_EVENT_TIMEOUT_T3230, gsm48_mm_timeout_mm_con},

	/* SIM reports SRES */
	{ALL_STATES, ALL_STATES, /* 4.3.2.2 */
	 GSM48_MM_EVENT_AUTH_RESPONSE, gsm48_mm_tx_auth_rsp},

#if 0
	/* change in classmark is reported */
	{ALL_STATES, ALL_STATES,
	 GSM48_MM_EVENT_CLASSMARK_CHG, gsm48_mm_classm_chg},
#endif
};

#define EVENTSLLEN \
	(sizeof(eventstatelist) / sizeof(struct eventstate))

static int gsm48_mm_ev(struct osmocom_ms *ms, int msg_type, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	int i, rc;

	if (mm->state == GSM48_MM_ST_MM_IDLE) {
		if (msg_type != GSM48_MM_EVENT_SYSINFO)
			LOGP(DMM, LOGL_INFO, "(ms %s) Received '%s' event in "
				"state MM IDLE, %s\n", ms->name,
				get_mmevent_name(msg_type),
				gsm48_mm_substate_names[mm->substate]);
	} else
		LOGP(DMM, LOGL_INFO, "(ms %s) Received '%s' event in state "
			"%s\n", ms->name, get_mmevent_name(msg_type),
		gsm48_mm_state_names[mm->state]);

	/* Find function for current state and message */
	for (i = 0; i < EVENTSLLEN; i++)
		if ((msg_type == eventstatelist[i].type)
		 && ((1 << mm->state) & eventstatelist[i].states)
		 && ((1 << mm->substate) & eventstatelist[i].substates))
			break;
	if (i == EVENTSLLEN) {
		LOGP(DMM, LOGL_NOTICE, "Message unhandled at this state.\n");
		return 0;
	}

	rc = eventstatelist[i].rout(ms, msg);

	return rc;
}

/*
 * MM Register (SIM insert and remove)
 */

/* register new SIM card and trigger attach */
static int gsm48_mmr_reg_req(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct msgb *nmsg;

	/* schedule insertion of SIM */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_SIM_INSERT);
	if (!nmsg)
		return -ENOMEM;
	gsm322_plmn_sendmsg(ms, nmsg);

	/* 4.2.1.2 SIM is inserted in state NO IMSI */
	if (mm->state == GSM48_MM_ST_MM_IDLE
	 && mm->substate == GSM48_MM_SST_NO_IMSI)
		new_mm_state(mm, GSM48_MM_ST_MM_IDLE,
			gsm48_mm_set_plmn_search(ms));

	return 0;
}

/* trigger detach of sim card */
static int gsm48_mmr_nreg_req(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct msgb *nmsg;

	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_IMSI_DETACH);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(mm->ms, nmsg);

	return 0;
}

static int gsm48_rcv_mmr(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmr *mmr = (struct gsm48_mmr *)msg->data;
	int msg_type = mmr->msg_type;
	int rc = 0;

	LOGP(DMM, LOGL_INFO, "(ms %s) Received '%s' event\n", ms->name,
		get_mmr_name(msg_type));
	switch(msg_type) {
		case GSM48_MMR_REG_REQ:
			rc = gsm48_mmr_reg_req(ms);
			break;
		case GSM48_MMR_NREG_REQ:
			rc = gsm48_mmr_nreg_req(ms);
			break;
		default:
			LOGP(DMM, LOGL_NOTICE, "Message unhandled.\n");
	}

	return rc;
}


