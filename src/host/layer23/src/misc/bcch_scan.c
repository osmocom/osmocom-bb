/* BCCH Scanning code for OsmocomBB */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
#include <errno.h>

#include <l1ctl_proto.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/msgb.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/gsm/rsl.h>

#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>

/* somewhere in 05.08 */
#define MAX_CELLS_IN_BA	32

/* Information about a single cell / BCCH */
struct cell_info {
	struct llist_head list;

	uint16_t band_arfcn;
	uint8_t bsic;
	uint8_t rxlev;

	struct {
		uint16_t mcc;	/* Mobile Country Code */
		uint16_t mnc;	/* Mobile Network Code */
		uint16_t lac;	/* Location Area Code */
		uint16_t rac;	/* Routing Area Code */
		uint16_t cid;	/* Cell ID */
	} id;
	uint16_t ba_arfcn[MAX_CELLS_IN_BA];
	uint8_t ba_arfcn_num;

	struct {
		int32_t fn_delta;	/* delta to current L1 fn */
		int16_t qbit_delta;
		int16_t afc_delta;
	} l1_sync;
};

#define AFS_F_PM_DONE	0x01
#define AFS_F_TESTED	0x02
#define AFS_F_BCCH	0x04
struct arfcn_state {
	uint8_t rxlev;
	uint8_t flags;
};

enum bscan_state {
	BSCAN_S_NONE,
	BSCAN_S_WAIT_DATA,
	BSCAN_S_DONE,
};

enum fps_state {
	FPS_S_NONE,
	FPS_S_PM_GSM900,
	FPS_S_PM_EGSM900,
	FPS_S_PM_GSM1800,
	FPS_S_BINFO,
};

struct full_power_scan {
	/* Full Power Scan */
	enum fps_state fps_state;
	struct arfcn_state arfcn_state[1024];

	struct osmocom_ms *ms;

	/* BCCH info part */
	enum bscan_state state;
	struct llist_head cell_list;
	struct cell_info *cur_cell;
	uint16_t cur_arfcn;
	struct osmo_timer_list timer;
};

static struct full_power_scan fps;

static int get_next_arfcn(struct full_power_scan *fps)
{
	unsigned int i;
	uint8_t best_rxlev = 0;
	int best_arfcn = -1;

	for (i = 0; i < ARRAY_SIZE(fps->arfcn_state); i++) {
		struct arfcn_state *af = &fps->arfcn_state[i];
		/* skip ARFCN's where we don't have a PM */
		if (!(af->flags & AFS_F_PM_DONE))
			continue;
		/* skip ARFCN's that we already tested */
		if (af->flags & AFS_F_TESTED)
			continue;
		/* if current arfcn_state is better than best so far,
		 * select it */
		if (af->rxlev > best_rxlev) {
			best_rxlev = af->rxlev;
			best_arfcn = i;
		}
	}
	printf("arfcn=%d rxlev=%u\n", best_arfcn, best_rxlev);
	return best_arfcn;
}

static struct cell_info *cell_info_alloc(void)
{
	struct cell_info *ci = talloc_zero(NULL, struct cell_info);
	return ci;
}

static void cell_info_free(struct cell_info *ci)
{
	talloc_free(ci);
}

/* start to scan for one ARFCN */
static int _cinfo_start_arfcn(unsigned int band_arfcn)
{
	int rc;

	/* ask L1 to try to tune to new ARFCN */
	/* FIXME: decode band */
	rc = l1ctl_tx_fbsb_req(fps.ms, band_arfcn,
	                       L1CTL_FBSB_F_FB01SB, 100, 0, CCCH_MODE_COMBINED,
			       fps.arfcn_state[band_arfcn].rxlev);
	if (rc < 0)
		return rc;

	/* allocate new cell info structure */
	fps.cur_cell = cell_info_alloc();
	fps.cur_arfcn = band_arfcn;
	fps.cur_cell->band_arfcn = band_arfcn;
	/* FIXME: start timer in case we never get a sync */
	fps.state = BSCAN_S_WAIT_DATA;
	osmo_timer_schedule(&fps.timer, 2, 0);

	return 0;
}


