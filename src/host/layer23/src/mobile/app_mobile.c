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
#include <time.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/lapdm.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/mobile/gsm48_rr.h>
#include <osmocom/bb/mobile/sysinfo.h>
#include <osmocom/bb/mobile/vty.h>
#include <osmocom/bb/mobile/gps.h>
#include <osmocom/vty/telnet_interface.h>

#include <osmocore/msgb.h>
#include <osmocore/talloc.h>
#include <osmocore/select.h>
#include <osmocore/signal.h>

extern struct log_target *stderr_target;
static const char *config_file = "/etc/osmocom/osmocom.cfg";
extern void *l23_ctx;
extern unsigned short vty_port;
extern int vty_reading;

int mobile_started = 0;

int mncc_recv_mobile(struct osmocom_ms *ms, int msg_type, void *arg);

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

static int signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms;
	struct gsm_settings *set;
	struct msgb *nmsg;

	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_RESET:
		if (mobile_started)
			break;

		ms = signal_data;
		set = &ms->settings;

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

		mobile_started = 1;
	}
	return 0;
}

int mobile_exit(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	if (!mm->power_off && mobile_started) {
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
	gps_close();
	gsm322_exit(ms);
	gsm48_mm_exit(ms);
	gsm48_rr_exit(ms);
	gsm_subscr_exit(ms);
	gsm48_cc_exit(ms);
	gsm_sim_exit(ms);

	printf("Power off!\n");

	return 0;
}

static struct vty_app_info vty_info = {
	.name = "OsmocomBB",
	.version = PACKAGE_VERSION,
	.go_parent_cb = ms_vty_go_parent,
};

int l23_app_init(struct osmocom_ms *ms)
{
	int rc;
	struct telnet_connection dummy_conn;

//	log_parse_category_mask(stderr_target, "DL1C:DRSL:DLAPDM:DCS:DPLMN:DRR:DMM:DSIM:DCC:DMNCC:DPAG:DSUM");
	log_parse_category_mask(stderr_target, "DCS:DPLMN:DRR:DMM:DSIM:DCC:DMNCC:DPAG:DSUM");
	log_set_log_level(stderr_target, LOGL_INFO);

	srand(time(NULL));

	gsm_support_init(ms);
	gsm_sim_init(ms);
	gsm_settings_init(ms);
	gsm48_cc_init(ms);
	gsm_subscr_init(ms);
	gsm48_rr_init(ms);
	gsm48_mm_init(ms);
	INIT_LLIST_HEAD(&ms->trans_list);
	ms->cclayer.mncc_recv = mncc_recv_mobile;
	gsm322_init(ms);

	l23_app_work = mobile_work;
	register_signal_handler(SS_L1CTL, &signal_cb, NULL);
	l23_app_exit = mobile_exit;

	vty_init(&vty_info);
	ms_vty_init();
	dummy_conn.priv = NULL;
	vty_reading = 1;
	rc = vty_read_config_file(config_file, &dummy_conn);
	if (rc < 0) {
		fprintf(stderr, "Failed to parse the config file: '%s'\n",
			config_file);
		fprintf(stderr, "Please check or create config file using: "
			"'touch %s'\n", config_file);
		return rc;
	}
	vty_reading = 0;
	telnet_init(l23_ctx, NULL, vty_port);
	if (rc < 0)
		return rc;
	printf("VTY available on port %u.\n", vty_port);

	gsm_random_imei(&ms->settings);

	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	printf("Mobile initialized, please start phone now!\n");
	return 0;
}

