/* Paging helper code */

/* (C) 2009 by Holger Hans Peter Freyther <zecke@selfish.org>
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

#include <stdint.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/gsm0502.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/gsm/rsl.h>

unsigned int
gsm0502_calc_paging_group(struct gsm48_control_channel_descr *chan_desc, uint64_t imsi)
{
	int ccch_conf;
	int bs_cc_chans;
	int blocks;
	unsigned int group;

	ccch_conf = chan_desc->ccch_conf;
	bs_cc_chans = rsl_ccch_conf_to_bs_cc_chans(ccch_conf);
	/* code word + 2, as 2 channels equals 0x0 */
	blocks = gsm48_number_of_paging_subchannels(chan_desc);
	group = gsm0502_get_paging_group(imsi, bs_cc_chans, blocks);

	return group;
}
