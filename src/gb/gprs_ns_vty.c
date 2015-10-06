/* VTY interface for our GPRS Networks Service (NS) implementation */

/* (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#include <arpa/inet.h>

#include <osmocom/core/msgb.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/select.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/gprs/gprs_ns.h>
#include <osmocom/gprs/gprs_bssgp.h>

#include <osmocom/vty/vty.h>
#include <osmocom/vty/command.h>
#include <osmocom/vty/logging.h>
#include <osmocom/vty/telnet_interface.h>
#include <osmocom/vty/misc.h>

#include "common_vty.h"

static struct gprs_ns_inst *vty_nsi = NULL;

/* FIXME: this should go to some common file as it is copied
 * in vty_interface.c of the BSC */
static const struct value_string gprs_ns_timer_strs[] = {
	{ 0, "tns-block" },
	{ 1, "tns-block-retries" },
	{ 2, "tns-reset" },
	{ 3, "tns-reset-retries" },
	{ 4, "tns-test" },
	{ 5, "tns-alive" },
	{ 6, "tns-alive-retries" },
	{ 0, NULL }
};

static void log_set_nsvc_filter(struct log_target *target,
				struct gprs_nsvc *nsvc)
{
	if (nsvc) {
		target->filter_map |= (1 << FLT_NSVC);
		target->filter_data[FLT_NSVC] = nsvc;
	} else if (target->filter_data[FLT_NSVC]) {
		target->filter_map = ~(1 << FLT_NSVC);
		target->filter_data[FLT_NSVC] = NULL;
	}
}

static struct cmd_node ns_node = {
	L_NS_NODE,
	"%s(config-ns)# ",
	1,
};

