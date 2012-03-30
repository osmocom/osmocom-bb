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
 */

#include <errno.h>
#include <signal.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/l1l2_interface.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/gps.h>
#include <osmocom/bb/common/sap_interface.h>
#include <osmocom/bb/common/sim.h>
#include <osmocom/bb/common/l23_app.h>

#include <osmocom/bb/mobile/gsm48_rr.h>
#include <osmocom/bb/mobile/gsm480_ss.h>
#include <osmocom/bb/mobile/gsm48_mm.h>
#include <osmocom/bb/mobile/gsm48_cc.h>
#include <osmocom/bb/mobile/gsm44068_gcc_bcc.h>
#include <osmocom/bb/mobile/gsm411_sms.h>
#include <osmocom/bb/mobile/gsm322.h>
#include <osmocom/bb/mobile/vty.h>
#include <osmocom/bb/mobile/app_mobile.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/mncc_ms.h>
#include <osmocom/bb/mobile/tch.h>
#include <osmocom/bb/mobile/primitives.h>

#include <osmocom/vty/vty.h>
#include <osmocom/vty/telnet_interface.h>
#include <osmocom/bb/ui/telnet_interface.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/select.h>
#include <osmocom/core/signal.h>

#include <l1ctl_proto.h>

#include "config.h"

extern void *l23_ctx;
extern struct llist_head ms_list;

static int _quit;
extern int quit; /* l23 main */

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

/* SIM becomes ATTACHED/DETACHED, or answers a request */
static int mobile_l23_subscr_signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct msgb *nmsg;
	struct gsm48_mm_event *nmme;
	struct osmocom_ms *ms;
	struct osmobb_l23_subscr_sim_auth_resp_sig_data *sim_auth_resp;

	OSMO_ASSERT(subsys == SS_L23_SUBSCR);

	switch (signal) {
	case S_L23_SUBSCR_SIM_ATTACHED:
		ms = signal_data;
		nmsg = gsm48_mmr_msgb_alloc(GSM48_MMR_REG_REQ);
		if (!nmsg)
			return -ENOMEM;
		gsm48_mmr_downmsg(ms, nmsg);
		break;
	case S_L23_SUBSCR_SIM_DETACHED:
		ms = signal_data;
		nmsg = gsm48_mmr_msgb_alloc(GSM48_MMR_NREG_REQ);
		if (!nmsg)
			return 0;
		gsm48_mmr_downmsg(ms, nmsg);
		break;
	case S_L23_SUBSCR_SIM_AUTH_RESP:
		sim_auth_resp = signal_data;
		ms = sim_auth_resp->ms;
		nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_AUTH_RESPONSE);
		if (!nmsg)
			return 0;
		nmme = (struct gsm48_mm_event *) nmsg->data;
		memcpy(nmme->sres, sim_auth_resp->sres, 4);
		gsm48_mmevent_msg(ms, nmsg);
		break;
	default:
		OSMO_ASSERT(0);
	}

	return 0;
}

/* run ms instance, if layer1 is available */
static int mobile_signal_cb(unsigned int subsys, unsigned int signal,
			    void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms;
	struct osmobb_keypad *kp;
	struct msgb *nmsg;

	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_RESET:
		ms = signal_data;

		/* waiting for reset after shutdown */
		if (ms->shutdown == MS_SHUTDOWN_WAIT_RESET) {
			LOGP(DMOB, LOGL_NOTICE, "MS '%s' has been reset\n", ms->name);
			ms->shutdown = MS_SHUTDOWN_COMPL;
			break;
		}

		if (ms->started)
			break;

		if (ms->settings.sim_type != GSM_SIM_TYPE_NONE) {
			/* insert sim card */
			gsm_subscr_insert(ms);
		} else {
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

		mobile_set_started(ms, true);
		break;
	case S_L1CTL_KEYPAD:
		kp = signal_data;
		ms = kp->ms;
		/* gui disabled */
		if (!ms->settings.ui_port)
			break;
		ui_inst_keypad(&ms->gui.ui, kp->key);				
		break;
	}
	return 0;
}

