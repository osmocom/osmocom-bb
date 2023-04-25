/*
 * (C) 2023 by sysmocom - s.m.f.c. GmbH <info@sysmocom.de>
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

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/linuxlist.h>

#include <osmocom/gprs/llc/llc.h>
#include <osmocom/gprs/llc/llc_prim.h>
#include <osmocom/gprs/gmm/gmm_prim.h>
#include <osmocom/gprs/sm/sm_prim.h>


#include <osmocom/vty/vty.h>
#include <osmocom/vty/command.h>

#include <osmocom/bb/common/settings.h>
#include <osmocom/bb/common/vty.h>
#include <osmocom/bb/common/apn.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/modem/gmm.h>
#include <osmocom/bb/modem/grr.h>
#include <osmocom/bb/modem/sm.h>
#include <osmocom/bb/modem/vty.h>

static struct cmd_node apn_node = {
	APN_NODE,
	"%s(apn)# ",
	1
};

int modem_vty_go_parent(struct vty *vty)
{
	struct osmobb_apn *apn;

	switch (vty->node) {
	case APN_NODE:
		apn = vty->index;
		vty->index = apn->ms;
		vty->node = MS_NODE;
		break;
	}
	return vty->node;
}

#define MS_NAME_DESC "Name of MS (see \"show ms\")\n"
#define TEST_CMD_DESC "Testing commands for developers\n"
#define GRR_CMDG_DESC "GPRS RR specific commands\n"
#define LLC_CMDG_DESC "GPRS LLC specific commands\n"
#define GMM_CMDG_DESC "GPRS GMM specific commands\n"
#define SM_CMDG_DESC "GPRS SM specific commands\n"

/* testing commands */
DEFUN_HIDDEN(test_grr_tx_chan_req,
	     test_grr_tx_chan_req_cmd,
	     "test MS_NAME grr tx-chan-req (1phase|2phase)",
	     TEST_CMD_DESC MS_NAME_DESC GRR_CMDG_DESC
	     "Send a CHANNEL REQUEST (RACH) to the network\n"
	     "One-phase packet access (011110xx or 01111x0x or 01111xx0)\n"
	     "Two-phase (single block) packet access (01110xxx)\n")
{
	struct osmocom_ms *ms;
	uint8_t chan_req;

	if ((ms = l23_vty_get_ms(argv[0], vty)) == NULL)
		return CMD_WARNING;

	chan_req = modem_grr_gen_chan_req(argv[1][0] == '2');
	if (modem_grr_tx_chan_req(ms, chan_req) != 0) {
		vty_out(vty, "Failed to send a CHANNEL REQUEST%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

DEFUN_HIDDEN(test_llc_unitdata_req_hexpdu,
	     test_llc_unitdata_req_hexpdu_cmd,
	     "test MS_NAME llc unitdata-req <0x00-0xffffffff> SAPI HEXSTRING",
	     TEST_CMD_DESC MS_NAME_DESC LLC_CMDG_DESC
	     "Enqueue an LLC UNITDATA.req for transmission\n"
	     "TLLI (Temporary Logical Link Identifier) value to be used\n"
	     "SAPI value to be used (for example, GMM, SMS, SNDCP3)\n"
	     "LLC PDU as a hexstring (up to 512 octets)\n")
{
	struct osmo_gprs_llc_prim *llc_prim;
	struct osmocom_ms *ms;
	uint8_t buf[512];
	int tlli, sapi, pdu_len;

	if ((ms = l23_vty_get_ms(argv[0], vty)) == NULL)
		return CMD_WARNING;

	if (osmo_str_to_int(&tlli, argv[1], 0, 0, 0xffffff) < 0)
		return CMD_WARNING;
	sapi = get_string_value(osmo_gprs_llc_sapi_names, argv[2]);
	if (sapi < 0)
		return CMD_WARNING;
	pdu_len = osmo_hexparse(argv[3], &buf[0], sizeof(buf));
	if (pdu_len < 0)
		return CMD_WARNING;

	llc_prim = osmo_gprs_llc_prim_alloc_ll_unitdata_req(tlli, sapi, &buf[0], pdu_len);
	if (osmo_gprs_llc_prim_upper_down(llc_prim) != 0) {
		vty_out(vty, "Failed to enqueue an LLC PDU%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

static uint8_t pdu_gmmm_attach_req[] = {
	0x08, 0x01, 0x02, 0xe5, 0xe0, 0x01, 0x0a, 0x00, 0x05, 0xf4, 0xf4, 0x3c, 0xec, 0x71, 0x32, 0xf4,
	0x07, 0x00, 0x05, 0x00, 0x17, 0x19, 0x33, 0x43, 0x2b, 0x37, 0x15, 0x9e, 0xf9, 0x88, 0x79, 0xcb,
	0xa2, 0x8c, 0x66, 0x21, 0xe7, 0x26, 0x88, 0xb1, 0x98, 0x87, 0x9c, 0x00, 0x17, 0x05,
};

/* TODO: remove this command once we have the GMM layer implemented */
DEFUN_HIDDEN(test_llc_unitdata_req_gmm_attch,
	     test_llc_unitdata_req_gmm_attch_cmd,
	     "test MS_NAME llc unitdata-req gmm-attach-req",
	     TEST_CMD_DESC MS_NAME_DESC LLC_CMDG_DESC
	     "Enqueue an LLC UNITDATA.req for transmission\n"
	     "Hard-coded GMM Attach Request (SAPI=GMM, TLLI=0xe1c5d364)\n")
{
	struct osmo_gprs_llc_prim *llc_prim;
	const uint32_t tlli = 0xe1c5d364;
	struct osmocom_ms *ms;

	if ((ms = l23_vty_get_ms(argv[0], vty)) == NULL)
		return CMD_WARNING;

	llc_prim = osmo_gprs_llc_prim_alloc_ll_unitdata_req(tlli, OSMO_GPRS_LLC_SAPI_GMM,
							    &pdu_gmmm_attach_req[0],
							    sizeof(pdu_gmmm_attach_req));
	if (osmo_gprs_llc_prim_upper_down(llc_prim) != 0) {
		vty_out(vty, "Failed to enqueue an LLC PDU%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

DEFUN_HIDDEN(test_gmm_reg_attach,
	     test_gmm_reg_attach_cmd,
	     "test MS_NAME gmm attach",
	     TEST_CMD_DESC MS_NAME_DESC GMM_CMDG_DESC
	     "Enqueue a GMM GMMREG-ATTACH.req for transmission\n")
{
	struct osmocom_ms *ms;

	if ((ms = l23_vty_get_ms(argv[0], vty)) == NULL) {
		vty_out(vty, "Failed to find ms '%s'%s", argv[0], VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (modem_gmm_gmmreg_attach_req(ms) < 0) {
		vty_out(vty, "Failed to enqueue a GMM PDU%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

DEFUN_HIDDEN(test_gmm_reg_detach,
	     test_gmm_reg_detach_cmd,
	     "test MS_NAME gmm detach",
	     TEST_CMD_DESC MS_NAME_DESC GMM_CMDG_DESC
	     "Enqueue a GMM GMMREG-DETACH.req for transmission\n")
{
	struct osmocom_ms *ms;

	if ((ms = l23_vty_get_ms(argv[0], vty)) == NULL) {
		vty_out(vty, "Failed to find ms '%s'%s", argv[0], VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (modem_gmm_gmmreg_detach_req(ms) < 0) {
		vty_out(vty, "Failed to enqueue a GMM PDU%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

DEFUN_HIDDEN(test_sm_act_pdp_ctx,
	     test_sm_act_pdp_ctx_cmd,
	     "test MS_NAME sm act-pdp-ctx APN",
	     TEST_CMD_DESC MS_NAME_DESC SM_CMDG_DESC
	     "Enqueue a SM SMREG-ACTIVATE.req for transmission\n"
	     "APN to activate\n")
{
	struct osmocom_ms *ms;
	struct osmobb_apn *apn;

	if ((ms = l23_vty_get_ms(argv[0], vty)) == NULL) {
		vty_out(vty, "Unable to find MS '%s'%s", argv[0], VTY_NEWLINE);
		return CMD_WARNING;
	}

	apn = ms_find_apn_by_name(ms, argv[1]);
	if (!apn) {
		vty_out(vty, "Unable to find APN '%s'%s", argv[1], VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (modem_sm_smreg_pdp_act_req(ms, apn) < 0) {
		vty_out(vty, "Failed submitting SM PDP Act Req%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

/* per APN config */
DEFUN(cfg_ms_apn, cfg_ms_apn_cmd, "apn APN_NAME",
	"Configure an APN\n"
	"Name of APN\n")
{
	struct osmocom_ms *ms = vty->index;
	struct osmobb_apn *apn;

	apn = ms_find_apn_by_name(ms, argv[0]);
	if (!apn)
		apn = apn_alloc(ms, argv[0]);
	if (!apn) {
		vty_out(vty, "Unable to create APN '%s'%s", argv[0], VTY_NEWLINE);
		return CMD_WARNING;
	}

	vty->index = apn;
	vty->node = APN_NODE;
	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_apn, cfg_ms_no_apn_cmd, "no apn APN_NAME",
	NO_STR "Configure an APN\n"
	"Name of APN\n")
{
	struct osmocom_ms *ms = vty->index;
	struct osmobb_apn *apn;

	apn = ms_find_apn_by_name(ms, argv[0]);
	if (!apn) {
		vty_out(vty, "Unable to find APN '%s'%s", argv[0], VTY_NEWLINE);
		return CMD_WARNING;
	}

	apn_free(apn);

	return CMD_SUCCESS;
}

DEFUN(cfg_apn_tun_dev_name, cfg_apn_tun_dev_name_cmd,
	"tun-device NAME",
	"Configure tun device name\n"
	"TUN device name")
{
	struct osmobb_apn *apn = (struct osmobb_apn *) vty->index;
	osmo_talloc_replace_string(apn, &apn->cfg.dev_name, argv[0]);
	return CMD_SUCCESS;
}

DEFUN(cfg_apn_tun_netns_name, cfg_apn_tun_netns_name_cmd,
	"tun-netns NAME",
	"Configure tun device network namespace name\n"
	"TUN device network namespace name")
{
	struct osmobb_apn *apn = (struct osmobb_apn *) vty->index;
	osmo_talloc_replace_string(apn, &apn->cfg.dev_netns_name, argv[0]);
	return CMD_SUCCESS;
}

DEFUN(cfg_apn_no_tun_netns_name, cfg_apn_no_tun_netns_name_cmd,
	"no tun-netns",
	"Configure tun device to use default network namespace name\n")
{
	struct osmobb_apn *apn = (struct osmobb_apn *) vty->index;
	TALLOC_FREE(apn->cfg.dev_netns_name);
	return CMD_SUCCESS;
}

static const struct value_string pdp_type_names[] = {
	{ APN_TYPE_IPv4, "v4" },
	{ APN_TYPE_IPv6, "v6" },
	{ APN_TYPE_IPv4v6, "v4v6" },
	{ 0, NULL }
};

#define V4V6V46_STRING	"IPv4(-only) PDP Type\n"	\
			"IPv6(-only) PDP Type\n"	\
			"IPv4v6 (dual-stack) PDP Type\n"

DEFUN(cfg_apn_type_support, cfg_apn_type_support_cmd,
	"type-support (v4|v6|v4v6)",
	"Enable support for PDP Type\n"
	V4V6V46_STRING)
{
	struct osmobb_apn *apn = (struct osmobb_apn *) vty->index;
	uint32_t type = get_string_value(pdp_type_names, argv[0]);

	apn->cfg.apn_type_mask |= type;
	return CMD_SUCCESS;
}

DEFUN(cfg_apn_shutdown, cfg_apn_shutdown_cmd,
	"shutdown",
	"Put the APN in administrative shut-down\n")
{
	struct osmobb_apn *apn = (struct osmobb_apn *) vty->index;

	if (!apn->cfg.shutdown) {
		if (apn_stop(apn)) {
			vty_out(vty, "%% Failed to Stop APN%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		apn->cfg.shutdown = true;
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_apn_no_shutdown, cfg_apn_no_shutdown_cmd,
	"no shutdown",
	NO_STR "Remove the APN from administrative shut-down\n")
{
	struct osmobb_apn *apn = (struct osmobb_apn *) vty->index;

	if (apn->cfg.shutdown) {
		if (!apn->cfg.dev_name) {
			vty_out(vty, "%% Failed to start APN, tun-device is not configured%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		if (apn_start(apn) < 0) {
			vty_out(vty, "%% Failed to start APN, check log for details%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		apn->cfg.shutdown = false;
	}

	return CMD_SUCCESS;
}

static void config_write_apn(struct vty *vty, const struct osmobb_apn *apn)
{
	unsigned int i;

	vty_out(vty, " apn %s%s", apn->cfg.name, VTY_NEWLINE);

	if (apn->cfg.dev_name)
		vty_out(vty, "  tun-device %s%s", apn->cfg.dev_name, VTY_NEWLINE);
	if (apn->cfg.dev_netns_name)
		vty_out(vty, "  tun-netns %s%s", apn->cfg.dev_netns_name, VTY_NEWLINE);

	for (i = 0; i < 32; i++) {
		if (!(apn->cfg.apn_type_mask & (UINT32_C(1) << i)))
			continue;
		vty_out(vty, "  type-support %s%s", get_value_string(pdp_type_names, (UINT32_C(1) << i)),
			VTY_NEWLINE);
	}

	/* must be last */
	vty_out(vty, "  %sshutdown%s", apn->cfg.shutdown ? "" : "no ", VTY_NEWLINE);
}

static void config_write_ms(struct vty *vty, const struct osmocom_ms *ms)
{
	struct osmobb_apn *apn;

	vty_out(vty, "ms %s%s", ms->name, VTY_NEWLINE);

	l23_vty_config_write_ms_node_contents(vty, ms, " ");

	llist_for_each_entry(apn, &ms->gprs.apn_list, list)
		config_write_apn(vty, apn);

	l23_vty_config_write_ms_node_contents_final(vty, ms, " ");
}

static int config_write(struct vty *vty)
{
	struct osmocom_ms *ms;
	llist_for_each_entry(ms, &ms_list, entity)
		config_write_ms(vty, ms);
	return CMD_SUCCESS;
}

int modem_vty_init(void)
{
	int rc;

	if ((rc = l23_vty_init(config_write, NULL)) < 0)
		return rc;
	install_element_ve(&l23_show_ms_cmd);
	install_element_ve(&test_grr_tx_chan_req_cmd);
	install_element_ve(&test_llc_unitdata_req_hexpdu_cmd);
	install_element_ve(&test_llc_unitdata_req_gmm_attch_cmd);
	install_element_ve(&test_gmm_reg_attach_cmd);
	install_element_ve(&test_gmm_reg_detach_cmd);
	install_element_ve(&test_sm_act_pdp_ctx_cmd);
	install_element(CONFIG_NODE, &l23_cfg_ms_cmd);

	install_element(MS_NODE, &cfg_ms_apn_cmd);
	install_element(MS_NODE, &cfg_ms_no_apn_cmd);
	install_node(&apn_node, NULL);
	install_element(APN_NODE, &cfg_apn_tun_dev_name_cmd);
	install_element(APN_NODE, &cfg_apn_tun_netns_name_cmd);
	install_element(APN_NODE, &cfg_apn_no_tun_netns_name_cmd);
	install_element(APN_NODE, &cfg_apn_type_support_cmd);
	install_element(APN_NODE, &cfg_apn_shutdown_cmd);
	install_element(APN_NODE, &cfg_apn_no_shutdown_cmd);

	return 0;
}
