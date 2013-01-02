/* Cell Scanning code for OsmocomBB */

/* (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#include <l1ctl_proto.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/msgb.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/rsl.h>

#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/common/gps.h>
#include <osmocom/bb/misc/cell_log.h>
#include "../../../gsmmap/geo.h"

#define READ_WAIT	2, 0
#define RACH_WAIT	0, 900000
#define MIN_RXLEV_DBM	-106
#define MAX_DIST	2000

enum {
	SCAN_STATE_PM,
	SCAN_STATE_SYNC,
	SCAN_STATE_READ,
	SCAN_STATE_RACH,
};

/* ranges of bands */
static uint16_t basic_band_range[][2] = {{0, 124}, {512, 885}, {955, 1023}, {0, 0}};
uint16_t (*band_range)[][2] = &basic_band_range;

#define INFO_FLG_PM	1
#define INFO_FLG_SYNC	2
#define INFO_FLG_SI1	4
#define INFO_FLG_SI2	8
#define INFO_FLG_SI2bis	16
#define INFO_FLG_SI2ter	32
#define INFO_FLG_SI3	64
#define INFO_FLG_SI4	128

static struct osmocom_ms *ms;
static struct osmo_timer_list timer;

static struct pm_info {
	uint16_t flags;
	int8_t rxlev_dbm;
} pm[1024];

static int started = 0;
static int state;
static int8_t min_rxlev_dbm = MIN_RXLEV_DBM;
static int sync_count;
static int pm_index, pm_gps_valid;
static double pm_gps_x, pm_gps_y, pm_gps_z;
static int arfcn;
static int rach_count;
static FILE *logfp = NULL;
extern char *logname;
extern int RACH_MAX;


static struct gsm48_sysinfo sysinfo;

static struct log_si {
	uint16_t flags;
	uint8_t bsic;
	int8_t rxlev_dbm;
	uint16_t mcc, mnc, lac, cellid;
	uint8_t ta;
	double latitude, longitude;
} log_si;

struct rach_ref {
	uint8_t valid;
	uint8_t cr;
	uint8_t t1, t2, t3;
} rach_ref;