/* power-off ms instance */
int mobile_exit(struct osmocom_ms *ms, int force)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* if shutdown is already performed */
	if (ms->shutdown >= MS_SHUTDOWN_WAIT_RESET)
		return 0;

	if (!force && ms->started) {
		struct msgb *nmsg;

		mobile_set_shutdown(ms, MS_SHUTDOWN_IMSI_DETACH);
		nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_IMSI_DETACH);
		if (!nmsg)
			return -ENOMEM;
		gsm48_mmevent_msg(mm->ms, nmsg);

		return -EBUSY;
	}

	gui_stop(ms);
	ui_telnet_exit(&ms->gui.ui);
	gsm322_exit(ms);
	gsm48_mm_exit(ms);
	gsm48_rr_exit(ms);
	gsm_subscr_exit(ms);
	mnccms_exit(ms);
	gsm48_cc_exit(ms);
	gsm480_ss_exit(ms);
	gsm411_sms_exit(ms);
	gsm44068_gcc_exit(ms);
	gsm_sim_exit(ms);
	lapdm_channel_exit(&ms->lapdm_channel);

	if (ms->started) {
		mobile_set_shutdown(ms, MS_SHUTDOWN_WAIT_RESET); /* being down, wait for reset */
		l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	} else {
		mobile_set_shutdown(ms, MS_SHUTDOWN_COMPL); /* being down */
	}
	l23_vty_ms_notify(ms, NULL);
	l23_vty_ms_notify(ms, "Power off!\n");
	LOGP(DMOB, LOGL_NOTICE, "Power off! (MS %s)\n", ms->name);

	return 0;
}

/* power-on ms instance */
static int mobile_init(struct osmocom_ms *ms)
{
	int rc;

	gsm_settings_arfcn(ms);

	const int t200_ms_dcch[_NR_DL_SAPI] = {
		[DL_SAPI0] = 1000,
		[DL_SAPI3] = 1000 * T200_DCCH_SHARED
	};
	const int t200_ms_acch[_NR_DL_SAPI] = {
		[DL_SAPI0] = 2000,
		[DL_SAPI3] = 1000 * T200_ACCH
	};

	lapdm_channel_init3(&ms->lapdm_channel, LAPDM_MODE_MS,
			    t200_ms_dcch, t200_ms_acch,
			    GSM_LCHAN_SDCCH, NULL);
	lapdm_channel_set_flags(&ms->lapdm_channel, LAPDM_ENT_F_DROP_2ND_REJ);
	lapdm_channel_set_l1(&ms->lapdm_channel, l1ctl_ph_prim_cb, ms);

	gsm_sim_init(ms);
	mnccms_init(ms);
	gsm48_cc_init(ms);
	gsm480_ss_init(ms);
	gsm411_sms_init(ms);
	gsm44068_gcc_init(ms);
	tch_init(ms);
	gsm_subscr_init(ms);
	gsm48_rr_init(ms);
	gsm48_mm_init(ms);
	INIT_LLIST_HEAD(&ms->trans_list);
	gsm322_init(ms);

	rc = layer2_open(ms, ms->settings.layer2_socket_path);
	if (rc < 0) {
		LOGP(DMOB, LOGL_ERROR, "Failed during layer2_open(%s)\n", ms->settings.layer2_socket_path);
		ms->l2_wq.bfd.fd = -1;
		mobile_exit(ms, 1);
		return rc;
	}


	if (ms->settings.ui_port) {
		rc = ui_telnet_init(&ms->gui.ui, l23_ctx, ms->settings.ui_port);
		if (rc < 0) {
			fprintf(stderr, "Failed during ui_telnet_init()\n");
			mobile_exit(ms, 1);
			return rc;
		}
		printf("UI available on port %u.\n", ms->settings.ui_port);
		gui_start(ms);
	}

	gsm_random_imei(&ms->settings);

	mobile_set_shutdown(ms, MS_SHUTDOWN_NONE);
	mobile_set_started(ms, false);

	if (!strcmp(ms->settings.imei, "000000000000000")) {
		LOGP(DMOB, LOGL_NOTICE, "***\nWarning: Mobile '%s' has default IMEI: %s\n",
			ms->name, ms->settings.imei);
		LOGP(DMOB, LOGL_NOTICE, "This could relate your identitiy to other users with "
			"default IMEI.\n***\n");
	}

	switch (ms->settings.mncc_handler) {
	case MNCC_HANDLER_INTERNAL:
		LOGP(DMOB, LOGL_INFO, "Using the built-in MNCC-handler for MS '%s'\n", ms->name);
		ms->mncc_entity.mncc_recv = &mncc_recv_internal;
		break;
	case MNCC_HANDLER_EXTERNAL:
		LOGP(DMOB, LOGL_INFO, "Using external MNCC-handler (socket '%s') for MS '%s'\n",
		     ms->settings.mncc_socket_path, ms->name);
		ms->mncc_entity.mncc_recv = &mncc_recv_external;
		ms->mncc_entity.sock_state = mncc_sock_init(ms, ms->settings.mncc_socket_path);
		break;
	case MNCC_HANDLER_DUMMY:
	default:
		LOGP(DMOB, LOGL_INFO, "Using dummy MNCC-handler (no call support) "
			"for MS '%s'\n", ms->name);
		ms->mncc_entity.mncc_recv = &mncc_recv_dummy;
	}

	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	LOGP(DMOB, LOGL_NOTICE, "Mobile '%s' initialized, please start phone now!\n", ms->name);
	return 0;
}

