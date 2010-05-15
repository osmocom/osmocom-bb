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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#include <arpa/inet.h>

#include <openbsc/gsm_data.h>
#include <osmocore/msgb.h>
#include <osmocore/tlv.h>
#include <osmocore/talloc.h>
#include <osmocore/select.h>
#include <osmocore/rate_ctr.h>
#include <openbsc/debug.h>
#include <openbsc/signal.h>
#include <openbsc/gprs_ns.h>
#include <openbsc/gprs_bssgp.h>
#include <openbsc/vty.h>

#include <vty/vty.h>
#include <vty/command.h>

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

static struct cmd_node ns_node = {
	NS_NODE,
	"%s(ns)#",
	1,
};

static int config_write_ns(struct vty *vty)
{
	struct gprs_nsvc *nsvc;
	unsigned int i;

	vty_out(vty, "ns%s", VTY_NEWLINE);

	llist_for_each_entry(nsvc, &vty_nsi->gprs_nsvcs, list) {
		if (!nsvc->persistent)
			continue;
		vty_out(vty, " nse %u nsvci %u%s",
			nsvc->nsei, nsvc->nsvci, VTY_NEWLINE);
		vty_out(vty, " nse %u remote-role %s%s",
			nsvc->nsei, nsvc->remote_end_is_sgsn ? "sgsn" : "bss",
			VTY_NEWLINE);
		if (nsvc->nsi->ll == GPRS_NS_LL_UDP) {
			vty_out(vty, " nse %u remote-ip %s%s",
				nsvc->nsei,
				inet_ntoa(nsvc->ip.bts_addr.sin_addr),
				VTY_NEWLINE);
			vty_out(vty, " nse %u remote-port %u%s",
				nsvc->nsei, ntohs(nsvc->ip.bts_addr.sin_port),
				VTY_NEWLINE);
		}
	}

	for (i = 0; i < ARRAY_SIZE(vty_nsi->timeout); i++)
		vty_out(vty, " timer %s %u%s",
			get_value_string(gprs_ns_timer_strs, i),
			vty_nsi->timeout[i], VTY_NEWLINE);

	return CMD_SUCCESS;
}

DEFUN(cfg_ns, cfg_ns_cmd,
      "ns",
      "Configure the GPRS Network Service")
{
	vty->node = NS_NODE;
	return CMD_SUCCESS;
}

static void dump_nse(struct vty *vty, struct gprs_nsvc *nsvc, int stats)
{
	vty_out(vty, "NSEI %5u, NS-VC %5u, Remote: %-4s, %5s %9s",
		nsvc->nsei, nsvc->nsvci,
		nsvc->remote_end_is_sgsn ? "SGSN" : "BSS",
		nsvc->state & NSE_S_ALIVE ? "ALIVE" : "DEAD",
		nsvc->state & NSE_S_BLOCKED ? "BLOCKED" : "UNBLOCKED");
	if (nsvc->nsi->ll == GPRS_NS_LL_UDP)
		vty_out(vty, ", %15s:%u",
			inet_ntoa(nsvc->ip.bts_addr.sin_addr),
			ntohs(nsvc->ip.bts_addr.sin_port));
	vty_out(vty, "%s", VTY_NEWLINE);
	if (stats)
		vty_out_rate_ctr_group(vty, " ", nsvc->ctrg);
}

static void dump_ns(struct vty *vty, struct gprs_ns_inst *nsi, int stats)
{
	struct gprs_nsvc *nsvc;

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
		nsvc = nsvc_by_nsei(nsi, id);
	else
		nsvc = nsvc_by_nsvci(nsi, id);

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

	nsvc = nsvc_by_nsei(vty_nsi, nsei);
	if (!nsvc) {
		nsvc = nsvc_create(vty_nsi, nsvci);
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

	nsvc = nsvc_by_nsei(vty_nsi, nsei);
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

	nsvc = nsvc_by_nsei(vty_nsi, nsei);
	if (!nsvc) {
		vty_out(vty, "No such NSE (%u)%s", nsei, VTY_NEWLINE);
		return CMD_WARNING;
	}

	nsvc->ip.bts_addr.sin_port = htons(port);

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

	nsvc = nsvc_by_nsei(vty_nsi, nsei);
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

	nsvc = nsvc_by_nsei(vty_nsi, nsei);
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

DEFUN(nsvc_nsei, nsvc_nsei_cmd,
	"nsvc nsei <0-65535> (block|unblock|reset)",
	"Perform an operation on a NSVC\n"
	"NS-VC Identifier (NS-VCI)\n"
	"Initiate BLOCK procedure\n"
	"Initiate UNBLOCK procedure\n"
	"Initiate RESET procedure\n")
{
	uint16_t nsvci = atoi(argv[0]);
	const char *operation = argv[1];
	struct gprs_nsvc *nsvc;

	nsvc = nsvc_by_nsei(vty_nsi, nsvci);
	if (!nsvc) {
		vty_out(vty, "No such NSVCI (%u)%s", nsvci, VTY_NEWLINE);
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


int gprs_ns_vty_init(struct gprs_ns_inst *nsi)
{
	vty_nsi = nsi;

	install_element_ve(&show_ns_cmd);
	install_element_ve(&show_ns_stats_cmd);
	install_element_ve(&show_nse_cmd);

	install_element(CONFIG_NODE, &cfg_ns_cmd);
	install_node(&ns_node, config_write_ns);
	install_default(NS_NODE);
	install_element(NS_NODE, &ournode_exit_cmd);
	install_element(NS_NODE, &ournode_end_cmd);
	install_element(NS_NODE, &cfg_nse_nsvci_cmd);
	install_element(NS_NODE, &cfg_nse_remoteip_cmd);
	install_element(NS_NODE, &cfg_nse_remoteport_cmd);
	install_element(NS_NODE, &cfg_nse_remoterole_cmd);
	install_element(NS_NODE, &cfg_no_nse_cmd);
	install_element(NS_NODE, &cfg_ns_timer_cmd);

	install_element(ENABLE_NODE, &nsvc_nsei_cmd);

	return 0;
}
