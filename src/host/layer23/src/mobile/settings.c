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

int gsm_settings_init(struct osmocom_ms *ms)
{
	struct gsm_settings *set = &ms->settings;
	struct gsm_support *sup = &ms->support;

	/* IMEI */
	sprintf(set->imei,   "000000000000000");
	sprintf(set->imeisv, "0000000000000000");

	/* test sim */
	strcpy(set->test_imsi, "001010000000000");
	set->test_rplmn_mcc = set->test_rplmn_mnc = 1;

	if (sup->half_v1 || sup->half_v3)
		set->half = 1;

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



