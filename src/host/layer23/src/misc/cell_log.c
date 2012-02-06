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

#define READ_WAIT	5, 0
#define IMM_WAIT	60, 0
#define RACH_WAIT	0, 900000
#define MIN_RXLEV	-90
#define MAX_DIST	2000
#define NUM_SYSINFO	30

extern int wait_time;
extern int log_gprs;
extern int scan_only;
extern char *scan_band;

#include "cellid.c"

enum {
	SCAN_STATE_PM,
	SCAN_STATE_SYNC,
	SCAN_STATE_BCCH,
	SCAN_STATE_READY,
	SCAN_STATE_RACH,
};

/* ranges of bands */
static uint16_t *band_range = 0;
static uint16_t range_all[] = {0, 124, 128, 251, 512, 885, 955, 1023, 0, 0};
static uint16_t range_900[] = {0, 124, 955, 1023, 0, 0};
static uint16_t range_1800[] = {512, 885, 0, 0};
static uint16_t range_850[] = {128, 251, 0, 0};
static uint16_t range_1900[] = {512|ARFCN_PCS, 810|ARFCN_PCS, 0, 0};

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
	int8_t rxlev;
} pm[1024];

enum dch_state_t {
	DCH_NONE,
	DCH_WAIT_EST,
	DCH_ACTIVE,
	DCH_WAIT_REL,
};

static struct {
	uint8_t *		si[NUM_SYSINFO];
	int			ccch_mode;

	enum dch_state_t	dch_state;
	uint8_t			dch_nr;
	int			dch_badcnt;

	FILE *			fh;

	struct gsm_sysinfo_freq	cell_arfcns[1024];
} app_state;

static int sdcch = 0;
static int got_data = 0;
static int stop_raw = 0;
static int collecting = 0;
static int short_count;
static int started = 0;
static int state;
static int8_t min_rxlev = MIN_RXLEV;
static int sync_count;
static int sync_retry;
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
	int8_t rxlev;
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

