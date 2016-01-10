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
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/gps.h>
#include <osmocom/bb/mobile/gsm48_rr.h>
#include <osmocom/bb/mobile/gsm480_ss.h>
#include <osmocom/bb/mobile/gsm411_sms.h>
#include <osmocom/bb/mobile/vty.h>
#include <osmocom/bb/mobile/app_mobile.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/voice.h>
#include <osmocom/bb/common/sap_interface.h>
#include <osmocom/vty/telnet_interface.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/select.h>
#include <osmocom/core/signal.h>

#include <l1ctl_proto.h>

extern void *l23_ctx;
extern struct llist_head ms_list;
extern int vty_reading;

int mncc_recv_mobile(struct osmocom_ms *ms, int msg_type, void *arg);
int mncc_recv_dummy(struct osmocom_ms *ms, int msg_type, void *arg);
int (*mncc_recv_app)(struct osmocom_ms *ms, int, void *);
static int quit;

/* handle ms instance */
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

/* run ms instance, if layer1 is available */
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

		/* waiting for reset after shutdown */
		if (ms->shutdown == 2) {
			printf("MS '%s' has been resetted\n", ms->name);
			ms->shutdown = 3;
			break;
		}

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
				set->test_rplmn_mnc, set->test_lac,
				set->test_tmsi, set->test_imsi_attached);
			break;
		case GSM_SIM_TYPE_SAP:
			gsm_subscr_sapcard(ms);
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

/* power-off ms instance */
int mobile_exit(struct osmocom_ms *ms, int force)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* if shutdown is already performed */
	if (ms->shutdown >= 2)
		return 0;

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
	gsm480_ss_exit(ms);
	gsm411_sms_exit(ms);
	gsm_sim_exit(ms);
	lapdm_channel_exit(&ms->lapdm_channel);

	if (ms->started) {
		ms->shutdown = 2; /* being down, wait for reset */
		l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	} else {
		ms->shutdown = 3; /* being down */
	}
	vty_notify(ms, NULL);
	vty_notify(ms, "Power off!\n");
	printf("Power off! (MS %s)\n", ms->name);

	return 0;
}

/* power-on ms instance */
int mobile_init(struct osmocom_ms *ms)
{
	int rc;

	gsm_settings_arfcn(ms);

	lapdm_channel_init(&ms->lapdm_channel, LAPDM_MODE_MS);
	ms->lapdm_channel.lapdm_dcch.datalink[DL_SAPI3].dl.t200_sec =
		T200_DCCH_SHARED;
	ms->lapdm_channel.lapdm_dcch.datalink[DL_SAPI3].dl.t200_usec = 0;
	ms->lapdm_channel.lapdm_acch.datalink[DL_SAPI3].dl.t200_sec =
		T200_ACCH;
	ms->lapdm_channel.lapdm_acch.datalink[DL_SAPI3].dl.t200_usec = 0;
	lapdm_channel_set_l1(&ms->lapdm_channel, l1ctl_ph_prim_cb, ms);

	/* init SAP client before SIM card starts up */
	osmosap_init(ms);

	gsm_sim_init(ms);
	gsm48_cc_init(ms);
	gsm480_ss_init(ms);
	gsm411_sms_init(ms);
	gsm_voice_init(ms);
	gsm_subscr_init(ms);
	gsm48_rr_init(ms);
	gsm48_mm_init(ms);
	INIT_LLIST_HEAD(&ms->trans_list);
	gsm322_init(ms);

	rc = layer2_open(ms, ms->settings.layer2_socket_path);
	if (rc < 0) {
		fprintf(stderr, "Failed during layer2_open()\n");
		ms->l2_wq.bfd.fd = -1;
		mobile_exit(ms, 1);
		return rc;
	}

#if 0
	rc = sap_open(ms, ms->settings.sap_socket_path);
	if (rc < 0) {
		fprintf(stderr, "Failed during sap_open(), no SIM reader\n");
		ms->sap_wq.bfd.fd = -1;
		mobile_exit(ms, 1);
		return rc;
	}
#endif

	gsm_random_imei(&ms->settings);

	ms->shutdown = 0;
	ms->started = 0;

	if (!strcmp(ms->settings.imei, "000000000000000")) {
		printf("***\nWarning: Mobile '%s' has default IMEI: %s\n",
			ms->name, ms->settings.imei);
		printf("This could relate your identitiy to other users with "
			"default IMEI.\n***\n");
	}

	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	printf("Mobile '%s' initialized, please start phone now!\n", ms->name);
	return 0;
}

/* create ms instance */
struct osmocom_ms *mobile_new(char *name)
{
	static struct osmocom_ms *ms;

	ms = talloc_zero(l23_ctx, struct osmocom_ms);
	if (!ms) {
		fprintf(stderr, "Failed to allocate MS\n");
		exit(1);
	}
	llist_add_tail(&ms->entity, &ms_list);

	strcpy(ms->name, name);

