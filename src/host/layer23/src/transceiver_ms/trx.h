/*
 * trx.h
 *
 * OpenBTS TRX interface handling
 *
 * Copyright (C) 2014  Sylvain Munaut <tnt@246tNt.com>
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
 */

#ifndef __TRXMS_TRX_H__
#define __TRXMS_TRX_H__


#include <stdint.h>


struct trx {
	/* UDP sockets */
	struct osmo_fd ofd_clk;
	struct osmo_fd ofd_ctrl;
	struct osmo_fd ofd_data;

	/* */
};

struct trx *trx_alloc(const char *addr, uint16_t base_port);
void trx_free(struct trx *trx);

int trx_ctrl_send_cmd(struct trx *trx, const char *cmd, const char *fmt, ...);

#endif /* __TRXMS_TRX_H__ */