static int config_write_ns(struct vty *vty)
{
	struct gprs_nsvc *nsvc;
	unsigned int i;
	struct in_addr ia;

	vty_out(vty, "ns%s", VTY_NEWLINE);

	llist_for_each_entry(nsvc, &vty_nsi->gprs_nsvcs, list) {
		if (!nsvc->persistent)
			continue;
		vty_out(vty, " nse %u nsvci %u%s",
			nsvc->nsei, nsvc->nsvci, VTY_NEWLINE);
		vty_out(vty, " nse %u remote-role %s%s",
			nsvc->nsei, nsvc->remote_end_is_sgsn ? "sgsn" : "bss",
			VTY_NEWLINE);
		switch (nsvc->ll) {
		case GPRS_NS_LL_UDP:
			vty_out(vty, " nse %u encapsulation udp%s", nsvc->nsei,
				VTY_NEWLINE);
			vty_out(vty, " nse %u remote-ip %s%s",
				nsvc->nsei,
				inet_ntoa(nsvc->ip.bts_addr.sin_addr),
				VTY_NEWLINE);
			vty_out(vty, " nse %u remote-port %u%s",
				nsvc->nsei, ntohs(nsvc->ip.bts_addr.sin_port),
				VTY_NEWLINE);
			break;
		case GPRS_NS_LL_FR_GRE:
			vty_out(vty, " nse %u encapsulation framerelay-gre%s",
				nsvc->nsei, VTY_NEWLINE);
			vty_out(vty, " nse %u remote-ip %s%s",
				nsvc->nsei,
				inet_ntoa(nsvc->frgre.bts_addr.sin_addr),
				VTY_NEWLINE);
			vty_out(vty, " nse %u fr-dlci %u%s",
				nsvc->nsei, ntohs(nsvc->frgre.bts_addr.sin_port),
				VTY_NEWLINE);
		default:
			break;
		}
	}

	for (i = 0; i < ARRAY_SIZE(vty_nsi->timeout); i++)
		vty_out(vty, " timer %s %u%s",
			get_value_string(gprs_ns_timer_strs, i),
			vty_nsi->timeout[i], VTY_NEWLINE);

	if (vty_nsi->nsip.local_ip) {
		ia.s_addr = htonl(vty_nsi->nsip.local_ip);
		vty_out(vty, " encapsulation udp local-ip %s%s",
			inet_ntoa(ia), VTY_NEWLINE);
	}
	if (vty_nsi->nsip.local_port)
		vty_out(vty, " encapsulation udp local-port %u%s",
			vty_nsi->nsip.local_port, VTY_NEWLINE);
	if (vty_nsi->nsip.dscp)
		vty_out(vty, " encapsulation udp dscp %d%s",
			vty_nsi->nsip.dscp, VTY_NEWLINE);

	vty_out(vty, " encapsulation framerelay-gre enabled %u%s",
		vty_nsi->frgre.enabled ? 1 : 0, VTY_NEWLINE);
	if (vty_nsi->frgre.local_ip) {
		ia.s_addr = htonl(vty_nsi->frgre.local_ip);
		vty_out(vty, " encapsulation framerelay-gre local-ip %s%s",
			inet_ntoa(ia), VTY_NEWLINE);
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_ns, cfg_ns_cmd,
      "ns",
      "Configure the GPRS Network Service")
{
	vty->node = L_NS_NODE;
	return CMD_SUCCESS;
}

static void dump_nse(struct vty *vty, struct gprs_nsvc *nsvc, int stats)
{
	vty_out(vty, "NSEI %5u, NS-VC %5u, Remote: %-4s, %5s %9s",
		nsvc->nsei, nsvc->nsvci,
		nsvc->remote_end_is_sgsn ? "SGSN" : "BSS",
		nsvc->state & NSE_S_ALIVE ? "ALIVE" : "DEAD",
		nsvc->state & NSE_S_BLOCKED ? "BLOCKED" : "UNBLOCKED");
	if (nsvc->ll == GPRS_NS_LL_UDP || nsvc->ll == GPRS_NS_LL_FR_GRE)
		vty_out(vty, ", %s %15s:%u",
			nsvc->ll == GPRS_NS_LL_UDP ? "UDP   " : "FR-GRE",
			inet_ntoa(nsvc->ip.bts_addr.sin_addr),
			ntohs(nsvc->ip.bts_addr.sin_port));
	vty_out(vty, "%s", VTY_NEWLINE);
	if (stats) {
		vty_out_rate_ctr_group(vty, " ", nsvc->ctrg);
		vty_out_stat_item_group(vty, " ", nsvc->statg);
	}
}

static void dump_ns(struct vty *vty, struct gprs_ns_inst *nsi, int stats)
{
	struct gprs_nsvc *nsvc;
	struct in_addr ia;

	ia.s_addr = htonl(vty_nsi->nsip.local_ip);
	vty_out(vty, "Encapsulation NS-UDP-IP     Local IP: %s, UDP Port: %u%s",
		inet_ntoa(ia), vty_nsi->nsip.local_port, VTY_NEWLINE);

	ia.s_addr = htonl(vty_nsi->frgre.local_ip);
	vty_out(vty, "Encapsulation NS-FR-GRE-IP  Local IP: %s%s",
		inet_ntoa(ia), VTY_NEWLINE);

	llist_for_each_entry(nsvc, &nsi->gprs_nsvcs, list) {
		if (nsvc == nsi->unknown_nsvc)
			continue;
		dump_nse(vty, nsvc, stats);
	}
}

DEFUN(show_ns, show_ns_cmd, "show ns",
	SHOW_STR "Display information about the NS protocol")
{
	struct gprs_ns_inst *nsi = vty_nsi;
	dump_ns(vty, nsi, 0);
	return CMD_SUCCESS;
}

DEFUN(show_ns_stats, show_ns_stats_cmd, "show ns stats",
	SHOW_STR
	"Display information about the NS protocol\n"
	"Include statistics\n")
{
	struct gprs_ns_inst *nsi = vty_nsi;
	dump_ns(vty, nsi, 1);
	return CMD_SUCCESS;
}

DEFUN(show_nse, show_nse_cmd, "show ns (nsei|nsvc) <0-65535> [stats]",
	SHOW_STR "Display information about the NS protocol\n"
	"Select one NSE by its NSE Identifier\n"
	"Select one NSE by its NS-VC Identifier\n"
	"The Identifier of selected type\n"
	"Include Statistics\n")
{
	struct gprs_ns_inst *nsi = vty_nsi;
	struct gprs_nsvc *nsvc;
	uint16_t id = atoi(argv[1]);
	int show_stats = 0;

	if (!strcmp(argv[0], "nsei"))
		nsvc = gprs_nsvc_by_nsei(nsi, id);
	else
		nsvc = gprs_nsvc_by_nsvci(nsi, id);

	if (!nsvc) {
		vty_out(vty, "No such NS Entity%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (argc >= 3)
		show_stats = 1;

	dump_nse(vty, nsvc, show_stats);
	return CMD_SUCCESS;
}

#define NSE_CMD_STR "Persistent NS Entity\n" "NS Entity ID (NSEI)\n"

DEFUN(cfg_nse_nsvc, cfg_nse_nsvci_cmd,
	"nse <0-65535> nsvci <0-65534>",
	NSE_CMD_STR
	"NS Virtual Connection\n"
	"NS Virtual Connection ID (NSVCI)\n"
	)
{
	uint16_t nsei = atoi(argv[0]);
	uint16_t nsvci = atoi(argv[1]);
	struct gprs_nsvc *nsvc;

	nsvc = gprs_nsvc_by_nsei(vty_nsi, nsei);
	if (!nsvc) {
		nsvc = gprs_nsvc_create(vty_nsi, nsvci);
		nsvc->nsei = nsei;
	}
	nsvc->nsvci = nsvci;
	/* All NSVCs that are explicitly configured by VTY are
	 * marked as persistent so we can write them to the config
	 * file at some later point */
	nsvc->persistent = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_nse_remoteip, cfg_nse_remoteip_cmd,
	"nse <0-65535> remote-ip A.B.C.D",
	NSE_CMD_STR
	"Remote IP Address\n"
	"Remote IP Address\n")
{
	uint16_t nsei = atoi(argv[0]);
	struct gprs_nsvc *nsvc;

	nsvc = gprs_nsvc_by_nsei(vty_nsi, nsei);
	if (!nsvc) {
		vty_out(vty, "No such NSE (%u)%s", nsei, VTY_NEWLINE);
		return CMD_WARNING;
	}
	inet_aton(argv[1], &nsvc->ip.bts_addr.sin_addr);

	return CMD_SUCCESS;

}

DEFUN(cfg_nse_remoteport, cfg_nse_remoteport_cmd,
	"nse <0-65535> remote-port <0-65535>",
	NSE_CMD_STR
	"Remote UDP Port\n"
	"Remote UDP Port Number\n")
{
	uint16_t nsei = atoi(argv[0]);
	uint16_t port = atoi(argv[1]);
	struct gprs_nsvc *nsvc;

	nsvc = gprs_nsvc_by_nsei(vty_nsi, nsei);
	if (!nsvc) {
		vty_out(vty, "No such NSE (%u)%s", nsei, VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (nsvc->ll != GPRS_NS_LL_UDP) {
		vty_out(vty, "Cannot set UDP Port on non-UDP NSE%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	nsvc->ip.bts_addr.sin_port = htons(port);

	return CMD_SUCCESS;
}

DEFUN(cfg_nse_fr_dlci, cfg_nse_fr_dlci_cmd,
	"nse <0-65535> fr-dlci <16-1007>",
	NSE_CMD_STR
	"Frame Relay DLCI\n"
	"Frame Relay DLCI Number\n")
{
	uint16_t nsei = atoi(argv[0]);
	uint16_t dlci = atoi(argv[1]);
	struct gprs_nsvc *nsvc;

	nsvc = gprs_nsvc_by_nsei(vty_nsi, nsei);
	if (!nsvc) {
		vty_out(vty, "No such NSE (%u)%s", nsei, VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (nsvc->ll != GPRS_NS_LL_FR_GRE) {
		vty_out(vty, "Cannot set FR DLCI on non-FR NSE%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	nsvc->frgre.bts_addr.sin_port = htons(dlci);

	return CMD_SUCCESS;
}

DEFUN(cfg_nse_encaps, cfg_nse_encaps_cmd,
	"nse <0-65535> encapsulation (udp|framerelay-gre)",
	NSE_CMD_STR
	"Encapsulation for NS\n"
	"UDP/IP Encapsulation\n" "Frame-Relay/GRE/IP Encapsulation\n")
{
	uint16_t nsei = atoi(argv[0]);
	struct gprs_nsvc *nsvc;

	nsvc = gprs_nsvc_by_nsei(vty_nsi, nsei);
	if (!nsvc) {
		vty_out(vty, "No such NSE (%u)%s", nsei, VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (!strcmp(argv[1], "udp"))
		nsvc->ll = GPRS_NS_LL_UDP;
	else
		nsvc->ll = GPRS_NS_LL_FR_GRE;

	return CMD_SUCCESS;
}


DEFUN(cfg_nse_remoterole, cfg_nse_remoterole_cmd,
	"nse <0-65535> remote-role (sgsn|bss)",
	NSE_CMD_STR
	"Remote NSE Role\n"
	"Remote Peer is SGSN\n"
	"Remote Peer is BSS\n")
{
	uint16_t nsei = atoi(argv[0]);
	struct gprs_nsvc *nsvc;

	nsvc = gprs_nsvc_by_nsei(vty_nsi, nsei);
	if (!nsvc) {
		vty_out(vty, "No such NSE (%u)%s", nsei, VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (!strcmp(argv[1], "sgsn"))
		nsvc->remote_end_is_sgsn = 1;
	else
		nsvc->remote_end_is_sgsn = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_nse, cfg_no_nse_cmd,
	"no nse <0-65535>",
	"Delete Persistent NS Entity\n"
	"Delete " NSE_CMD_STR)
{
	uint16_t nsei = atoi(argv[0]);
	struct gprs_nsvc *nsvc;

	nsvc = gprs_nsvc_by_nsei(vty_nsi, nsei);
	if (!nsvc) {
		vty_out(vty, "No such NSE (%u)%s", nsei, VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (!nsvc->persistent) {
		vty_out(vty, "NSEI %u is not a persistent NSE%s",
			nsei, VTY_NEWLINE);
		return CMD_WARNING;
	}

	nsvc->persistent = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_ns_timer, cfg_ns_timer_cmd,
	"timer " NS_TIMERS " <0-65535>",
	"Network Service Timer\n"
	NS_TIMERS_HELP "Timer Value\n")
{
	int idx = get_string_value(gprs_ns_timer_strs, argv[0]);
	int val = atoi(argv[1]);

	if (idx < 0 || idx >= ARRAY_SIZE(vty_nsi->timeout))
		return CMD_WARNING;

	vty_nsi->timeout[idx] = val;

	return CMD_SUCCESS;
}

#define ENCAPS_STR "NS encapsulation options\n"

DEFUN(cfg_nsip_local_ip, cfg_nsip_local_ip_cmd,
      "encapsulation udp local-ip A.B.C.D",
	ENCAPS_STR "NS over UDP Encapsulation\n"
	"Set the IP address on which we listen for NS/UDP\n"
	"IP Address\n")
{
	struct in_addr ia;

	inet_aton(argv[0], &ia);
	vty_nsi->nsip.local_ip = ntohl(ia.s_addr);

	return CMD_SUCCESS;
}

DEFUN(cfg_nsip_local_port, cfg_nsip_local_port_cmd,
      "encapsulation udp local-port <0-65535>",
	ENCAPS_STR "NS over UDP Encapsulation\n"
	"Set the UDP port on which we listen for NS/UDP\n"
	"UDP port number\n")
{
	unsigned int port = atoi(argv[0]);

	vty_nsi->nsip.local_port = port;

	return CMD_SUCCESS;
}

DEFUN(cfg_nsip_dscp, cfg_nsip_dscp_cmd,
      "encapsulation udp dscp <0-255>",
	ENCAPS_STR "NS over UDP Encapsulation\n"
	"Set DSCP/TOS on the UDP socket\n" "DSCP Value\n")
{
	int dscp = atoi(argv[0]);
	vty_nsi->nsip.dscp = dscp;
	return CMD_SUCCESS;
}

DEFUN(cfg_frgre_local_ip, cfg_frgre_local_ip_cmd,
      "encapsulation framerelay-gre local-ip A.B.C.D",
	ENCAPS_STR "NS over Frame Relay over GRE Encapsulation\n"
	"Set the IP address on which we listen for NS/FR/GRE\n"
	"IP Address\n")
{
	struct in_addr ia;

	if (!vty_nsi->frgre.enabled) {
		vty_out(vty, "FR/GRE is not enabled%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	inet_aton(argv[0], &ia);
	vty_nsi->frgre.local_ip = ntohl(ia.s_addr);

	return CMD_SUCCESS;
}

DEFUN(cfg_frgre_enable, cfg_frgre_enable_cmd,
      "encapsulation framerelay-gre enabled (1|0)",
	ENCAPS_STR "NS over Frame Relay over GRE Encapsulation\n"
	"Enable or disable Frame Relay over GRE\n"
	"Enable\n" "Disable\n")
{
	int enabled = atoi(argv[0]);

	vty_nsi->frgre.enabled = enabled;

	return CMD_SUCCESS;
}

DEFUN(nsvc_nsei, nsvc_nsei_cmd,
	"nsvc (nsei|nsvci) <0-65535> (block|unblock|reset)",
	"Perform an operation on a NSVC\n"
	"NSEI to identify NS-VC Identifier (NS-VCI)\n"
	"NS-VC Identifier (NS-VCI)\n"
	"The NSEI\n"
	"Initiate BLOCK procedure\n"
	"Initiate UNBLOCK procedure\n"
	"Initiate RESET procedure\n")
{
	const char *id_type = argv[0];
	uint16_t id = atoi(argv[1]);
	const char *operation = argv[2];
	struct gprs_nsvc *nsvc;

	if (!strcmp(id_type, "nsei"))
		nsvc = gprs_nsvc_by_nsei(vty_nsi, id);
	else if (!strcmp(id_type, "nsvci"))
		nsvc = gprs_nsvc_by_nsvci(vty_nsi, id);
	else {
		vty_out(vty, "%%No such id_type '%s'%s", id_type, VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (!nsvc) {
		vty_out(vty, "No such %s (%u)%s", id_type, id, VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (!strcmp(operation, "block"))
		gprs_ns_tx_block(nsvc, NS_CAUSE_OM_INTERVENTION);
	else if (!strcmp(operation, "unblock"))
		gprs_ns_tx_unblock(nsvc);
	else if (!strcmp(operation, "reset"))
		gprs_nsvc_reset(nsvc, NS_CAUSE_OM_INTERVENTION);
	else
		return CMD_WARNING;

	return CMD_SUCCESS;
}

DEFUN(logging_fltr_nsvc,
      logging_fltr_nsvc_cmd,
      "logging filter nsvc (nsei|nsvci) <0-65535>",
	LOGGING_STR FILTER_STR
	"Filter based on NS Virtual Connection\n"
	"Identify NS-VC by NSEI\n"
	"Identify NS-VC by NSVCI\n"
	"Numeric identifier\n")
{
	struct log_target *tgt = osmo_log_vty2tgt(vty);
	struct gprs_nsvc *nsvc;
	uint16_t id = atoi(argv[1]);

	if (!tgt)
		return CMD_WARNING;

	if (!strcmp(argv[0], "nsei"))
		nsvc = gprs_nsvc_by_nsei(vty_nsi, id);
	else
		nsvc = gprs_nsvc_by_nsvci(vty_nsi, id);

	if (!nsvc) {
		vty_out(vty, "No NS-VC by that identifier%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	log_set_nsvc_filter(tgt, nsvc);
	return CMD_SUCCESS;
}

int gprs_ns_vty_init(struct gprs_ns_inst *nsi)
{
	vty_nsi = nsi;

	install_element_ve(&show_ns_cmd);
	install_element_ve(&show_ns_stats_cmd);
	install_element_ve(&show_nse_cmd);
	install_element_ve(&logging_fltr_nsvc_cmd);

	install_element(CFG_LOG_NODE, &logging_fltr_nsvc_cmd);

	install_element(CONFIG_NODE, &cfg_ns_cmd);
	install_node(&ns_node, config_write_ns);
	install_default(L_NS_NODE);
	install_element(L_NS_NODE, &libgb_exit_cmd);
	install_element(L_NS_NODE, &libgb_end_cmd);
	install_element(L_NS_NODE, &cfg_nse_nsvci_cmd);
	install_element(L_NS_NODE, &cfg_nse_remoteip_cmd);
	install_element(L_NS_NODE, &cfg_nse_remoteport_cmd);
	install_element(L_NS_NODE, &cfg_nse_fr_dlci_cmd);
	install_element(L_NS_NODE, &cfg_nse_encaps_cmd);
	install_element(L_NS_NODE, &cfg_nse_remoterole_cmd);
	install_element(L_NS_NODE, &cfg_no_nse_cmd);
	install_element(L_NS_NODE, &cfg_ns_timer_cmd);
	install_element(L_NS_NODE, &cfg_nsip_local_ip_cmd);
	install_element(L_NS_NODE, &cfg_nsip_local_port_cmd);
	install_element(L_NS_NODE, &cfg_nsip_dscp_cmd);
	install_element(L_NS_NODE, &cfg_frgre_enable_cmd);
	install_element(L_NS_NODE, &cfg_frgre_local_ip_cmd);

	install_element(ENABLE_NODE, &nsvc_nsei_cmd);

	return 0;
}
