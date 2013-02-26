/*
 * l1ctl.h
 *
 * Minimal L1CTL
 *
 * Copyright (C) 2013  Sylvain Munaut <tnt@246tNt.com>
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

#ifndef __TRX_L1CTL_H__
#define __TRX_L1CTL_H__


#include <stdint.h>

#include <l1ctl_proto.h>

struct burst_data;
struct l1ctl_link;
struct msgb;


int l1ctl_tx_reset_req(struct l1ctl_link *l1l, uint8_t type);
int l1ctl_tx_fbsb_req(struct l1ctl_link *l1l,
	uint16_t arfcn, uint8_t flags, uint16_t timeout,
	uint8_t sync_info_idx, uint8_t ccch_mode);
int l1ctl_tx_bts_mode(struct l1ctl_link *l1l,
	uint8_t enabled, uint8_t bsic, uint16_t band_arfcn, int gain);
int l1ctl_tx_bts_burst_req(struct l1ctl_link *l1l,
	uint32_t fn, uint8_t tn, struct burst_data *burst);

int l1ctl_recv(void *data, struct msgb *msg);


#endif /* __TRX_L1CTL_H__ */
