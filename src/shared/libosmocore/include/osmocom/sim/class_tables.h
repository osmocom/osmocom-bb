#pragma once

/* simtrace - tables determining APDU case for card emulation
 *
 * (C) 2016 by Harald Welte <laforge@gnumonks.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2, or
 *  any later version as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdint.h>

struct osim_cla_ins_case {
	uint8_t cla;
	uint8_t cla_mask;
	int (*helper)(const struct osim_cla_ins_case *cic, const uint8_t *hdr);
	const uint8_t *ins_tbl;
};

struct osim_cla_ins_card_profile {
	const char	*name;
	const char	*description;
	const struct osim_cla_ins_case *cic_arr;
	unsigned int cic_arr_size;
};

extern const struct osim_cla_ins_card_profile osim_iso7816_cic_profile;
extern const struct osim_cla_ins_card_profile osim_uicc_cic_profile;
extern const struct osim_cla_ins_card_profile osim_uicc_sim_cic_profile;

int osim_determine_apdu_case(const struct osim_cla_ins_card_profile *prof,
			     const uint8_t *hdr);
