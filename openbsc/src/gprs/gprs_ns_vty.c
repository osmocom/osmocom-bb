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

DEFUN(show_ns, show_ns_cmd, "show ns",
      SHOW_STR "Display information about the NS protocol")
{
	struct gprs_ns_inst *nsi = vty_nsi;
	struct gprs_nsvc *nsvc;

	llist_for_each_entry(nsvc, &nsi->gprs_nsvcs, list) {
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
		vty_out_rate_ctr_group(vty, " ", nsvc->ctrg);
	}

	return CMD_SUCCESS;
}


#define NSE_CMD_STR "NS Entity\n" "NS Entity ID (NSEI)\n"

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
	"Delete NS Entity\n"
	"Delete " NSE_CMD_STR)
{
	uint16_t nsei = atoi(argv[0]);
	struct gprs_nsvc *nsvc;

	nsvc = nsvc_by_nsei(vty_nsi, nsei);
	if (!nsvc) {
		vty_out(vty, "No such NSE (%u)%s", nsei, VTY_NEWLINE);
		return CMD_WARNING;
	}

	nsvc_delete(nsvc);

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

int gprs_ns_vty_init(struct gprs_ns_inst *nsi)
{
	vty_nsi = nsi;

	install_element_ve(&show_ns_cmd);

	install_element(CONFIG_NODE, &cfg_ns_cmd);
	install_node(&ns_node, config_write_ns);
	install_default(NS_NODE);
	install_element(NS_NODE, &cfg_nse_nsvci_cmd);
	install_element(NS_NODE, &cfg_nse_remoteip_cmd);
	install_element(NS_NODE, &cfg_nse_remoteport_cmd);
	install_element(NS_NODE, &cfg_nse_remoterole_cmd);
	install_element(NS_NODE, &cfg_no_nse_cmd);
	install_element(NS_NODE, &cfg_ns_timer_cmd);

	return 0;
}