#define LOGFILE(fmt, args...) \
	fprintf(logfp, fmt, ## args);
#define LOGFLUSH() \
	fflush(logfp);

static void start_sync(void);
static void start_rach(void);
static void start_pm(void);

static void log_gps(void)
{
	if (!g.enable || !g.valid)
		return;
	LOGFILE("position %.8f %.8f\n", g.longitude, g.latitude);
}

static void log_time(void)
{
	time_t now;

	if (g.enable && g.valid)
		now = g.gmt;
	else
		time(&now);
	LOGFILE("time %lu\n", now);
}

static void log_frame(char *tag, uint8_t *data)
{
	int i;

	LOGFILE("%s", tag);
	for (i = 0; i < 23; i++)
		LOGFILE(" %02x", *data++);
	LOGFILE("\n");
}

static void log_pm(void)
{
	int count = 0, i;

	LOGFILE("[power]\n");
	log_time();
	log_gps();
	for (i = 0; i <= 1023; i++) {
		if ((pm[i].flags & INFO_FLG_PM)) {
			if (!count)
				LOGFILE("arfcn %d", i);
			LOGFILE(" %d", pm[i].rxlev_dbm);
			count++;
			if (count == 12) {
				LOGFILE("\n");
				count = 0;
			}
		} else {
			if (count) {
				LOGFILE("\n");
				count = 0;
			}
		}
	}
	if (count)
		LOGFILE("\n");

	LOGFILE("\n");
	LOGFLUSH();
}

static void log_sysinfo(void)
{
	struct rx_meas_stat *meas = &ms->meas;
	struct gsm48_sysinfo *s = &sysinfo;
	int8_t rxlev_dbm;
	char ta_str[32] = "";

	if (log_si.ta != 0xff)
		sprintf(ta_str, " TA=%d", log_si.ta);

	LOGP(DSUM, LOGL_INFO, "Cell: ARFCN=%d MCC=%s MNC=%s (%s, %s)%s\n",
		arfcn, gsm_print_mcc(s->mcc), gsm_print_mnc(s->mnc),
		gsm_get_mcc(s->mcc), gsm_get_mnc(s->mcc, s->mnc), ta_str);

	LOGFILE("[sysinfo]\n");
	LOGFILE("arfcn %d\n", s->arfcn);
	log_time();
	log_gps();
	LOGFILE("bsic %d,%d\n", s->bsic >> 3, s->bsic & 7);
	rxlev_dbm = meas->rxlev / meas->frames - 110;
	LOGFILE("rxlev %d\n", rxlev_dbm);
	if (s->si1)
		log_frame("si1", s->si1_msg);
	if (s->si2)
		log_frame("si2", s->si2_msg);
	if (s->si2bis)
		log_frame("si2bis", s->si2b_msg);
	if (s->si2ter)
		log_frame("si2ter", s->si2t_msg);
	if (s->si3)
		log_frame("si3", s->si3_msg);
	if (s->si4)
		log_frame("si4", s->si4_msg);
	if (log_si.ta != 0xff)
		LOGFILE("ta %d\n", log_si.ta);

	LOGFILE("\n");
	LOGFLUSH();
}

static void timeout_cb(void *arg)
{
	switch (state) {
	case SCAN_STATE_READ:
		LOGP(DRR, LOGL_INFO, "Timeout reading BCCH\n");
		start_sync();
		break;
	case SCAN_STATE_RACH:
		LOGP(DRR, LOGL_INFO, "Timeout on RACH\n");
		rach_count++;
		start_rach();
		break;
	}
}

static void stop_timer(void)
{
	if (osmo_timer_pending(&timer))
		osmo_timer_del(&timer);
}

static void start_timer(int sec, int micro)
{
	stop_timer();
	timer.cb = timeout_cb;
	timer.data = ms;
	osmo_timer_schedule(&timer, sec, micro);
}

static void start_rach(void)
{
	struct gsm48_sysinfo *s = &sysinfo;
	uint8_t chan_req_val, chan_req_mask;
	struct msgb *nmsg;
	struct abis_rsl_cchan_hdr *ncch;

	if (rach_count == RACH_MAX) {
		log_sysinfo();
		start_sync();
		return;
	}

	state = SCAN_STATE_RACH;

	if (s->neci) {
		chan_req_mask = 0x0f;
		chan_req_val = 0x01;
		LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x "
			"(OTHER with NECI)\n", chan_req_val);
	} else {
		chan_req_mask = 0x1f;
		chan_req_val = 0xe0;
		LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (OTHER no NECI)\n",
			chan_req_val);
	}

	rach_ref.valid = 0;
	rach_ref.cr = random();
	rach_ref.cr &= chan_req_mask;
	rach_ref.cr |= chan_req_val;

	nmsg = msgb_alloc_headroom(RSL_ALLOC_SIZE+RSL_ALLOC_HEADROOM,
		RSL_ALLOC_HEADROOM, "GSM 04.06 RSL");
	if (!nmsg)
		return;
	nmsg->l2h = nmsg->data;
	ncch = (struct abis_rsl_cchan_hdr *) msgb_put(nmsg, sizeof(*ncch)
							+ 4 + 2 + 2);
	rsl_init_cchan_hdr(ncch, RSL_MT_CHAN_RQD);
	ncch->chan_nr = RSL_CHAN_RACH;
	ncch->data[0] = RSL_IE_REQ_REFERENCE;
	ncch->data[1] = rach_ref.cr;
	ncch->data[2] = (s->ccch_conf == 1) << 7;
	ncch->data[3] = 0;
	ncch->data[4] = RSL_IE_ACCESS_DELAY;
	ncch->data[5] = 0; /* no delay */ 
	ncch->data[6] = RSL_IE_MS_POWER;
	ncch->data[7] = 0; /* full power */

	start_timer(RACH_WAIT);

	lapdm_rslms_recvmsg(nmsg, &ms->lapdm_channel);
}

