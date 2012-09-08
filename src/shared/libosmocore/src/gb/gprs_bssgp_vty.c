/* VTY interface for our GPRS BSS Gateway Protocol (BSSGP) implementation */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
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

/* FIXME: this should go to some common file as it is copied
 * in vty_interface.c of the BSC */
static const struct value_string gprs_bssgp_timer_strs[] = {
	{ 0, NULL }
};

static void log_set_bvc_filter(struct log_target *target,
				struct bssgp_bvc_ctx *bctx)
{
	if (bctx) {
		target->filter_map |= (1 << FLT_BVC);
		target->filter_data[FLT_BVC] = bctx;
	} else if (target->filter_data[FLT_NSVC]) {
		target->filter_map = ~(1 << FLT_BVC);
		target->filter_data[FLT_BVC] = NULL;
	}
}

static struct cmd_node bssgp_node = {
	L_BSSGP_NODE,
	"%s(bssgp)#",
	1,
};

static int config_write_bssgp(struct vty *vty)
{
	vty_out(vty, "bssgp%s", VTY_NEWLINE);

	return CMD_SUCCESS;
}

DEFUN(cfg_bssgp, cfg_bssgp_cmd,
      "bssgp",
      "Configure the GPRS BSS Gateway Protocol")
{
	vty->node = L_BSSGP_NODE;
	return CMD_SUCCESS;
}

static void dump_bvc(struct vty *vty, struct bssgp_bvc_ctx *bvc, int stats)
{
	vty_out(vty, "NSEI %5u, BVCI %5u, RA-ID: %u-%u-%u-%u, CID: %u, "
		"STATE: %s%s", bvc->nsei, bvc->bvci, bvc->ra_id.mcc,
		bvc->ra_id.mnc, bvc->ra_id.lac, bvc->ra_id.rac, bvc->cell_id,
		bvc->state & BVC_S_BLOCKED ? "BLOCKED" : "UNBLOCKED",
		VTY_NEWLINE);

	if (stats) {
		struct bssgp_flow_control *fc = bvc->fc;

		vty_out_rate_ctr_group(vty, " ", bvc->ctrg);

		if (fc)
			vty_out(vty, "FC-BVC(bucket_max: %uoct, leak_rate: "
				"%uoct/s, cur_tokens: %uoct, max_q_d: %u, "
				"cur_q_d: %u)\n", fc->bucket_size_max,
				fc->bucket_leak_rate, fc->bucket_counter,
				fc->max_queue_depth, fc->queue_depth);
	}
}

static void dump_bssgp(struct vty *vty, int stats)
{
	struct bssgp_bvc_ctx *bvc;

	llist_for_each_entry(bvc, &bssgp_bvc_ctxts, list) {
		dump_bvc(vty, bvc, stats);
	}
}

#define BSSGP_STR "Show information about the BSSGP protocol\n"

DEFUN(show_bssgp, show_bssgp_cmd, "show bssgp",
	SHOW_STR BSSGP_STR)
{
	dump_bssgp(vty, 0);
	return CMD_SUCCESS;
}

DEFUN(show_bssgp_stats, show_bssgp_stats_cmd, "show bssgp stats",
	SHOW_STR BSSGP_STR
	"Include statistics\n")
{
	dump_bssgp(vty, 1);
	return CMD_SUCCESS;
}

DEFUN(show_bvc, show_bvc_cmd, "show bssgp nsei <0-65535> [stats]",
	SHOW_STR BSSGP_STR
	"Show all BVCs on one NSE\n"
	"The NSEI\n" "Include Statistics\n")
{
	struct bssgp_bvc_ctx *bvc;
	uint16_t nsei = atoi(argv[1]);
	int show_stats = 0;

	if (argc >= 2)
		show_stats = 1;

	llist_for_each_entry(bvc, &bssgp_bvc_ctxts, list) {
		if (bvc->nsei != nsei)
			continue;
		dump_bvc(vty, bvc, show_stats);
	}

	return CMD_SUCCESS;
}

DEFUN(logging_fltr_bvc,
      logging_fltr_bvc_cmd,
      "logging filter bvc nsei <0-65535> bvci <0-65535>",
	LOGGING_STR FILTER_STR
	"Filter based on BSSGP Virtual Connection\n"
	"NSEI of the BVC to be filtered\n"
	"Network Service Entity Identifier (NSEI)\n"
	"BVCI of the BVC to be filtered\n"
	"BSSGP Virtual Connection Identifier (BVCI)\n")
{
	struct log_target *tgt = osmo_log_vty2tgt(vty);
	struct bssgp_bvc_ctx *bvc;
	uint16_t nsei = atoi(argv[0]);
	uint16_t bvci = atoi(argv[1]);

	if (!tgt)
		return CMD_WARNING;

	bvc = btsctx_by_bvci_nsei(bvci, nsei);
	if (!bvc) {
		vty_out(vty, "No BVC by that identifier%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	log_set_bvc_filter(tgt, bvc);
	return CMD_SUCCESS;
}

int bssgp_vty_init(void)
{
	install_element_ve(&show_bssgp_cmd);
	install_element_ve(&show_bssgp_stats_cmd);
	install_element_ve(&show_bvc_cmd);
	install_element_ve(&logging_fltr_bvc_cmd);

	install_element(CFG_LOG_NODE, &logging_fltr_bvc_cmd);

	install_element(CONFIG_NODE, &cfg_bssgp_cmd);
	install_node(&bssgp_node, config_write_bssgp);
	install_default(L_BSSGP_NODE);
	install_element(L_BSSGP_NODE, &libgb_exit_cmd);
	install_element(L_BSSGP_NODE, &libgb_end_cmd);
	//install_element(L_BSSGP_NODE, &cfg_bssgp_timer_cmd);

	return 0;
}