static int gsm48_rx_imm_ass(struct msgb *msg, struct osmocom_ms *ms);

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
			LOGFILE(" %d", pm[i].rxlev);
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
	int8_t rxlev;
	char ta_str[32] = "";

	if (log_si.ta != 0xff)
		sprintf(ta_str, " TA=%d", log_si.ta);

	rxlev = meas->rxlev / meas->frames - 110;
	LOGP(DSUM, LOGL_INFO, "Cell: ARFCN=%d PWR=%ddB MCC=%s MNC=%s (%s, %s)%s\n",
		arfcn, rxlev, gsm_print_mcc(s->mcc), gsm_print_mnc(s->mnc),
		gsm_get_mcc(s->mcc), gsm_get_mnc(s->mcc, s->mnc), ta_str);

	LOGFILE("[sysinfo]\n");
	LOGFILE("arfcn %d\n", s->arfcn);
	log_time();
	log_gps();
	LOGFILE("bsic %d,%d\n", s->bsic >> 3, s->bsic & 7);
	LOGFILE("rxlev %d\n", rxlev);
	LOGFILE("mcc %s mnc %s\n", gsm_print_mcc(s->mcc), gsm_print_mnc(s->mnc));
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
	if (sdcch) {
		stop_raw = 1;
	}
	switch (state) {
	case SCAN_STATE_BCCH:
		LOGP(DRR, LOGL_INFO, "Timeout reading BCCH\n");
	case SCAN_STATE_READY:
		printf("Timeout\n");
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
		chan_req_val = 0xf0;
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
	int rxlev = -128;
	int i, dist = 0;
	char dist_str[32] = "";

	if (sdcch)
		return;
	arfcn = 0xffff;
	for (i = 0; i <= 1023; i++) {
		if ((pm[i].flags & INFO_FLG_PM)
		 && !(pm[i].flags & INFO_FLG_SYNC)) {
			if (pm[i].rxlev > rxlev) {
				rxlev = pm[i].rxlev;
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
	if (dist > MAX_DIST || arfcn == 0xffff || rxlev < min_rxlev) {
		if (scan_only) {
			scan_exit();
			exit(0);
		}
		memset(pm, 0, sizeof(pm));
		pm_index = 0;
		printf("end of sync\n");
		wait_time *= 2;
		start_pm();
		return;
	}
	pm[arfcn].flags |= INFO_FLG_SYNC;
	memset(&sysinfo, 0, sizeof(sysinfo));
	sysinfo.arfcn = arfcn;
	state = SCAN_STATE_SYNC;
	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	stop_raw = 0;
	collecting = 0;
	sync_retry = 0;
	reset_cid();
	memset(&app_state.cell_arfcns, 0x00, sizeof(app_state.cell_arfcns));
	got_data = (log_gprs ? 0 : 1);
	if (!scan_only)
		printf("\nARFCN %d: tuning\n", arfcn);
	l1ctl_tx_fbsb_req(ms, arfcn, L1CTL_FBSB_F_FB01SB, 100, 0,
		CCCH_MODE_NONE);
}

static void start_pm(void)
{
	uint16_t from, to;

	state = SCAN_STATE_PM;
	from = band_range[2*pm_index+0];
	to = band_range[2*pm_index+1];

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
        struct osmobb_msg_ind *mi;
	struct osmocom_ms *ms;
	static unsigned loss_count = 0;

	uint16_t index;

	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_PM_RES:
		mr = signal_data;
		index = mr->band_arfcn & 0x3ff;
		pm[index].flags |= INFO_FLG_PM;
		pm[index].rxlev = mr->rx_lev - 110;
		if (pm[index].rxlev >= min_rxlev)
			sync_count++;
//		printf("rxlev %d = %d (sync_count %d)\n", index, pm[index].rxlev, sync_count);
		break;
        case S_L1CTL_BURST_IND:
                mi = signal_data;
                layer3_rx_burst(mi->ms, mi->msg);
                break;
	case S_L1CTL_PM_DONE:
		pm_index++;
		start_pm();
		break;
	case S_L1CTL_FBSB_RESP:
		loss_count = 0;
		if (sdcch)
			break;
		if (!scan_only)
			printf("ARFCN %d: got sync\n", arfcn);
		if (state < SCAN_STATE_BCCH) {
			state = SCAN_STATE_BCCH;
			fr = signal_data;
			ms = fr->ms;
			sysinfo.bsic = fr->bsic;
			memset(&ms->meas, 0, sizeof(ms->meas));
			memset(&log_si, 0, sizeof(log_si));
			log_si.flags |= INFO_FLG_SYNC;
			log_si.ta = 0xff; /* invalid */
		}
		if (!collecting) {
			start_timer(READ_WAIT);
		}
		LOGP(DRR, LOGL_INFO, "Synchronized, start reading\n");
		break;
	case S_L1CTL_FBSB_ERR:
		if (scan_only || (state < SCAN_STATE_BCCH)) {
			start_sync();
			break;
		}
		if (sync_retry < wait_time/60) {
			printf("ARFCN %d: resync\n", arfcn);
			sync_retry++;
			fr = signal_data;
			ms = fr->ms;
			l1ctl_tx_dm_rel_req(ms);
			return l1ctl_tx_fbsb_req(ms, arfcn,
		                         L1CTL_FBSB_F_FB01SB, 100, 0,
		                         CCCH_MODE_NONE);
		}
		break;
	case S_L1CTL_RESET:
		if (sdcch) {
			printf("RESET! while sdcch\n");
		} else {
			if (started)
				break;
			started = 1;
			loss_count = 0;
			memset(pm, 0, sizeof(pm));
			pm_index = 0;
			start_pm();
		}
		break;
        case S_L1CTL_LOSS_IND:
		if (sdcch)
			break;
		loss_count++;
                ms = signal_data;
		if (loss_count > 10) {
			loss_count = 0;
			if (sync_retry > 3) {
				start_sync();
			} else {
				sync_retry++;
				l1ctl_tx_dm_rel_req(ms);
				sleep(1);
				return l1ctl_tx_fbsb_req(ms, arfcn,
						L1CTL_FBSB_F_FB01SB, 100, 0,
						CCCH_MODE_NONE);
			}
                }
                break;
	}

	return 0;
}

/* receive CCCH at RR layer */
static int pch_agch(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_header *sih = msgb_l3(msg);

	if (sdcch || (state != SCAN_STATE_READY))
		return 0;

	switch (sih->system_information) {
	case GSM48_MT_RR_PAG_REQ_1:
	case GSM48_MT_RR_PAG_REQ_2:
	case GSM48_MT_RR_PAG_REQ_3:
		return 0;
	case GSM48_MT_RR_IMM_ASS:
//		return imm_ass(ms, msg);
		return gsm48_rx_imm_ass(msg, ms);
	case GSM48_MT_RR_IMM_ASS_EXT:
//		return imm_ass_ext(ms, msg);
	case GSM48_MT_RR_IMM_ASS_REJ:
//		return imm_ass_rej(ms, msg);
	default:
		return -EINVAL;
	}
}

/* check if sysinfo is complete, change to RACH state */
static int new_sysinfo(void)
{
	struct gsm48_sysinfo *s = &sysinfo;

	/* restart timer */
	//start_timer(READ_WAIT);

	if (s->si3 && scan_only) {
		log_sysinfo();
		start_sync();
	}

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

	log_sysinfo();

	state = SCAN_STATE_READY;

	start_timer(IMM_WAIT);

	return 0;
}

/* receive BCCH at RR layer */
static int bcch(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_sysinfo *s = &sysinfo;
	struct gsm48_system_information_type_header *sih = msgb_l3(msg);
	struct gsm48_system_information_type_1 *si1 =
		(struct gsm48_system_information_type_1 *) msgb_l3(msg);
        struct gsm48_system_information_type_3 *si3 =
                (struct gsm48_system_information_type_3 *) msgb_l3(msg);
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
                gsm48_decode_freq_list(app_state.cell_arfcns,
                                       si1->cell_channel_description,
                                       sizeof(si1->cell_channel_description),
                                       0xff, 0x01);
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
		set_cid(si3->lai.digits, si3->lai.lac, si3->cell_identity);
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

	if (state < SCAN_STATE_BCCH && state != SCAN_STATE_RACH) {
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

void set_freq_range()
{
	if (!scan_band) {
		band_range = range_all;
		return;
	}
	if (!strcmp(scan_band, "900")) {
		band_range = range_900;
		return;
	}
	if (!strcmp(scan_band, "1800")) {
		band_range = range_1800;
		return;
	}
	if (!strcmp(scan_band, "850")) {
		band_range = range_850;
		return;
	}
	if (!strcmp(scan_band, "1900")) {
		band_range = range_1900;
		return;
	}
	printf("Bad frequency range\n");
	exit(-1);
}

int scan_init(struct osmocom_ms *_ms)
{
	ms = _ms;
	osmo_signal_register_handler(SS_L1CTL, &signal_cb, NULL);
	memset(&timer, 0, sizeof(timer));
	lapdm_channel_set_l3(&ms->lapdm_channel, &rcv_rsl, ms);
	g.enable = 1;
	sdcch = 0;
	set_freq_range();
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

static int gsm48_rx_imm_ass(struct msgb *msg, struct osmocom_ms *ms)
{
	struct gsm48_imm_ass *ia = msgb_l3(msg);
	uint8_t ch_type, ch_subch, ch_ts;
	int rv;

	/* If we're busy ... */
	if (app_state.dch_state != DCH_NONE)
		return 0;

	/* Discard packet TBF assignement */
	if (ia->page_mode & 0xf0) {
		if (got_data) {
			return 0;
		} else {
			got_data = 1;
			printf("ARFCN %d: got DATA assignment\n", arfcn);
		}
	} else {
		printf("ARFCN %d: got SDCCH assignment\n", arfcn);
	}

	rsl_dec_chan_nr(ia->chan_desc.chan_nr, &ch_type, &ch_subch, &ch_ts);

	if (!ia->chan_desc.h0.h) {
		/* Non-hopping */
		uint16_t _arfcn;

		_arfcn = ia->chan_desc.h0.arfcn_low | (ia->chan_desc.h0.arfcn_high << 8);
		if (arfcn != _arfcn)
			printf("ARFCN %d: not jumping to ARFCN %d!\n", arfcn, _arfcn);

		DEBUGP(DRR, "GSM48 IMM ASS (ra=0x%02x, chan_nr=0x%02x, "
			"ARFCN=%u, TS=%u, SS=%u, TSC=%u) ", ia->req_ref.ra,
			ia->chan_desc.chan_nr, arfcn, ch_ts, ch_subch,
			ia->chan_desc.h0.tsc);

		/* request L1 to go to dedicated mode on assigned channel */
		rv = l1ctl_tx_dm_est_req_h0(ms,
			arfcn, ia->chan_desc.chan_nr, ia->chan_desc.h0.tsc,
			GSM48_CMODE_SIGN, 0);
	} else {
		/* Hopping */
		uint8_t maio, hsn, ma_len;
		uint16_t ma[64], arfcn;
		int i, j, k;

		hsn = ia->chan_desc.h1.hsn;
		maio = ia->chan_desc.h1.maio_low | (ia->chan_desc.h1.maio_high << 2);

		DEBUGP(DRR, "GSM48 IMM ASS (ra=0x%02x, chan_nr=0x%02x, "
			"HSN=%u, MAIO=%u, TS=%u, SS=%u, TSC=%u) ", ia->req_ref.ra,
			ia->chan_desc.chan_nr, hsn, maio, ch_ts, ch_subch,
			ia->chan_desc.h1.tsc);

		/* decode mobile allocation */
		ma_len = 0;
		for (i=1, j=0; i<=1024; i++) {
			arfcn = i & 1023;
			if (app_state.cell_arfcns[arfcn].mask & 0x01) {
				k = ia->mob_alloc_len - (j>>3) - 1;
				if (ia->mob_alloc[k] & (1 << (j&7))) {
					ma[ma_len++] = arfcn;
				}
				j++;
			}
		}

		if (!ma_len) {
			if (ia->page_mode & 0xf0)
				got_data = 0;
			return -1;
		}

		/* request L1 to go to dedicated mode on assigned channel */
		rv = l1ctl_tx_dm_est_req_h1(ms,
			maio, hsn, ma, ma_len,
			ia->chan_desc.chan_nr, ia->chan_desc.h1.tsc,
			GSM48_CMODE_SIGN, 0);
	}

	DEBUGPC(DRR, "\n");

	/* Set state */
	app_state.dch_state = DCH_WAIT_EST;
	app_state.dch_nr = ia->chan_desc.chan_nr;
	app_state.dch_badcnt = 0;

	sdcch = 1;
	if (!collecting) {
		collecting = 1;
		short_count = 0;
		start_timer(wait_time, 0);
	}

	return rv;
}

static char *
gen_filename(struct osmocom_ms *ms, struct l1ctl_burst_ind *bi)
{
	static char buffer[256];
	time_t d;
	struct tm lt;

	time(&d);
	localtime_r(&d, &lt);

	snprintf(buffer, 256, "%s_%04d%02d%02d_%02d%02d_%d_%d_%02x.dat",
		get_cid_str(),
		lt.tm_year + 1900, lt.tm_mon, lt.tm_mday,
		lt.tm_hour, lt.tm_min,
		arfcn,
		ntohl(bi->frame_nr),
		bi->chan_nr
	);

	return buffer;
}

void layer3_rx_burst(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_burst_ind *bi;
	int16_t rx_dbm;
	uint16_t l_arfcn;
	int ul, do_rel=0;
	static unsigned rcv_frames;
	static unsigned rcv_snr;

	if (!sdcch)
		return;

	/* Header handling */
	bi = (struct l1ctl_burst_ind *) msg->l1h;

	l_arfcn = ntohs(bi->band_arfcn);
	rx_dbm = rxlev2dbm(bi->rx_level);
	ul = !!(l_arfcn & ARFCN_UPLINK);

	/* Check for channel start */
	if (app_state.dch_state == DCH_WAIT_EST) {
		if (bi->chan_nr == app_state.dch_nr) {
			if (bi->snr > 100) {
				/* Change state */
				app_state.dch_state = DCH_ACTIVE;
				app_state.dch_badcnt = 0;
				rcv_frames = 0;
				rcv_snr = 0;
				printf("ARFCN %d: session begin\n", arfcn);

				/* Open output */
				app_state.fh = fopen(gen_filename(ms, bi), "wb");
			} else {
				/* Abandon ? */
				//do_rel = (app_state.dch_badcnt++) >= 8;
			}
		}
	}

	/* Check for channel end */
	if (app_state.dch_state == DCH_ACTIVE) {
		if (!ul) {
			/* Bad burst counting */
			if (bi->snr < 100)
				app_state.dch_badcnt++;
			else if (app_state.dch_badcnt >= 2)
				app_state.dch_badcnt -= 2;
			else
				app_state.dch_badcnt = 0;

			/* Release condition */
			if (bi->chan_nr & 0xe0)
				do_rel = app_state.dch_badcnt >= 4;
		}

		if (!ul) {
			rcv_frames++;
			rcv_snr += bi->snr;

			/* capture up to 3 minutes */
			if (rcv_frames >= 40000)
				do_rel = 1;
		}

		/* Save the burst */
		fwrite(bi, sizeof(*bi), 1, app_state.fh);
	}

	/* SNR check */
	if (do_rel) {
		printf("ARFCN %d: session end, frames=%d SNR_avg=%d\n", arfcn, rcv_frames, rcv_snr/rcv_frames);
		if ((bi->chan_nr & 0xe0) && (rcv_frames < 20))
			short_count++;
		rcv_frames = 0;
		rcv_snr = 0;
		app_state.dch_state = DCH_NONE;
		app_state.dch_badcnt = 0;
		if (app_state.fh) {
			fclose(app_state.fh);
			app_state.fh = NULL;
		}
		sdcch = 0;
		if (stop_raw || (short_count > 3)) {
			collecting = 0;
			pm[arfcn].flags |= INFO_FLG_SYNC;
			start_sync();
		} else {
			l1ctl_tx_dm_rel_req(ms);
			l1ctl_tx_fbsb_req(ms, arfcn, L1CTL_FBSB_F_FB01SB, 100, 0, CCCH_MODE_NON_COMBINED);
		}
	}
}

void layer3_app_reset(void)
{
	/* Reset state */
	memset(app_state.si, 0, NUM_SYSINFO*sizeof(uint8_t*));
	app_state.ccch_mode = CCCH_MODE_NONE;
	app_state.dch_state = DCH_NONE;
	app_state.dch_badcnt = 0;

	if (app_state.fh)
		fclose(app_state.fh);
	app_state.fh = NULL;
}

