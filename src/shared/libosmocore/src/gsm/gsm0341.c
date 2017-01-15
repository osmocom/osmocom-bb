/*
 * (C) 2014 by Harald Welte <laforge@gnumonks.org>
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


#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

#include <osmocom/core/talloc.h>
#include <osmocom/gsm/protocol/gsm_03_41.h>

struct gsm341_ms_message *
gsm0341_build_msg(void *ctx, uint8_t geo_scope, uint8_t msg_code,
		  uint8_t update, uint16_t msg_id, uint8_t dcs,
		  uint8_t page_total, uint8_t page_cur,
		  uint8_t *data, uint8_t len)
{
	struct gsm341_ms_message *cbmsg;

	msg_id = htons(msg_id);

	if (len > 88)
		return NULL;

	cbmsg = talloc_zero_size(ctx, sizeof(*cbmsg)+len);
	if (!cbmsg)
		return NULL;

	cbmsg->serial.code_hi = (msg_code >> 4) & 0xF;
	cbmsg->serial.gs = geo_scope;
	cbmsg->serial.update = update;
	cbmsg->serial.code_lo = msg_code & 0xF;
	cbmsg->msg_id = msg_id;
	cbmsg->dcs.group = dcs >> 4;
	cbmsg->dcs.language = dcs & 0xF;
	cbmsg->page.total = page_total;
	cbmsg->page.current = page_cur;
	memcpy(cbmsg->data, data, len);

	return cbmsg;
}
