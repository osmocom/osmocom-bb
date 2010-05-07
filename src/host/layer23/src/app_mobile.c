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

#include <osmocom/osmocom_data.h>
#include <osmocom/l1ctl.h>
#include <osmocom/l23_app.h>
#include <osmocom/gsm48_rr.h>
#include <osmocom/sysinfo.h>
#include <osmocom/lapdm.h>
#include <osmocom/gsmtap_util.h>
#include <osmocom/logging.h>

#include <osmocore/msgb.h>
#include <osmocore/talloc.h>
#include <osmocore/select.h>
#include <osmocore/signal.h>

extern struct log_target *stderr_target;

static int started = 0;

int mncc_recv_dummy(struct osmocom_ms *ms, int msg_type, void *arg);

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
		w |= mncc_dequeue(ms);
		if (w)
			work = 1;
	} while (w);
	return work;
}

static int signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms;
	struct msgb *nmsg;

	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_RESET:
		if (started) {
			printf("L1_RESET, TODO: resend last radio request "
				"(CCCH / dedicated / power scan)\n");
			break;
		}
		started = 1;
		ms = signal_data;
		gsm_subscr_testcard(ms, 1, 1, "0000000000");
		ms->subscr.plmn_valid = 1;
		ms->subscr.plmn_mcc = 1;
		ms->subscr.plmn_mnc = 1;
		/* start PLMN + cell selection process */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_SWITCH_ON);
		if (!nmsg)
			return -ENOMEM;
		gsm322_plmn_sendmsg(ms, nmsg);
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_SWITCH_ON);
		if (!nmsg)
			return -ENOMEM;
		gsm322_cs_sendmsg(ms, nmsg);
	}
	return 0;
}

int mobile_exit(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	if (!mm->power_off && started) {
		struct msgb *nmsg;

		mm->power_off = 1;
		nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_IMSI_DETACH);
		if (!nmsg)
			return -ENOMEM;
		gsm48_mmevent_msg(mm->ms, nmsg);

		return -EBUSY;
	}

	/* in case there is a lockup during exit */
	signal(SIGINT, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);

	unregister_signal_handler(SS_L1CTL, &signal_cb, NULL);
	gsm322_exit(ms);
	gsm48_mm_exit(ms);
	gsm48_rr_exit(ms);
	gsm_subscr_exit(ms);
	gsm48_cc_exit(ms);

	return 0;
}

int l23_app_init(struct osmocom_ms *ms)
{
	log_parse_category_mask(stderr_target, "DCS:DPLMN:DRR:DMM:DCC:DMNCC:DPAG");

	gsm48_cc_init(ms);
	gsm_support_init(ms);
	gsm_subscr_init(ms);
	gsm48_rr_init(ms);
	gsm48_mm_init(ms);
	INIT_LLIST_HEAD(&ms->trans_list);
	ms->cclayer.mncc_recv = mncc_recv_dummy;
	gsm322_init(ms);
	l23_app_work = mobile_work;
	register_signal_handler(SS_L1CTL, &signal_cb, NULL);
	l23_app_exit = mobile_exit;

	return 0;
}