int mobile_start(struct osmocom_ms *ms, char **other_name)
{
	struct osmocom_ms *tmp;
	int rc;

	if (ms->shutdown != MS_SHUTDOWN_COMPL)
		return 0;

	llist_for_each_entry(tmp, &ms_list, entity) {
		if (tmp->shutdown == MS_SHUTDOWN_COMPL)
			continue;
		if (!strcmp(ms->settings.layer2_socket_path,
				tmp->settings.layer2_socket_path)) {
			LOGP(DMOB, LOGL_ERROR, "Cannot start MS '%s', because MS '%s' "
				"is using the same layer2-socket.\nPlease shutdown "
				"MS '%s' first.\n", ms->name, tmp->name, tmp->name);
			*other_name = tmp->name;
			return -1;
		}
		if (!strcmp(ms->settings.sap_socket_path,
				tmp->settings.sap_socket_path)) {
			LOGP(DMOB, LOGL_ERROR, "Cannot start MS '%s', because MS '%s' "
				"is using the same sap-socket.\nPlease shutdown "
				"MS '%s' first.\n", ms->name, tmp->name, tmp->name);
			*other_name = tmp->name;
			return -2;
		}
		if (!strcmp(ms->settings.mncc_socket_path,
				tmp->settings.mncc_socket_path)) {
			LOGP(DMOB, LOGL_ERROR, "Cannot start MS '%s', because MS '%s' "
				"is using the same mncc-socket.\nPlease shutdown "
				"MS '%s' first.\n", ms->name, tmp->name, tmp->name);
			*other_name = tmp->name;
			return -3;
		}
	}

	rc = mobile_init(ms);
	if (rc < 0)
		return -3;
	return 0;
}

int mobile_stop(struct osmocom_ms *ms, int force)
{
	if (force && ms->shutdown <= MS_SHUTDOWN_IMSI_DETACH)
		return mobile_exit(ms, 1);
	if (!force && ms->shutdown == MS_SHUTDOWN_NONE)
		return mobile_exit(ms, 0);
	return 0;
}


/* create ms instance */
struct osmocom_ms *mobile_new(char *name)
{
	static struct osmocom_ms *ms;

	ms = osmocom_ms_alloc(l23_ctx, name);
	if (!ms) {
		LOGP(DMOB, LOGL_ERROR, "Failed to allocate MS: %s\n", name);
		return NULL;
	}

	mobile_set_shutdown(ms, MS_SHUTDOWN_COMPL);
	return ms;
}

/* destroy ms instance */
int mobile_delete(struct osmocom_ms *ms, int force)
{
	int rc;

	ms->deleting = true;

	if (ms->settings.mncc_handler == MNCC_HANDLER_EXTERNAL) {
		mncc_sock_exit(ms->mncc_entity.sock_state);
		ms->mncc_entity.sock_state = NULL;
	}

	if (ms->shutdown == MS_SHUTDOWN_NONE || (ms->shutdown == MS_SHUTDOWN_IMSI_DETACH && force)) {
		rc = mobile_exit(ms, force);
		if (rc < 0)
			return rc;
	}

	return 0;
}