static void start_sync(void)
{
	int rxlev_dbm = -128;
	int i, dist = 0;
	char dist_str[32] = "";

	arfcn = 0xffff;
	for (i = 0; i <= 1023; i++) {
		if ((pm[i].flags & INFO_FLG_PM)
		 && !(pm[i].flags & INFO_FLG_SYNC)) {
			if (pm[i].rxlev_dbm > rxlev_dbm) {
				rxlev_dbm = pm[i].rxlev_dbm;
				arfcn = i;
			}
		}
	}
	/* if GPS becomes valid, like after exitting a tunnel */
	if (!pm_gps_valid && g.valid) {
		pm_gps_valid = 1;
		geo2space(&pm_gps_x, &pm_gps_y, &pm_gps_z, g.longitude,
			g.latitude);
	}
	if (pm_gps_valid && g.valid) {
		double x, y, z;

		geo2space(&x, &y, &z, g.longitude, g.latitude);
		dist = distinspace(pm_gps_x, pm_gps_y, pm_gps_z, x, y, z);
		sprintf(dist_str, "  dist %d", (int)dist);
	}
	if (dist > MAX_DIST || arfcn == 0xffff || rxlev_dbm < min_rxlev_dbm) {
		memset(pm, 0, sizeof(pm));
		pm_index = 0;
		sync_count = 0;
		start_pm();
		return;
	}
	pm[arfcn].flags |= INFO_FLG_SYNC;
	LOGP(DSUM, LOGL_INFO, "Sync ARFCN %d (rxlev %d, %d syncs left)%s\n",
		arfcn, pm[arfcn].rxlev_dbm, sync_count--, dist_str);
	memset(&sysinfo, 0, sizeof(sysinfo));
	sysinfo.arfcn = arfcn;
	state = SCAN_STATE_SYNC;
	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	l1ctl_tx_fbsb_req(ms, arfcn, L1CTL_FBSB_F_FB01SB, 100, 0,
		CCCH_MODE_NONE, dbm2rxlev(pm[arfcn].rxlev_dbm));
}

static void start_pm(void)
{
	uint16_t from, to;

	state = SCAN_STATE_PM;
	from = (*band_range)[pm_index][0];
	to = (*band_range)[pm_index][1];

	if (from == 0 && to == 0) {
		LOGP(DSUM, LOGL_INFO, "Measurement done\n");
		pm_gps_valid = g.enable && g.valid;
		if (pm_gps_valid)
			geo2space(&pm_gps_x, &pm_gps_y, &pm_gps_z,
				g.longitude, g.latitude);
		log_pm();
		start_sync();
		return;
	}
	LOGP(DSUM, LOGL_INFO, "Measure from %d to %d\n", from, to);
	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	l1ctl_tx_pm_req_range(ms, from, to);
}

static int signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct osmobb_meas_res *mr;
	struct osmobb_fbsb_res *fr;
	uint16_t index;

	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_PM_RES:
		mr = signal_data;
		index = mr->band_arfcn & 0x3ff;
		pm[index].flags |= INFO_FLG_PM;
		pm[index].rxlev_dbm = mr->rx_lev - 110;
		if (pm[index].rxlev_dbm >= min_rxlev_dbm)
			sync_count++;
//		printf("rxlev %d = %d (sync_count %d)\n", index, pm[index].rxlev_dbm, sync_count);
		break;
	case S_L1CTL_PM_DONE:
		pm_index++;
		start_pm();
		break;
	case S_L1CTL_FBSB_RESP:
		fr = signal_data;
		sysinfo.bsic = fr->bsic;
		state = SCAN_STATE_READ;
		memset(&ms->meas, 0, sizeof(ms->meas));
		memset(&log_si, 0, sizeof(log_si));
		log_si.flags |= INFO_FLG_SYNC;
		log_si.ta = 0xff; /* invalid */
		start_timer(READ_WAIT);
		LOGP(DRR, LOGL_INFO, "Synchronized, start reading\n");
		break;
	case S_L1CTL_FBSB_ERR:
		LOGP(DRR, LOGL_INFO, "Sync failed\n");
		start_sync();
		break;
	case S_L1CTL_RESET:
		if (started)
			break;
		started = 1;
		memset(pm, 0, sizeof(pm));
		pm_index = 0;
		sync_count = 0;
		start_pm();
	}
	return 0;
}

static int ta_result(uint8_t ta)
{
	stop_timer();

	if (ta == 0xff)
		LOGP(DSUM, LOGL_INFO, "Got assignment reject\n");
	else {
		LOGP(DSUM, LOGL_DEBUG, "Got assignment TA = %d\n", ta);
		log_si.ta = ta;
	}

	log_sysinfo();
	start_sync();

	return 0;
}

