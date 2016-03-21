/*
 * (C) 2016 by Harald Welte <laforge@gnumonks.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <osmocom/sim/sim.h>
#include <osmocom/sim/class_tables.h>

const uint8_t sim_sel_mf[] = { 0xA0, 0xA4, 0x00, 0x00, 0x02, 0x3F, 0x00 };
const uint8_t usim_sel_mf[] = { 0x00, 0xA4, 0x00, 0x00, 0x02, 0x3F, 0x00 };
const uint8_t uicc_tprof[] = { 0x80, 0x10, 0x00, 0x00, 0x02, 0x01, 0x02 };
const uint8_t uicc_tprof_wrong_class[] = { 0x00, 0x10, 0x00, 0x00, 0x02, 0x01, 0x02 };
const uint8_t uicc_read[] = { 0x00, 0xB0, 0x00, 0x00, 0x10 };
const uint8_t uicc_upd[] = { 0x00, 0xD6, 0x00, 0x00, 0x02, 0x01, 0x02 };

#define APDU_CASE_ASSERT(x, y)				\
	do {						\
		printf("Testing " #x "\n");		\
		int rc = osim_determine_apdu_case(&osim_uicc_sim_cic_profile, x);	\
		if (rc != y)							\
			printf("%d (actual) != %d (intended)\n", rc, y);	\
		OSMO_ASSERT(rc == y);						\
	} while (0)

static void test_cla_ins_tbl(void)
{
	APDU_CASE_ASSERT(sim_sel_mf, 4);
	APDU_CASE_ASSERT(usim_sel_mf, 4);
	APDU_CASE_ASSERT(uicc_tprof, 3);
	APDU_CASE_ASSERT(uicc_tprof_wrong_class, 0);
	APDU_CASE_ASSERT(uicc_read, 2);
	APDU_CASE_ASSERT(uicc_upd, 3);
}

int main(int argc, char **argv)
{
	test_cla_ins_tbl();
	return EXIT_SUCCESS;
}