static void cinfo_next_cell(void *data)
{
	int rc;

	/* we've been waiting for BCCH info */
	fps.arfcn_state[fps.cur_arfcn].flags |= AFS_F_TESTED;
	/* if there is a BCCH, we need to add the collected BCCH
	 * information to our list */

	if (fps.arfcn_state[fps.cur_arfcn].flags & AFS_F_BCCH)
		llist_add(&fps.cur_cell->list, &fps.cell_list);
	else
		cell_info_free(fps.cur_cell);

	rc = get_next_arfcn(&fps);
	if (rc < 0) {
		fps.state = BSCAN_S_DONE;
		return;
	}
	/* start syncing to the next ARFCN */
	_cinfo_start_arfcn(rc);
}

static void cinfo_timer_cb(void *data)
{
	switch (fps.state) {
	case BSCAN_S_WAIT_DATA:
		cinfo_next_cell(data);
		break;
	}
}

/* Update cell_info for current cell with received BCCH info */
static int rx_bcch_info(const uint8_t *data)
{
	struct cell_info *ci = fps.cur_cell;
	struct gsm48_system_information_type_header *si_hdr;
	si_hdr = (struct gsm48_system_information_type_header *) data;;

	/* we definitely have a BCCH on this channel */
	fps.arfcn_state[ci->band_arfcn].flags |= AFS_F_BCCH;

	switch (si_hdr->system_information) {
	case GSM48_MT_RR_SYSINFO_1:
		/* FIXME: CA, RACH control */
		break;
	case GSM48_MT_RR_SYSINFO_2:
		/* FIXME: BA, NCC, RACH control */
		break;
	case GSM48_MT_RR_SYSINFO_3:
		/* FIXME: cell_id, LAI */
		break;
	case GSM48_MT_RR_SYSINFO_4:
		/* FIXME: LAI */
		break;
	}
	return 0;
}

/* Update L1/SCH information (AFC/QBIT/FN offset, BSIC) */
static int rx_sch_info()
{
	/* FIXME */
}

static int bscan_sig_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct cell_info *ci = fps.cur_cell;
	struct osmocom_ms *ms;
	struct osmobb_meas_res *mr;
	uint16_t arfcn;
	int rc;

	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_PM_RES:
		mr = signal_data;
		/* check if PM result is for same MS */
		if (fps.ms != mr->ms)
			return 0;
		arfcn = mr->band_arfcn & 0x3ff;
		/* update RxLev and notice that PM was done */
		fps.arfcn_state[arfcn].rxlev = mr->rx_lev;
		fps.arfcn_state[arfcn].flags |= AFS_F_PM_DONE;
		break;
	case S_L1CTL_PM_DONE:
		ms = signal_data;
		switch (fps.fps_state) {
		case FPS_S_PM_GSM900:
			fps.fps_state = FPS_S_PM_EGSM900;
			return l1ctl_tx_pm_req_range(ms, 955, 1023);
		case FPS_S_PM_EGSM900:
			fps.fps_state = FPS_S_PM_GSM1800;
			return l1ctl_tx_pm_req_range(ms, 512, 885);
		case FPS_S_PM_GSM1800:
			/* power measurement has finished, we can start
			 * to actually iterate over the ARFCN's and try
			 * to sync to BCCHs */
			fps.fps_state = FPS_S_BINFO;
			rc = get_next_arfcn(&fps);
			if (rc < 0) {
				fps.state = BSCAN_S_DONE;
				return 0;
			}
			_cinfo_start_arfcn(rc);
			break;
		}
		break;
	case S_L1CTL_FBSB_RESP:
		/* We actually got a FCCH/SCH burst */
#if 0
		fps.arfcn_state[ci->band_arfcn].flags |= AFS_F_BCCH;
		/* fallthrough */
#else
		break;
#endif
	case S_L1CTL_FBSB_ERR:
		/* We timed out, move on */
		if (fps.state == BSCAN_S_WAIT_DATA)
			cinfo_next_cell(NULL);
		break;
	}
	return 0;
}

/* start the full power scan */
int fps_start(struct osmocom_ms *ms)
{
	memset(&fps, 0, sizeof(fps));
	fps.ms = ms;

	fps.timer.cb = cinfo_timer_cb;
	fps.timer.data = &fps;

	/* Start by scanning the good old GSM900 band */
	fps.fps_state = FPS_S_PM_GSM900;
	return l1ctl_tx_pm_req_range(ms, 0, 124);
}

int fps_init(void)
{
	return osmo_signal_register_handler(SS_L1CTL, &bscan_sig_cb, NULL);
}