/* handle global shutdown */
static int global_signal_cb(unsigned int subsys, unsigned int signal,
			    void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms, *ms2;

	if (subsys != SS_GLOBAL)
		return 0;

	switch (signal) {
	case S_GLOBAL_SHUTDOWN:
		/* force to exit, if signalled */
		if (signal_data && *((uint8_t *)signal_data))
			_quit = 1;

		llist_for_each_entry_safe(ms, ms2, &ms_list, entity)
			mobile_delete(ms, _quit);

		/* quit, after all MS processes are gone */
		_quit = 1;
		break;
	}
	return 0;
}

/* global work handler */
static int _mobile_app_work(void)
{
	struct osmocom_ms *ms, *ms2;
	int work = 0;

	llist_for_each_entry_safe(ms, ms2, &ms_list, entity) {
		if (ms->shutdown != MS_SHUTDOWN_COMPL)
			work |= mobile_work(ms);
		if (ms->shutdown == MS_SHUTDOWN_COMPL) {
			if (ms->l2_wq.bfd.fd > -1) {
				layer2_close(ms);
				ms->l2_wq.bfd.fd = -1;
			}
			if (ms->deleting) {
				gsm_settings_exit(ms);
				script_lua_close(ms);
				llist_del(&ms->entity);
				talloc_free(ms);
				work = 1;
			}
		}
	}

	/* return, if a shutdown was scheduled (quit = 1) */
	quit = _quit;
	return work;
}

/* global exit */
static int _mobile_app_exit(void)
{
	osmo_signal_unregister_handler(SS_L23_SUBSCR, &mobile_l23_subscr_signal_cb, NULL);
	osmo_signal_unregister_handler(SS_L1CTL, &gsm322_l1_signal, NULL);
	osmo_signal_unregister_handler(SS_L1CTL, &mobile_signal_cb, NULL);
	osmo_signal_unregister_handler(SS_GLOBAL, &global_signal_cb, NULL);

	osmo_gps_close();

	ms_vty_exit();

	return 0;
}


static int _mobile_app_start(void)
{
	int rc;

	if (llist_empty(&ms_list)) {
		struct osmocom_ms *ms;

		LOGP(DMOB, LOGL_NOTICE, "No Mobile Station defined, creating: MS '1'\n");
		ms = mobile_new("1");
		if (!ms)
			return -1;

		rc = mobile_init(ms);
		if (rc < 0)
			return rc;
	}

	_quit = 0;

	return 0;
}

/* global init */
int l23_app_init(void)
{
	l23_app_start = _mobile_app_start;
	l23_app_work = _mobile_app_work;
	l23_app_exit = _mobile_app_exit;
	osmo_gps_init();

	osmo_signal_register_handler(SS_GLOBAL, &global_signal_cb, NULL);
	osmo_signal_register_handler(SS_L1CTL, &mobile_signal_cb, NULL);
	osmo_signal_register_handler(SS_L1CTL, &gsm322_l1_signal, NULL);
	osmo_signal_register_handler(SS_L23_SUBSCR, &mobile_l23_subscr_signal_cb, NULL);

	return 0;
}

static int _mobile_vty_init(void)
{
	return ms_vty_init(l23_ctx);
}

static struct vty_app_info _mobile_vty_info = {
	.name = "OsmocomBB(mobile)",
	.version = PACKAGE_VERSION,
};

const struct l23_app_info l23_app_info = {
	.copyright = "Copyright (C) 2010-2015 Andreas Eversberg, Sylvain Munaut, Holger Freyther, Harald Welte\n",
	.contribution = "Contributions by Alex Badea, Pablo Neira, Steve Markgraf and others\n",
	.opt_supported = L23_OPT_TAP | L23_OPT_VTY | L23_OPT_DBG,
	.vty_info = &_mobile_vty_info,
	.vty_init = _mobile_vty_init,
};

void mobile_set_started(struct osmocom_ms *ms, bool state)
{
	ms->started = state;

	mobile_prim_ntfy_started(ms, state);
}

void mobile_set_shutdown(struct osmocom_ms *ms, int state)
{
	int old_state = ms->shutdown;
	ms->shutdown = state;

	mobile_prim_ntfy_shutdown(ms, old_state, state);
}
