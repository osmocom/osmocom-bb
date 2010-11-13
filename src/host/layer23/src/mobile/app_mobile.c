/* "Application" code of the layer2/3 stack */

/* (C) 2010 by Holger Hans Peter Freyther
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
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

#include <errno.h>
#include <signal.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1l2_interface.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/lapdm.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/mobile/gsm48_rr.h>
#include <osmocom/bb/mobile/vty.h>
#include <osmocom/vty/telnet_interface.h>

#include <osmocore/msgb.h>
#include <osmocore/talloc.h>
#include <osmocore/select.h>

int mncc_recv_mobile(struct osmocom_ms *ms, int msg_type, void *arg);
int mncc_recv_dummy(struct osmocom_ms *ms, int msg_type, void *arg);
extern int (*l23_app_exit) (struct osmocom_ms *ms, int force);

int mobile_work(struct osmocom_ms *ms)
{
	int work = 0, w;

	do {
		w = 0;
		w |= gsm48_rsl_dequeue(ms);
		w |= gsm48_rr_dequeue(ms);
		w |= gsm48_mmxx_dequeue(ms);
		w |= gsm48_mmr_dequeue(ms);
		w |= gsm48_mmevent_dequeue(ms);
		w |= gsm322_plmn_dequeue(ms);
		w |= gsm322_cs_dequeue(ms);
		w |= gsm_sim_job_dequeue(ms);
		w |= mncc_dequeue(ms);
		if (w)
			work = 1;
	} while (w);
	return work;
}

int mobile_signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms;
	struct gsm_settings *set;
	struct msgb *nmsg;

	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_RESET:
		ms = signal_data;
		set = &ms->settings;

		if (ms->started)
			break;

		/* insert test card, if enabled */
		switch (set->sim_type) {
		case GSM_SIM_TYPE_READER:
			/* trigger sim card reader process */
			gsm_subscr_simcard(ms);
			break;
		case GSM_SIM_TYPE_TEST:
			gsm_subscr_testcard(ms, set->test_rplmn_mcc,
						set->test_rplmn_mnc);
			break;
		default:
			/* no SIM, trigger PLMN selection process */
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_SWITCH_ON);
			if (!nmsg)
				return -ENOMEM;
			gsm322_plmn_sendmsg(ms, nmsg);
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_SWITCH_ON);
			if (!nmsg)
				return -ENOMEM;
			gsm322_cs_sendmsg(ms, nmsg);
		}

		ms->started = 1;
	}
	return 0;
}

int mobile_exit(struct osmocom_ms *ms, int force)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	if (!force && ms->started) {
		struct msgb *nmsg;

		ms->shutdown = 1; /* going down */
		nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_IMSI_DETACH);
		if (!nmsg)
			return -ENOMEM;
		gsm48_mmevent_msg(mm->ms, nmsg);

		return -EBUSY;
	}

	gsm322_exit(ms);
	gsm48_mm_exit(ms);
	gsm48_rr_exit(ms);
	gsm_subscr_exit(ms);
	gsm48_cc_exit(ms);
	gsm_sim_exit(ms);
	lapdm_exit(&ms->l2_entity.lapdm_acch);
	lapdm_exit(&ms->l2_entity.lapdm_dcch);

	ms->shutdown = 2; /* being down */
	vty_notify(ms, NULL);
	vty_notify(ms, "Power off!\n");
	printf("Power off! (MS %s)\n", ms->name);

	return 0;
}

int l23_app_init(struct osmocom_ms *ms)
{
	int rc;

	lapdm_init(&ms->l2_entity.lapdm_dcch, ms);
	lapdm_init(&ms->l2_entity.lapdm_acch, ms);
	gsm_sim_init(ms);
	gsm48_cc_init(ms);
	gsm_subscr_init(ms);
	gsm48_rr_init(ms);
	gsm48_mm_init(ms);
	INIT_LLIST_HEAD(&ms->trans_list);
	gsm322_init(ms);

	rc = layer2_open(ms, ms->settings.layer2_socket_path);
	if (rc < 0) {
		fprintf(stderr, "Failed during layer2_open()\n");
		ms->l2_wq.bfd.fd = -1;
		l23_app_exit(ms, 1);
		return rc;
	}

#if 0
	rc = sap_open(ms, ms->settings.sap_socket_path);
	if (rc < 0) {
		fprintf(stderr, "Failed during sap_open(), no SIM reader\n");
		ms->sap_wq.bfd.fd = -1;
		l23_app_exit(ms, 1);
		return rc;
	}
#endif

	if (ms->settings.ch_cap == GSM_CAP_SDCCH)
		ms->cclayer.mncc_recv = mncc_recv_dummy;
	else
		ms->cclayer.mncc_recv = mncc_recv_mobile;

	gsm_random_imei(&ms->settings);

	ms->shutdown = 0;
	ms->started = 0;

	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	printf("Mobile '%s' initialized, please start phone now!\n", ms->name);
	return 0;
}