/* match request reference agains request */
static int match_ra(struct osmocom_ms *ms, struct gsm48_req_ref *ref)
{
	uint8_t ia_t1, ia_t2, ia_t3;

	/* filter confirmed RACH requests only */
	if (rach_ref.valid && ref->ra == rach_ref.cr) {
	 	ia_t1 = ref->t1;
	 	ia_t2 = ref->t2;
	 	ia_t3 = (ref->t3_high << 3) | ref->t3_low;
		if (ia_t1 == rach_ref.t1 && ia_t2 == rach_ref.t2
			 && ia_t3 == rach_ref.t3) {
	 		LOGP(DRR, LOGL_INFO, "request %02x matches "
				"(fn=%d,%d,%d)\n", ref->ra, ia_t1, ia_t2,
					ia_t3);
			return 1;
		} else
	 		LOGP(DRR, LOGL_INFO, "request %02x matches but not "
				"frame number (IMM.ASS fn=%d,%d,%d != RACH "
				"fn=%d,%d,%d)\n", ref->ra, ia_t1, ia_t2, ia_t3,
				rach_ref.t1, rach_ref.t2, rach_ref.t3);
	}

	return 0;
}

/* 9.1.18 IMMEDIATE ASSIGNMENT is received */
static int imm_ass(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_imm_ass *ia = msgb_l3(msg);

	LOGP(DRR, LOGL_INFO, "IMMEDIATE ASSIGNMENT:\n");

	if (state != SCAN_STATE_RACH) {
		LOGP(DRR, LOGL_INFO, "Not for us, no request.\n");
		return 0;
	}

	/* request ref */
	if (match_ra(ms, &ia->req_ref)) {
		return ta_result(ia->timing_advance);
	}
	LOGP(DRR, LOGL_INFO, "Request, but not for us.\n");

	return 0;
}

/* 9.1.19 IMMEDIATE ASSIGNMENT EXTENDED is received */
static int imm_ass_ext(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_imm_ass_ext *ia = msgb_l3(msg);

	LOGP(DRR, LOGL_INFO, "IMMEDIATE ASSIGNMENT EXTENDED:\n");

	if (state != SCAN_STATE_RACH) {
		LOGP(DRR, LOGL_INFO, "Not for us, no request.\n");
		return 0;
	}

	/* request ref 1 */
	if (match_ra(ms, &ia->req_ref1)) {
		return ta_result(ia->timing_advance1);
	}
	/* request ref 2 */
	if (match_ra(ms, &ia->req_ref2)) {
		return ta_result(ia->timing_advance2);
	}
	LOGP(DRR, LOGL_INFO, "Request, but not for us.\n");

	return 0;
}

/* 9.1.20 IMMEDIATE ASSIGNMENT REJECT is received */
static int imm_ass_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_imm_ass_rej *ia = msgb_l3(msg);
	int i;
	struct gsm48_req_ref *req_ref;

	LOGP(DRR, LOGL_INFO, "IMMEDIATE ASSIGNMENT REJECT:\n");

	if (state != SCAN_STATE_RACH) {
		LOGP(DRR, LOGL_INFO, "Not for us, no request.\n");
		return 0;
	}

	for (i = 0; i < 4; i++) {
		/* request reference */
		req_ref = (struct gsm48_req_ref *)
				(((uint8_t *)&ia->req_ref1) + i * 4);
		LOGP(DRR, LOGL_INFO, "IMMEDIATE ASSIGNMENT REJECT "
			"(ref 0x%02x)\n", req_ref->ra);
		if (match_ra(ms, req_ref)) {
			return ta_result(0xff);
		}
	}

	return 0;
}

/* receive CCCH at RR layer */
static int pch_agch(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_header *sih = msgb_l3(msg);

	switch (sih->system_information) {
	case GSM48_MT_RR_PAG_REQ_1:
	case GSM48_MT_RR_PAG_REQ_2:
	case GSM48_MT_RR_PAG_REQ_3:
		return 0;
	case GSM48_MT_RR_IMM_ASS:
		return imm_ass(ms, msg);
	case GSM48_MT_RR_IMM_ASS_EXT:
		return imm_ass_ext(ms, msg);
	case GSM48_MT_RR_IMM_ASS_REJ:
		return imm_ass_rej(ms, msg);
	default:
		return -EINVAL;
	}
}

