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
 */

#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <osmocom/core/talloc.h>
#include <osmocom/gsm/gsm48.h>

#include <osmocom/bb/common/settings.h>
#include <osmocom/bb/common/utils.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/apn.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/l1l2_interface.h>

/* Used to set default path globally through cmdline */
char *layer2_socket_path = L2_DEFAULT_SOCKET_PATH;

static char *sap_socket_path = "/tmp/osmocom_sap";
static char *mncc_socket_path = "/tmp/ms_mncc";
static char *alsa_dev_default = "default";

int gsm_settings_init(struct osmocom_ms *ms)
{
	struct gsm_settings *set = &ms->settings;
	struct gsm_support *sup = &ms->support;

	strcpy(set->layer2_socket_path, layer2_socket_path);
	strcpy(set->sap_socket_path, sap_socket_path);

	/* Compose MNCC socket path using MS name */
	snprintf(set->mncc_socket_path, sizeof(set->mncc_socket_path) - 1,
		 "%s_%s", mncc_socket_path, ms->name);

	/* Audio settings: drop TCH frames by default */
	set->audio.io_handler = AUDIO_IOH_NONE;
	OSMO_STRLCPY_ARRAY(set->audio.alsa_output_dev, alsa_dev_default);
	OSMO_STRLCPY_ARRAY(set->audio.alsa_input_dev, alsa_dev_default);

	/* Built-in MNCC handler */
	set->mncc_handler = MNCC_HANDLER_INTERNAL;

	/* network search */
	set->plmn_mode = PLMN_MODE_AUTO;

	/* IMEI */
	sprintf(set->imei,   "000000000000000");
	sprintf(set->imeisv, "0000000000000000");

	/* SIM type */
	set->sim_type = GSM_SIM_TYPE_L1PHY;

	/* test SIM */
	OSMO_STRLCPY_ARRAY(set->test_sim.imsi, "001010000000000");
	set->test_sim.rplmn.mcc = 1;
	set->test_sim.rplmn.mnc = 1;
	set->test_sim.rplmn.mnc_3_digits = false;
	set->test_sim.lac = 0x0000;
	set->test_sim.tmsi = GSM_RESERVED_TMSI;

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
	set->class_850 = sup->class_850;
	set->class_pcs = sup->class_pcs;
	set->class_400 = sup->class_400;
	set->full_v1 = sup->full_v1;
	set->full_v2 = sup->full_v2;
	set->full_v3 = sup->full_v3;
	set->half_v1 = sup->half_v1;
	set->half_v3 = sup->half_v3;
	set->ch_cap = sup->ch_cap;
	set->min_rxlev_dbm = sup->min_rxlev_dbm;
	set->dsc_max = sup->dsc_max;
	set->vgcs = sup->vgcs;
	set->vbs = sup->vbs;

	if (sup->half_v1 || sup->half_v3)
		set->half = 1;


	/* software features */
	set->cc_dtmf = 1;

	set->any_timeout = MOB_C7_DEFLT_ANY_TIMEOUT;

	set->store_sms = true;

	INIT_LLIST_HEAD(&set->abbrev);

	return 0;
}

int gsm_settings_arfcn(struct osmocom_ms *ms)
{
	int i;
	struct gsm_settings *set = &ms->settings;

	/* set supported frequencies */
	memset(set->freq_map, 0, sizeof(set->freq_map));
	if (set->p_gsm)
		for(i = 1; i <= 124; i++)
			set->freq_map[i >> 3] |= (1 << (i & 7));
	if (set->gsm_850)
		for(i = 128; i <= 251; i++)
			set->freq_map[i >> 3] |= (1 << (i & 7));
	if (set->gsm_450)
		for(i = 259; i <= 293; i++)
			set->freq_map[i >> 3] |= (1 << (i & 7));
	if (set->gsm_480)
		for(i = 306; i <= 340; i++)
			set->freq_map[i >> 3] |= (1 << (i & 7));
	if (set->dcs)
		for(i = 512; i <= 885; i++)
			set->freq_map[i >> 3] |= (1 << (i & 7));
	if (set->pcs)
		for(i = 1024; i <= 1024-512+810; i++)
			set->freq_map[i >> 3] |= (1 << (i & 7));
	if (set->e_gsm) {
		for(i = 975; i <= 1023; i++)
			set->freq_map[i >> 3] |= (1 << (i & 7));
		set->freq_map[0] |= 1;
	}
	if (set->r_gsm)
		for(i = 955; i <= 974; i++)
			set->freq_map[i >> 3] |= (1 << (i & 7));

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
	char rand[16+1];

	if (digits <= 0)
		return 0;
	if (digits > 15)
		digits = 15;

	sprintf(rand, "%08d", layer23_random() % 100000000);
	sprintf(rand + 8, "%07d", layer23_random() % 10000000);

	strcpy(set->imei + 15 - digits, rand + 15 - digits);
	osmo_strlcpy(set->imeisv, set->imei, 15);

	return 0;
}

const struct value_string audio_io_handler_names[] = {
	{ AUDIO_IOH_NONE,	"none" },
	{ AUDIO_IOH_GAPK,	"gapk" },
	{ AUDIO_IOH_L1PHY,	"l1phy" },
	{ AUDIO_IOH_MNCC_SOCK,	"mncc-sock" },
	{ AUDIO_IOH_LOOPBACK,	"loopback" },
	{ 0, NULL }
};

const struct value_string audio_io_format_names[] = {
	{ AUDIO_IOF_RTP,	"rtp" },
	{ AUDIO_IOF_TI,		"ti" },
	{ 0, NULL }
};


int gprs_settings_init(struct osmocom_ms *ms)
{
	struct gprs_settings *set = &ms->gprs;
	INIT_LLIST_HEAD(&set->apn_list);

	return 0;
}

int gprs_settings_fi(struct osmocom_ms *ms)
{
	struct gprs_settings *set = &ms->gprs;
	struct osmobb_apn *apn;
	while ((apn = llist_first_entry_or_null(&set->apn_list, struct osmobb_apn, list))) {
		/* free calls llist_del(): */
		apn_free(apn);
	}
	return 0;
}

struct osmobb_apn *ms_find_apn_by_name(struct osmocom_ms *ms, const char *apn_name)
{
	struct gprs_settings *set = &ms->gprs;
	struct osmobb_apn *apn;

	llist_for_each_entry(apn, &set->apn_list, list) {
		if (strcmp(apn->cfg.name, apn_name) == 0)
			return apn;
	}
	return NULL;
}

int ms_dispatch_all_apn(struct osmocom_ms *ms, uint32_t event, void *data)
{
	struct gprs_settings *set = &ms->gprs;
	int rc = 0;
	struct osmobb_apn *apn;

	llist_for_each_entry(apn, &set->apn_list, list)
		rc |= osmo_fsm_inst_dispatch(apn->fsm.fi, event, data);
	return rc;
}