	ms->l2_wq.bfd.fd = -1;
	ms->sap_wq.bfd.fd = -1;

	gsm_support_init(ms);
	gsm_settings_init(ms);

	ms->shutdown = 3; /* being down */

	if (mncc_recv_app) {
		char name[32];

		sprintf(name, "/tmp/ms_mncc_%s", ms->name);

		ms->mncc_entity.mncc_recv = mncc_recv_app;
		ms->mncc_entity.sock_state = mncc_sock_init(ms, name, l23_ctx);

	} else if (ms->settings.ch_cap == GSM_CAP_SDCCH)
		ms->mncc_entity.mncc_recv = mncc_recv_dummy;
	else
		ms->mncc_entity.mncc_recv = mncc_recv_mobile;


	return ms;
}

/* destroy ms instance */
int mobile_delete(struct osmocom_ms *ms, int force)
{
	int rc;

	ms->deleting = 1;

	if (mncc_recv_app) {
		mncc_sock_exit(ms->mncc_entity.sock_state);
		ms->mncc_entity.sock_state = NULL;
	}

	if (ms->shutdown == 0 || (ms->shutdown == 1 && force)) {
		rc = mobile_exit(ms, force);
		if (rc < 0)
			return rc;
	}

	return 0;
}

/* handle global shutdown */
int global_signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms, *ms2;

	if (subsys != SS_GLOBAL)
		return 0;

	switch (signal) {
	case S_GLOBAL_SHUTDOWN:
		/* force to exit, if signalled */
		if (signal_data && *((uint8_t *)signal_data))
			quit = 1;

		llist_for_each_entry_safe(ms, ms2, &ms_list, entity)
			mobile_delete(ms, quit);

		/* quit, after all MS processes are gone */
		quit = 1;
		break;
	}
	return 0;
}

/* global work handler */
int l23_app_work(int *_quit)
{
	struct osmocom_ms *ms, *ms2;
	int work = 0;

	llist_for_each_entry_safe(ms, ms2, &ms_list, entity) {
		if (ms->shutdown != 3)
			work |= mobile_work(ms);
		if (ms->shutdown == 3) {
			if (ms->l2_wq.bfd.fd > -1) {
				layer2_close(ms);
				ms->l2_wq.bfd.fd = -1;
			}

			if (ms->sap_wq.bfd.fd > -1) {
				sap_close(ms);
				ms->sap_wq.bfd.fd = -1;
			}

			if (ms->deleting) {
				gsm_settings_exit(ms);
				llist_del(&ms->entity);
				talloc_free(ms);
				work = 1;
			}
		}
	}

	/* return, if a shutdown was scheduled (quit = 1) */
	*_quit = quit;
	return work;
}

/* global exit */
int l23_app_exit(void)
{
	osmo_signal_unregister_handler(SS_L1CTL, &gsm322_l1_signal, NULL);
	osmo_signal_unregister_handler(SS_L1CTL, &mobile_signal_cb, NULL);
	osmo_signal_unregister_handler(SS_GLOBAL, &global_signal_cb, NULL);

	osmo_gps_close();

	telnet_exit();

	return 0;
}

static struct vty_app_info vty_info = {
	.name = "OsmocomBB",
	.version = PACKAGE_VERSION,
	.go_parent_cb = ms_vty_go_parent,
};

/* global init */
int l23_app_init(int (*mncc_recv)(struct osmocom_ms *ms, int, void *),
	const char *config_file, const char *vty_ip, uint16_t vty_port)
{
	struct telnet_connection dummy_conn;
	int rc = 0;

	mncc_recv_app = mncc_recv;

	osmo_gps_init();

	vty_init(&vty_info);
	ms_vty_init();
	dummy_conn.priv = NULL;
	vty_reading = 1;
	if (config_file != NULL) {
		rc = vty_read_config_file(config_file, &dummy_conn);
		if (rc < 0) {
			fprintf(stderr, "Failed to parse the config file:"
					" '%s'\n", config_file);
			fprintf(stderr, "Please check or create config file"
					" using: 'touch %s'\n", config_file);
			return rc;
		}
	}
	vty_reading = 0;
	rc = telnet_init_dynif(l23_ctx, NULL, vty_ip, vty_port);
	if (rc < 0)
		return rc;
	printf("VTY available on port %u.\n", vty_port);

	osmo_signal_register_handler(SS_GLOBAL, &global_signal_cb, NULL);
	osmo_signal_register_handler(SS_L1CTL, &mobile_signal_cb, NULL);
	osmo_signal_register_handler(SS_L1CTL, &gsm322_l1_signal, NULL);

	if (llist_empty(&ms_list)) {
		struct osmocom_ms *ms;

		printf("No Mobile Station defined, creating: MS '1'\n");
		ms = mobile_new("1");
		if (ms) {
			rc = mobile_init(ms);
			if (rc < 0)
				return rc;
		}
	}

	quit = 0;

	return 0;
}