/* check if sysinfo is complete, change to RACH state */
static int new_sysinfo(void)
{
	struct gsm48_sysinfo *s = &sysinfo;

	/* restart timer */
	start_timer(READ_WAIT);

	/* mandatory */
	if (!s->si1 || !s->si2 || !s->si3 || !s->si4) {
		LOGP(DRR, LOGL_INFO, "not all mandatory SI received\n");
		return 0;
	}

	/* extended band */
	if (s->nb_ext_ind_si2 && !s->si2bis) {
		LOGP(DRR, LOGL_INFO, "extended ba, but si2bis not received\n");
		return 0;
	}

	/* 2ter */
	if (s->si2ter_ind && !s->si2ter) {
		LOGP(DRR, LOGL_INFO, "si2ter_ind, but si2ter not received\n");
		return 0;
	}

	LOGP(DRR, LOGL_INFO, "Sysinfo complete\n");

	stop_timer();

	rach_count = 0;
	start_rach();

	return 0;
}

/* receive BCCH at RR layer */
static int bcch(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_sysinfo *s = &sysinfo;
	struct gsm48_system_information_type_header *sih = msgb_l3(msg);
	uint8_t ccch_mode;

	if (msgb_l3len(msg) != 23) {
		LOGP(DRR, LOGL_NOTICE, "Invalid BCCH message length\n");
		return -EINVAL;
	}
	switch (sih->system_information) {
	case GSM48_MT_RR_SYSINFO_1:
		if (!memcmp(sih, s->si1_msg, sizeof(s->si1_msg)))
			return 0;
		LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 1\n");
		gsm48_decode_sysinfo1(s,
			(struct gsm48_system_information_type_1 *) sih,
			msgb_l3len(msg));
		return new_sysinfo();
	case GSM48_MT_RR_SYSINFO_2:
		if (!memcmp(sih, s->si2_msg, sizeof(s->si2_msg)))
			return 0;
		LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 2\n");
		gsm48_decode_sysinfo2(s,
			(struct gsm48_system_information_type_2 *) sih,
			msgb_l3len(msg));
		return new_sysinfo();
	case GSM48_MT_RR_SYSINFO_2bis:
		if (!memcmp(sih, s->si2b_msg, sizeof(s->si2b_msg)))
			return 0;
		LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 2bis\n");
		gsm48_decode_sysinfo2bis(s,
			(struct gsm48_system_information_type_2bis *) sih,
			msgb_l3len(msg));
		return new_sysinfo();
	case GSM48_MT_RR_SYSINFO_2ter:
		if (!memcmp(sih, s->si2t_msg, sizeof(s->si2t_msg)))
			return 0;
		LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 2ter\n");
		gsm48_decode_sysinfo2ter(s,
			(struct gsm48_system_information_type_2ter *) sih,
			msgb_l3len(msg));
		return new_sysinfo();
	case GSM48_MT_RR_SYSINFO_3:
		if (!memcmp(sih, s->si3_msg, sizeof(s->si3_msg)))
			return 0;
		LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 3\n");
		gsm48_decode_sysinfo3(s,
			(struct gsm48_system_information_type_3 *) sih,
			msgb_l3len(msg));
		ccch_mode = (s->ccch_conf == 1) ? CCCH_MODE_COMBINED :
			CCCH_MODE_NON_COMBINED;
		LOGP(DRR, LOGL_INFO, "Changing CCCH_MODE to %d\n", ccch_mode);
		l1ctl_tx_ccch_mode_req(ms, ccch_mode);
		return new_sysinfo();
	case GSM48_MT_RR_SYSINFO_4:
		if (!memcmp(sih, s->si4_msg, sizeof(s->si4_msg)))
			return 0;
		LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 4\n");
		gsm48_decode_sysinfo4(s,
			(struct gsm48_system_information_type_4 *) sih,
			msgb_l3len(msg));
		return new_sysinfo();
	default:
		return -EINVAL;
	}
}

static int unit_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	struct tlv_parsed tv;
	uint8_t ch_type, ch_subch, ch_ts;
	
	DEBUGP(DRSL, "RSLms UNIT DATA IND chan_nr=0x%02x link_id=0x%02x\n",
		rllh->chan_nr, rllh->link_id);

	rsl_tlv_parse(&tv, rllh->data, msgb_l2len(msg)-sizeof(*rllh));
	if (!TLVP_PRESENT(&tv, RSL_IE_L3_INFO)) {
		DEBUGP(DRSL, "UNIT_DATA_IND without L3 INFO ?!?\n");
		return -EIO;
	}
	msg->l3h = (uint8_t *) TLVP_VAL(&tv, RSL_IE_L3_INFO);

	if (state != SCAN_STATE_READ && state != SCAN_STATE_RACH) {
	 	return -EINVAL;
	}

	rsl_dec_chan_nr(rllh->chan_nr, &ch_type, &ch_subch, &ch_ts);
	switch (ch_type) {
	case RSL_CHAN_PCH_AGCH:
		return pch_agch(ms, msg);
	case RSL_CHAN_BCCH:
		return bcch(ms, msg);
#if 0
	case RSL_CHAN_Bm_ACCHs:
	case RSL_CHAN_Lm_ACCHs:
	case RSL_CHAN_SDCCH4_ACCH:
	case RSL_CHAN_SDCCH8_ACCH:
		return rx_acch(ms, msg);
#endif
	default:
		LOGP(DRSL, LOGL_NOTICE, "RSL with chan_nr 0x%02x unknown.\n",
			rllh->chan_nr);
		return -EINVAL;
	}
}

