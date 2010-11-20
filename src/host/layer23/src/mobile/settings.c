/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
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

#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <osmocore/talloc.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/networks.h>

static char *layer2_socket_path = "/tmp/osmocom_l2";
static char *sap_socket_path = "/tmp/osmocom_sap";

int gsm_settings_init(struct osmocom_ms *ms)
{
	struct gsm_settings *set = &ms->settings;
	struct gsm_support *sup = &ms->support;

	strcpy(set->layer2_socket_path, layer2_socket_path);
	strcpy(set->sap_socket_path, sap_socket_path);

	/* IMEI */
	sprintf(set->imei,   "000000000000000");
	sprintf(set->imeisv, "0000000000000000");

	/* SIM type */
#warning TODO: Enable after SIM reader is available in master branch.
//	set->sim_type = SIM_TYPE_READER;

	/* test SIM */
	strcpy(set->test_imsi, "001010000000000");
	set->test_rplmn_mcc = set->test_rplmn_mnc = 1;
	set->test_lac = 0x0000;
	set->test_tmsi = 0xffffffff;

	/* set all supported features */
	set->sms_ptp = sup->sms_ptp;
	set->a5_1 = sup->a5_1;
	set->a5_2 = sup->a5_2;
	set->a5_3 = sup->a5_3;
	set->a5_4 = sup->a5_4;
	set->a5_5 = sup->a5_5;
	set->a5_6 = sup->a5_6;
	set->a5_7 = sup->a5_7;
	set->p_gsm = sup->p_gsm;
	set->e_gsm = sup->e_gsm;
	set->r_gsm = sup->r_gsm;
	set->dcs = sup->dcs;
	set->class_900 = sup->class_900;
	set->class_dcs = sup->class_dcs;
	set->full_v1 = sup->full_v1;
	set->full_v2 = sup->full_v2;
	set->full_v3 = sup->full_v3;
	set->half_v1 = sup->half_v1;
	set->half_v3 = sup->half_v3;
	set->ch_cap = sup->ch_cap;
	set->min_rxlev_db = sup->min_rxlev_db;
	set->dsc_max = sup->dsc_max;

	if (sup->half_v1 || sup->half_v3)
		set->half = 1;

	/* software features */
	set->cc_dtmf = 1;

	INIT_LLIST_HEAD(&set->abbrev);

	return 0;
}

int gsm_settings_exit(struct osmocom_ms *ms)
{
	struct gsm_settings *set = &ms->settings;
	struct gsm_settings_abbrev *abbrev;

	while (!llist_empty(&set->abbrev)) {
		abbrev = llist_entry(set->abbrev.next,
			struct gsm_settings_abbrev, list);
		llist_del(&abbrev->list);
		talloc_free(abbrev);
	}

	return 0;
}

char *gsm_check_imei(const char *imei, const char *sv)
{
	int i;

	if (!imei || strlen(imei) != 15)
		return "IMEI must have 15 digits!";

	for (i = 0; i < strlen(imei); i++) {
		if (imei[i] < '0' || imei[i] > '9')
			return "IMEI must have digits 0 to 9 only!";
	}

	if (!sv || strlen(sv) != 1)
		return "Software version must have 1 digit!";

	if (sv[0] < '0' || sv[0] > '9')
		return "Software version must have digits 0 to 9 only!";

	return NULL;
}

int gsm_random_imei(struct gsm_settings *set)
{
	int digits = set->imei_random;
	char rand[16];

	if (digits <= 0)
		return 0;
	if (digits > 15)
		digits = 15;

	sprintf(rand, "%08ld", random() % 100000000);
	sprintf(rand + 8, "%07ld", random() % 10000000);

	strcpy(set->imei + 15 - digits, rand + 15 - digits);
	strncpy(set->imeisv, set->imei, 15);
	
	return 0;
}