static int rcv_rll(struct osmocom_ms *ms, struct msgb *msg)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	int msg_type = rllh->c.msg_type;

	if (msg_type == RSL_MT_UNIT_DATA_IND) {
		unit_data_ind(ms, msg);
	} else
		LOGP(DRSL, LOGL_NOTICE, "RSLms message unhandled\n");

	msgb_free(msg);

	return 0;
}

int chan_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct abis_rsl_cchan_hdr *ch = msgb_l2(msg);
	struct gsm48_req_ref *ref = (struct gsm48_req_ref *) (ch->data + 1);

	if (msgb_l2len(msg) < sizeof(*ch) + sizeof(*ref)) {
		LOGP(DRR, LOGL_ERROR, "CHAN_CNF too slort\n");
		return -EINVAL;
	}

	rach_ref.valid = 1;
	rach_ref.t1 = ref->t1;
	rach_ref.t2 = ref->t2;
	rach_ref.t3 = ref->t3_low | (ref->t3_high << 3);

	return 0;
}

static int rcv_cch(struct osmocom_ms *ms, struct msgb *msg)
{
	struct abis_rsl_cchan_hdr *ch = msgb_l2(msg);
	int msg_type = ch->c.msg_type;
	int rc;

	LOGP(DRSL, LOGL_INFO, "Received '%s' from layer1\n",
		rsl_msg_name(msg_type));

	if (state == SCAN_STATE_RACH && msg_type == RSL_MT_CHAN_CONF) {
	 	rc = chan_conf(ms, msg);
		msgb_free(msg);
		return rc;
	}

	LOGP(DRSL, LOGL_NOTICE, "RSLms message unhandled\n");
	msgb_free(msg);
	return 0;
}

static int rcv_rsl(struct msgb *msg, struct lapdm_entity *le, void *l3ctx)
{
	struct osmocom_ms *ms = l3ctx;
	struct abis_rsl_common_hdr *rslh = msgb_l2(msg);
	int rc = 0;

	switch (rslh->msg_discr & 0xfe) {
	case ABIS_RSL_MDISC_RLL:
		rc = rcv_rll(ms, msg);
		break;
	case ABIS_RSL_MDISC_COM_CHAN:
		rc = rcv_cch(ms, msg);
		break;
	default:
		LOGP(DRSL, LOGL_NOTICE, "unknown RSLms msg_discr 0x%02x\n",
			rslh->msg_discr);
		msgb_free(msg);
		rc = -EINVAL;
		break;
	}

	return rc;
}

int scan_init(struct osmocom_ms *_ms)
{
	ms = _ms;
	osmo_signal_register_handler(SS_L1CTL, &signal_cb, NULL);
	memset(&timer, 0, sizeof(timer));
	lapdm_channel_set_l3(&ms->lapdm_channel, &rcv_rsl, ms);
	g.enable = 1;
	osmo_gps_init();
	if (osmo_gps_open())
		g.enable = 0;

	if (!strcmp(logname, "-"))
		logfp = stdout;
	else
		logfp = fopen(logname, "a");
	if (!logfp) {
		fprintf(stderr, "Failed to open logfile '%s'\n", logname);
		scan_exit();
		return -errno;
	}
	LOGP(DSUM, LOGL_INFO, "Scanner initialized\n");

	return 0;
}

int scan_exit(void)
{
	LOGP(DSUM, LOGL_INFO, "Scanner exit\n");
	if (g.valid)
		osmo_gps_close();
	if (logfp)
		fclose(logfp);
	osmo_signal_unregister_handler(SS_L1CTL, &signal_cb, NULL);
	stop_timer();

	return 0;
}
