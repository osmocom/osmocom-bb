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
#include <arpa/inet.h>
#include <osmocom/core/talloc.h>
#include <osmocom/crypt/auth.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/sap_interface.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/mobile/vty.h>

/* enable to get an empty list of forbidden PLMNs, even if stored on SIM.
 * if list is changed, the result is not written back to SIM */
//#define TEST_EMPTY_FPLMN

void *l23_ctx;

static void subscr_sim_query_cb(struct osmocom_ms *ms, struct msgb *msg);
static void subscr_sim_update_cb(struct osmocom_ms *ms, struct msgb *msg);
static void subscr_sim_key_cb(struct osmocom_ms *ms, struct msgb *msg);

/*
 * support
 */

char *gsm_check_imsi(const char *imsi)
{
	int i;

	if (!imsi || strlen(imsi) != 15)
		return "IMSI must have 15 digits!";

	for (i = 0; i < strlen(imsi); i++) {
		if (imsi[i] < '0' || imsi[i] > '9')
			return "IMSI must have digits 0 to 9 only!";
	}

	return NULL;
}

static char *sim_decode_bcd(uint8_t *data, uint8_t length)
{
	int i, j = 0;
	static char result[32], c;

	for (i = 0; i < (length << 1); i++) {
		if ((i & 1))
			c = (data[i >> 1] >> 4);
		else
			c = (data[i >> 1] & 0xf);
		if (c == 0xf)
			break;
		result[j++] = c + '0';
		if (j == sizeof(result) - 1)
			break;
	}
	result[j] = '\0';

	return result;
}

/*
 * init/exit
 */

int gsm_subscr_init(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subscr = &ms->subscr;

	memset(subscr, 0, sizeof(*subscr));
	subscr->ms = ms;

	/* set TMSI / LAC invalid */
	subscr->tmsi = 0xffffffff;
	subscr->lac = 0x0000;

	/* set key invalid */
	subscr->key_seq = 7;

	/* any cell selection timer timeout */
	subscr->any_timeout = 30;

	/* init lists */
	INIT_LLIST_HEAD(&subscr->plmn_list);
	INIT_LLIST_HEAD(&subscr->plmn_na);

	/* open SIM */
	subscr->sim_handle_query = sim_open(ms, subscr_sim_query_cb);
	subscr->sim_handle_update = sim_open(ms, subscr_sim_update_cb);
	subscr->sim_handle_key = sim_open(ms, subscr_sim_key_cb);

	return 0;
}

int gsm_subscr_exit(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct llist_head *lh, *lh2;

	if (subscr->sim_handle_query) {
		sim_close(ms, subscr->sim_handle_query);
		subscr->sim_handle_query = 0;
	}
	if (subscr->sim_handle_update) {
		sim_close(ms, subscr->sim_handle_update);
		subscr->sim_handle_update = 0;
	}
	if (subscr->sim_handle_key) {
		sim_close(ms, subscr->sim_handle_key);
		subscr->sim_handle_key = 0;
	}

	/* flush lists */
	llist_for_each_safe(lh, lh2, &subscr->plmn_list) {
		llist_del(lh);
		talloc_free(lh);
	}
	llist_for_each_safe(lh, lh2, &subscr->plmn_na) {
		llist_del(lh);
		talloc_free(lh);
	}

	return 0;
}

/*
 * test card
 */

/* Attach test card, no SIM must be currently attached */
int gsm_subscr_testcard(struct osmocom_ms *ms, uint16_t mcc, uint16_t mnc,
	uint16_t lac, uint32_t tmsi, uint8_t imsi_attached)
{
	struct gsm_settings *set = &ms->settings;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	char *error;

	if (subscr->sim_valid) {
		LOGP(DMM, LOGL_ERROR, "Cannot insert card, until current card "
			"is detached.\n");
		return -EBUSY;
	}

	error = gsm_check_imsi(set->test_imsi);
	if (error) {
		LOGP(DMM, LOGL_ERROR, "%s\n", error);
		return -EINVAL;
	}

	/* reset subscriber */
	gsm_subscr_exit(ms);
	gsm_subscr_init(ms);

	subscr->sim_type = GSM_SIM_TYPE_TEST;
	sprintf(subscr->sim_name, "test");
	subscr->sim_valid = 1;
	if (imsi_attached && set->test_rplmn_valid) {
		subscr->imsi_attached = imsi_attached;
		subscr->ustate = GSM_SIM_U1_UPDATED;
	} else
		subscr->ustate = GSM_SIM_U2_NOT_UPDATED;
	subscr->acc_barr = set->test_barr; /* we may access barred cell */
	subscr->acc_class = 0xffff; /* we have any access class */
	subscr->plmn_valid = set->test_rplmn_valid;
	subscr->plmn_mcc = mcc;
	subscr->plmn_mnc = mnc;
	subscr->mcc = mcc;
	subscr->mnc = mnc;
	subscr->lac = lac;
	subscr->tmsi = tmsi;
	subscr->always_search_hplmn = set->test_always;
	subscr->t6m_hplmn = 1; /* try to find home network every 6 min */
	strcpy(subscr->imsi, set->test_imsi);

	LOGP(DMM, LOGL_INFO, "(ms %s) Inserting test card (IMSI=%s %s, %s)\n",
		ms->name, subscr->imsi, gsm_imsi_mcc(subscr->imsi),
		gsm_imsi_mnc(subscr->imsi));

	if (subscr->plmn_valid)
		LOGP(DMM, LOGL_INFO, "-> Test card registered to %s %s 0x%04x"
			"(%s, %s)\n", gsm_print_mcc(mcc),
			gsm_print_mnc(mnc), lac, gsm_get_mcc(mcc),
			gsm_get_mnc(mcc, mnc));
	else
		LOGP(DMM, LOGL_INFO, "-> Test card not registered\n");
	if (subscr->imsi_attached)
		LOGP(DMM, LOGL_INFO, "-> Test card attached\n");

	/* insert card */
	nmsg = gsm48_mmr_msgb_alloc(GSM48_MMR_REG_REQ);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmr_downmsg(ms, nmsg);

	return 0;
}

/*
 * sim card
 */

static int subscr_sim_iccid(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	struct gsm_subscriber *subscr = &ms->subscr;

	strcpy(subscr->iccid, sim_decode_bcd(data, length));
	sprintf(subscr->sim_name, "sim-%s", subscr->iccid);
	LOGP(DMM, LOGL_INFO, "received ICCID %s from SIM\n", subscr->iccid);

	return 0;
}

static int subscr_sim_imsi(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	char *imsi;

	/* get actual length */
	if (length < 1)
		return -EINVAL;
	if (data[0] + 1 < length) {
		LOGP(DMM, LOGL_NOTICE, "invalid length = %d\n", length);
		return -EINVAL;
	}
	length = data[0];

	/* decode IMSI, skip first digit (parity) */
	imsi = sim_decode_bcd(data + 1, length);
	if (strlen(imsi) - 1 > GSM_IMSI_LENGTH - 1 || strlen(imsi) - 1 < 6) {
		LOGP(DMM, LOGL_NOTICE, "IMSI invalid length = %zu\n",
			strlen(imsi) - 1);
		return -EINVAL;
	}

	strncpy(subscr->imsi, imsi + 1, sizeof(subscr->imsi) - 1);

	LOGP(DMM, LOGL_INFO, "received IMSI %s from SIM\n", subscr->imsi);

	return 0;
}

static int subscr_sim_loci(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm1111_ef_loci *loci;

	if (length < 11)
		return -EINVAL;
	loci = (struct gsm1111_ef_loci *) data;

	/* TMSI */
	subscr->tmsi = ntohl(loci->tmsi);

	/* LAI */
	gsm48_decode_lai_hex(&loci->lai, &subscr->mcc, &subscr->mnc,
		&subscr->lac);

	/* location update status */
	switch (loci->lupd_status & 0x07) {
	case 0x00:
		subscr->ustate = GSM_SIM_U1_UPDATED;
		break;
	case 0x02:
	case 0x03:
		subscr->ustate = GSM_SIM_U3_ROAMING_NA;
		break;
	default:
		subscr->ustate = GSM_SIM_U2_NOT_UPDATED;
	}

	LOGP(DMM, LOGL_INFO, "received LOCI from SIM (mcc=%s mnc=%s lac=0x%04x "
		"U%d)\n", gsm_print_mcc(subscr->mcc),
		gsm_print_mnc(subscr->mnc), subscr->lac, subscr->ustate);

	return 0;
}

static int subscr_sim_msisdn(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm1111_ef_adn *adn;

	if (length < sizeof(*adn))
		return -EINVAL;
	adn = (struct gsm1111_ef_adn *) (data + length - sizeof(*adn));

	/* empty */
	subscr->msisdn[0] = '\0';
	if (adn->len_bcd <= 1)
		return 0;

	/* number */
	if (((adn->ton_npi & 0x70) >> 4) == 1)
		strcpy(subscr->msisdn, "+");
	if (((adn->ton_npi & 0x70) >> 4) == 2)
		strcpy(subscr->msisdn, "0");
	strncat(subscr->msisdn, sim_decode_bcd(adn->number, adn->len_bcd - 1),
		sizeof(subscr->msisdn) - 2);

	LOGP(DMM, LOGL_INFO, "received MSISDN %s from SIM\n", subscr->msisdn);

	return 0;
}

static int subscr_sim_smsp(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm1111_ef_smsp *smsp;

	if (length < sizeof(*smsp))
		return -EINVAL;
	smsp = (struct gsm1111_ef_smsp *) (data + length - sizeof(*smsp));

	/* empty */
	subscr->sms_sca[0] = '\0';

	/* TS-Service Centre Address */
	if (!(smsp->par_ind & 0x02) && smsp->ts_sca[0] <= 11) {
		if (((smsp->ts_sca[1] & 0x70) >> 4) == 1)
			strcpy(subscr->sms_sca, "+");
		if (((smsp->ts_sca[1] & 0x70) >> 4) == 2)
			strcpy(subscr->sms_sca, "0");
		gsm48_decode_bcd_number(subscr->sms_sca +
			strlen(subscr->sms_sca), sizeof(subscr->sms_sca)
			- strlen(subscr->sms_sca), smsp->ts_sca, 1);
	}

	LOGP(DMM, LOGL_INFO, "received SMSP from SIM (sca=%s)\n",
		subscr->sms_sca);

	return 0;
}

static int subscr_sim_kc(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	struct gsm_subscriber *subscr = &ms->subscr;

	if (length < 9)
		return -EINVAL;

	/* key */
	memcpy(subscr->key, data, 8);

	/* key sequence */
	subscr->key_seq = data[8] & 0x07;

	LOGP(DMM, LOGL_INFO, "received KEY from SIM\n");

	return 0;
}

static int subscr_sim_plmnsel(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm_sub_plmn_list *plmn;
	struct llist_head *lh, *lh2;
	uint8_t lai[5];
	uint16_t dummy_lac;

	/* flush list */
	llist_for_each_safe(lh, lh2, &subscr->plmn_list) {
		llist_del(lh);
		talloc_free(lh);
	}

	while(length >= 3) {
		/* end of list inside mandatory fields */
		if (data[0] == 0xff && data[1] == 0xff && data[2] == 0x0ff)
			break;

		/* add to list */
		plmn = talloc_zero(l23_ctx, struct gsm_sub_plmn_list);
		if (!plmn)
			return -ENOMEM;
		lai[0] = data[0];
		lai[1] = data[1];
		lai[2] = data[2];
		gsm48_decode_lai_hex((struct gsm48_loc_area_id *)lai,
			&plmn->mcc, &plmn->mnc, &dummy_lac);
		llist_add_tail(&plmn->entry, &subscr->plmn_list);

		LOGP(DMM, LOGL_INFO, "received PLMN selector (mcc=%s mnc=%s) "
			"from SIM\n",
			gsm_print_mcc(plmn->mcc), gsm_print_mnc(plmn->mnc));

		data += 3;
		length -= 3;
	}

	return 0;
}

static int subscr_sim_hpplmn(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	struct gsm_subscriber *subscr = &ms->subscr;

	if (length < 1)
		return -EINVAL;

	/* HPLMN search interval */
	subscr->t6m_hplmn = *data; /* multiple of 6 minutes */

	LOGP(DMM, LOGL_INFO, "received HPPLMN %d (%d mins) from SIM\n",
		subscr->t6m_hplmn, subscr->t6m_hplmn * 6);

	return 0;
}

static int subscr_sim_spn(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	int i;

	/* UCS2 code not supported */
	if (length < 17 || data[1] >= 0x80)
		return -ENOTSUP;

	data++;
	for (i = 0; i < 16; i++) {
		if (*data == 0xff)
			break;
		subscr->sim_spn[i] = *data++;
	}
	subscr->sim_spn[i] = '\0';

	LOGP(DMM, LOGL_INFO, "received SPN %s from SIM\n", subscr->sim_spn);

	return 0;
}

static int subscr_sim_acc(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	uint16_t ac;

	if (length < 2)
		return -EINVAL;

	/* cell access */
	memcpy(&ac, data, sizeof(ac));
	subscr->acc_class = ntohs(ac);

	LOGP(DMM, LOGL_INFO, "received ACC %04x from SIM\n", subscr->acc_class);

	return 0;
}

static int subscr_sim_fplmn(struct osmocom_ms *ms, uint8_t *data,
	uint8_t length)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm_sub_plmn_na *na;
	struct llist_head *lh, *lh2;
	uint8_t lai[5];
	uint16_t dummy_lac;

#ifdef TEST_EMPTY_FPLMN
	return 0;
#endif

	/* flush list */
	llist_for_each_safe(lh, lh2, &subscr->plmn_na) {
		llist_del(lh);
		talloc_free(lh);
	}

	while (length >= 3) {
		/* end of list inside mandatory fields */
		if (data[0] == 0xff && data[1] == 0xff && data[2] == 0x0ff)
			break;

		/* add to list */
		na = talloc_zero(l23_ctx, struct gsm_sub_plmn_na);
		if (!na)
			return -ENOMEM;
		lai[0] = data[0];
		lai[1] = data[1];
		lai[2] = data[2];
		gsm48_decode_lai_hex((struct gsm48_loc_area_id *)lai, &na->mcc,
			&na->mnc, &dummy_lac);
		LOGP(DMM, LOGL_INFO, "received Forbidden PLMN %s %s from SIM\n",
			gsm_print_mcc(na->mcc), gsm_print_mnc(na->mnc));
		na->cause = -1; /* must have a value, but SIM stores no cause */
		llist_add_tail(&na->entry, &subscr->plmn_na);

		data += 3;
		length -= 3;
	}
	return 0;
}

static struct subscr_sim_file {
	uint8_t         mandatory;
	uint16_t	path[MAX_SIM_PATH_LENGTH];
	uint16_t	file;
	uint8_t		sim_job;
	int		(*func)(struct osmocom_ms *ms, uint8_t *data,
				uint8_t length);
} subscr_sim_files[] = {
	{ 1, { 0 },         0x2fe2, SIM_JOB_READ_BINARY, subscr_sim_iccid },
	{ 1, { 0x7f20, 0 }, 0x6f07, SIM_JOB_READ_BINARY, subscr_sim_imsi },
	{ 1, { 0x7f20, 0 }, 0x6f7e, SIM_JOB_READ_BINARY, subscr_sim_loci },
	{ 0, { 0x7f20, 0 }, 0x6f20, SIM_JOB_READ_BINARY, subscr_sim_kc },
	{ 0, { 0x7f20, 0 }, 0x6f30, SIM_JOB_READ_BINARY, subscr_sim_plmnsel },
	{ 0, { 0x7f20, 0 }, 0x6f31, SIM_JOB_READ_BINARY, subscr_sim_hpplmn },
	{ 0, { 0x7f20, 0 }, 0x6f46, SIM_JOB_READ_BINARY, subscr_sim_spn },
	{ 0, { 0x7f20, 0 }, 0x6f78, SIM_JOB_READ_BINARY, subscr_sim_acc },
	{ 0, { 0x7f20, 0 }, 0x6f7b, SIM_JOB_READ_BINARY, subscr_sim_fplmn },
	{ 0, { 0x7f10, 0 }, 0x6f40, SIM_JOB_READ_RECORD, subscr_sim_msisdn },
	{ 0, { 0x7f10, 0 }, 0x6f42, SIM_JOB_READ_RECORD, subscr_sim_smsp },
	{ 0, { 0 },         0,      0,                   NULL }
};

/* request file from SIM */
static int subscr_sim_request(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct subscr_sim_file *sf = &subscr_sim_files[subscr->sim_file_index];
	struct msgb *nmsg;
	struct sim_hdr *nsh;
	int i;

	/* we are done, fire up PLMN and cell selection process */
	if (!sf->func) {
		LOGP(DMM, LOGL_INFO, "(ms %s) Done reading SIM card "
			"(IMSI=%s %s, %s)\n", ms->name, subscr->imsi,
			gsm_imsi_mcc(subscr->imsi), gsm_imsi_mnc(subscr->imsi));

		/* if LAI is valid, set RPLMN */
		if (subscr->lac > 0x0000 && subscr->lac < 0xfffe) {
			subscr->plmn_valid = 1;
			subscr->plmn_mcc = subscr->mcc;
			subscr->plmn_mnc = subscr->mnc;
			LOGP(DMM, LOGL_INFO, "-> SIM card registered to %s %s "
				"(%s, %s)\n", gsm_print_mcc(subscr->plmn_mcc),
				gsm_print_mnc(subscr->plmn_mnc),
				gsm_get_mcc(subscr->plmn_mcc),
				gsm_get_mnc(subscr->plmn_mcc,
					subscr->plmn_mnc));
		} else
			LOGP(DMM, LOGL_INFO, "-> SIM card not registered\n");

		/* insert card */
		nmsg = gsm48_mmr_msgb_alloc(GSM48_MMR_REG_REQ);
		if (!nmsg)
			return -ENOMEM;
		gsm48_mmr_downmsg(ms, nmsg);

		return 0;
	}

	/* trigger SIM reading */
	nmsg = gsm_sim_msgb_alloc(subscr->sim_handle_query,
		sf->sim_job);
	if (!nmsg)
		return -ENOMEM;
	nsh = (struct sim_hdr *) nmsg->data;
	i = 0;
	while (sf->path[i]) {
		nsh->path[i] = sf->path[i];
		i++;
	}
	nsh->path[i] = 0; /* end of path */
	nsh->file = sf->file;
	nsh->rec_no = 1;
	nsh->rec_mode = 0x04;
	LOGP(DMM, LOGL_INFO, "Requesting SIM file 0x%04x\n", nsh->file);
	sim_job(ms, nmsg);

	return 0;
}

static void subscr_sim_query_cb(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct sim_hdr *sh = (struct sim_hdr *) msg->data;
	uint8_t *payload = msg->data + sizeof(*sh);
	uint16_t payload_len = msg->len - sizeof(*sh);
	int rc;
	struct subscr_sim_file *sf = &subscr_sim_files[subscr->sim_file_index];
	struct msgb *nmsg;

	/* error handling */
	if (sh->job_type == SIM_JOB_ERROR) {
		uint8_t cause = payload[0];

		switch (cause) {
			/* unlocking required */
		case SIM_CAUSE_PIN1_REQUIRED:
			LOGP(DMM, LOGL_INFO, "PIN is required, %d tries left\n",
				payload[1]);

			vty_notify(ms, NULL);
			vty_notify(ms, "Please give PIN for ICCID %s (you have "
				"%d tries left)\n", subscr->iccid, payload[1]);
			subscr->sim_pin_required = 1;
			break;
		case SIM_CAUSE_PIN1_BLOCKED:
			LOGP(DMM, LOGL_NOTICE, "PIN is blocked\n");

			vty_notify(ms, NULL);
			vty_notify(ms, "PIN is blocked\n");
			if (payload[1]) {
				vty_notify(ms, "Please give PUC for ICCID %s "
					"(you have %d tries left)\n",
					subscr->iccid, payload[1]);
			}
			subscr->sim_pin_required = 1;
			break;
		case SIM_CAUSE_PUC_BLOCKED:
			LOGP(DMM, LOGL_NOTICE, "PUC is blocked\n");

			vty_notify(ms, NULL);
			vty_notify(ms, "PUC is blocked\n");
			subscr->sim_pin_required = 1;
			break;
		default:
			if (sf->func && !sf->mandatory) {
				LOGP(DMM, LOGL_NOTICE, "SIM reading failed, "
					"ignoring!\n");
				goto ignore;
			}
			LOGP(DMM, LOGL_NOTICE, "SIM reading failed\n");

			vty_notify(ms, NULL);
			vty_notify(ms, "SIM failed, replace SIM!\n");

			/* detach simcard */
			subscr->sim_valid = 0;
			nmsg = gsm48_mmr_msgb_alloc(GSM48_MMR_NREG_REQ);
			if (!nmsg)
				return;
			gsm48_mmr_downmsg(ms, nmsg);
		}
		msgb_free(msg);

		return;
	}

	/* if pin was successfully unlocked, then resend request */
	if (subscr->sim_pin_required) {
		subscr->sim_pin_required = 0;
		subscr_sim_request(ms);
		return;
	}

	/* done when nothing more to read. this happens on PIN requests */
	if (!sf->func)
		return;

	/* call function do decode SIM reply */
	rc = sf->func(ms, payload, payload_len);
	if (rc) {
		LOGP(DMM, LOGL_NOTICE, "SIM reading failed, file invalid\n");
		if (subscr_sim_files[subscr->sim_file_index].mandatory) {
			vty_notify(ms, NULL);
			vty_notify(ms, "SIM failed, data invalid, replace "
				"SIM!\n");
			msgb_free(msg);

			return;
		}
	}

ignore:
	msgb_free(msg);

	/* trigger next file */
	subscr->sim_file_index++;
	subscr_sim_request(ms);
}

/* enter PIN */
void gsm_subscr_sim_pin(struct osmocom_ms *ms, char *pin1, char *pin2,
	int8_t mode)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	uint8_t job;

	/* skip, if no real valid SIM */
	if (subscr->sim_type != GSM_SIM_TYPE_READER)
		return;

	switch (mode) {
	case -1:
		job = SIM_JOB_PIN1_DISABLE;
		LOGP(DMM, LOGL_INFO, "disabling PIN %s\n", pin1);
		break;
	case 1:
		job = SIM_JOB_PIN1_ENABLE;
		LOGP(DMM, LOGL_INFO, "enabling PIN %s\n", pin1);
		break;
	case 2:
		job = SIM_JOB_PIN1_CHANGE;
		LOGP(DMM, LOGL_INFO, "changing PIN %s to %s\n", pin1, pin2);
		break;
	case 99:
		job = SIM_JOB_PIN1_UNBLOCK;
		LOGP(DMM, LOGL_INFO, "unblocking PIN %s with PUC %s\n", pin1,
			pin2);
		break;
	default:
		if (!subscr->sim_pin_required) {
			LOGP(DMM, LOGL_ERROR, "No PIN required now\n");
			return;
		}
		LOGP(DMM, LOGL_INFO, "entering PIN %s\n", pin1);
		job = SIM_JOB_PIN1_UNLOCK;
	}

	nmsg = gsm_sim_msgb_alloc(subscr->sim_handle_query, job);
	if (!nmsg)
		return;
	memcpy(msgb_put(nmsg, strlen(pin1) + 1), pin1, strlen(pin1) + 1);
	memcpy(msgb_put(nmsg, strlen(pin2) + 1), pin2, strlen(pin2) + 1);
	sim_job(ms, nmsg);
}

/* Attach SIM reader, no SIM must be currently attached */
int gsm_subscr_simcard(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subscr = &ms->subscr;

	if (subscr->sim_valid) {
		LOGP(DMM, LOGL_ERROR, "Cannot attach card, until current card "
			"is detached.\n");
		return -EBUSY;
	}

	/* reset subscriber */
	gsm_subscr_exit(ms);
	gsm_subscr_init(ms);

	subscr->sim_type = GSM_SIM_TYPE_READER;
	sprintf(subscr->sim_name, "sim");
	subscr->sim_valid = 1;
	subscr->ustate = GSM_SIM_U2_NOT_UPDATED;

	/* start with first index */
	subscr->sim_file_index = 0;
	return subscr_sim_request(ms);
}

/* update plmn not allowed list on SIM */
static int subscr_write_plmn_na(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	struct sim_hdr *nsh;
	struct gsm_sub_plmn_na *na, *nas[4] = { NULL, NULL, NULL, NULL };
	int count = 0, i;
	uint8_t *data;
	uint8_t lai[5];

#ifdef TEST_EMPTY_FPLMN
	return 0;
#endif

	/* skip, if no real valid SIM */
	if (subscr->sim_type != GSM_SIM_TYPE_READER || !subscr->sim_valid)
		return 0;

	/* get tail list from "PLMN not allowed" */
	llist_for_each_entry(na, &subscr->plmn_na, entry) {
		if (count < 4)
			nas[count] = na;
		else {
			nas[0] = nas[1];
			nas[1] = nas[2];
			nas[2] = nas[3];
			nas[3] = na;
		}
		count++;
	}

	/* write to SIM */
	LOGP(DMM, LOGL_INFO, "Updating FPLMN on SIM\n");
	nmsg = gsm_sim_msgb_alloc(subscr->sim_handle_update,
		SIM_JOB_UPDATE_BINARY);
	if (!nmsg)
		return -ENOMEM;
	nsh = (struct sim_hdr *) nmsg->data;
	data = msgb_put(nmsg, 12);
	nsh->path[0] = 0x7f20;
	nsh->path[1] = 0;
	nsh->file = 0x6f7b;
	for (i = 0; i < 4; i++) {
		if (nas[i]) {
			gsm48_encode_lai_hex((struct gsm48_loc_area_id *)lai,
			nas[i]->mcc, nas[i]->mnc, 0);
			*data++ = lai[0];
			*data++ = lai[1];
			*data++ = lai[2];
		} else {
			*data++ = 0xff;
			*data++ = 0xff;
			*data++ = 0xff;
		}
	}
	sim_job(ms, nmsg);

	return 0;
}

/* update LOCI on SIM */
int gsm_subscr_write_loci(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	struct sim_hdr *nsh;
	struct gsm1111_ef_loci *loci;

	/* skip, if no real valid SIM */
	if (subscr->sim_type != GSM_SIM_TYPE_READER || !subscr->sim_valid)
		return 0;

	LOGP(DMM, LOGL_INFO, "Updating LOCI on SIM\n");

	/* write to SIM */
	nmsg = gsm_sim_msgb_alloc(subscr->sim_handle_update,
		SIM_JOB_UPDATE_BINARY);
	if (!nmsg)
		return -ENOMEM;
	nsh = (struct sim_hdr *) nmsg->data;
	nsh->path[0] = 0x7f20;
	nsh->path[1] = 0;
	nsh->file = 0x6f7e;
	loci = (struct gsm1111_ef_loci *)msgb_put(nmsg, sizeof(*loci));

	/* TMSI */
	loci->tmsi = htonl(subscr->tmsi);

	/* LAI */
	gsm48_encode_lai_hex(&loci->lai, subscr->mcc, subscr->mnc, subscr->lac);

	/* TMSI time */
	loci->tmsi_time = 0xff;

	/* location update status */
	switch (subscr->ustate) {
	case GSM_SIM_U1_UPDATED:
		loci->lupd_status = 0x00;
		break;
	case GSM_SIM_U3_ROAMING_NA:
		loci->lupd_status = 0x03;
		break;
	default:
		loci->lupd_status = 0x01;
	}

	sim_job(ms, nmsg);

	return 0;
}

static void subscr_sim_update_cb(struct osmocom_ms *ms, struct msgb *msg)
{
	struct sim_hdr *sh = (struct sim_hdr *) msg->data;
	uint8_t *payload = msg->data + sizeof(*sh);

	/* error handling */
	if (sh->job_type == SIM_JOB_ERROR)
		LOGP(DMM, LOGL_NOTICE, "SIM update failed (cause %d)\n",
			*payload);

	msgb_free(msg);
}

int gsm_subscr_generate_kc(struct osmocom_ms *ms, uint8_t key_seq,
	uint8_t *rand, uint8_t no_sim)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	struct sim_hdr *nsh;

	/* not a SIM */
	if ((subscr->sim_type != GSM_SIM_TYPE_READER
	  && subscr->sim_type != GSM_SIM_TYPE_TEST)
	 || !subscr->sim_valid || no_sim) {
		struct gsm48_mm_event *nmme;

		LOGP(DMM, LOGL_INFO, "Sending dummy authentication response\n");
		nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_AUTH_RESPONSE);
		if (!nmsg)
			return -ENOMEM;
		nmme = (struct gsm48_mm_event *) nmsg->data;
		nmme->sres[0] = 0x12;
		nmme->sres[1] = 0x34;
		nmme->sres[2] = 0x56;
		nmme->sres[3] = 0x78;
		gsm48_mmevent_msg(ms, nmsg);

		return 0;
	}

	/* test SIM */
	if (subscr->sim_type == GSM_SIM_TYPE_TEST) {
		struct gsm48_mm_event *nmme;
		struct gsm_settings *set = &ms->settings;
		static struct osmo_sub_auth_data auth = {
			.type = OSMO_AUTH_TYPE_GSM
		};
		struct osmo_auth_vector _vec;
		struct osmo_auth_vector *vec = &_vec;

		auth.algo = set->test_ki_type;
		memcpy(auth.u.gsm.ki, set->test_ki, sizeof(auth.u.gsm.ki));
		int ret = osmo_auth_gen_vec(vec, &auth, rand);
		if (ret < 0)
			return ret;

		/* store sequence */
		subscr->key_seq = key_seq;
		memcpy(subscr->key, vec->kc, 8);

		LOGP(DMM, LOGL_INFO, "Sending authentication response\n");
		nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_AUTH_RESPONSE);
		if (!nmsg)
			return -ENOMEM;
		nmme = (struct gsm48_mm_event *) nmsg->data;
		memcpy(nmme->sres, vec->sres, 4);
		gsm48_mmevent_msg(ms, nmsg);

		return 0;
	}

	LOGP(DMM, LOGL_INFO, "Generating KEY at SIM\n");

	/* command to SIM */
	nmsg = gsm_sim_msgb_alloc(subscr->sim_handle_key, SIM_JOB_RUN_GSM_ALGO);
	if (!nmsg)
		return -ENOMEM;
	nsh = (struct sim_hdr *) nmsg->data;
	nsh->path[0] = 0x7f20;
	nsh->path[1] = 0;

	/* random */
	memcpy(msgb_put(nmsg, 16), rand, 16);

	/* store sequence */
	subscr->key_seq = key_seq;

	sim_job(ms, nmsg);

	return 0;
}

static void subscr_sim_key_cb(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct sim_hdr *sh = (struct sim_hdr *) msg->data;
	uint8_t *payload = msg->data + sizeof(*sh);
	uint16_t payload_len = msg->len - sizeof(*sh);
	struct msgb *nmsg;
	struct sim_hdr *nsh;
	struct gsm48_mm_event *nmme;
	uint8_t *data;

	/* error handling */
	if (sh->job_type == SIM_JOB_ERROR) {
		LOGP(DMM, LOGL_NOTICE, "key generation on SIM failed "
			"(cause %d)\n", *payload);

		msgb_free(msg);

		return;
	}

	if (payload_len < 12) {
		LOGP(DMM, LOGL_NOTICE, "response from SIM too short\n");
		return;
	}

	/* store key */
	memcpy(subscr->key, payload + 4, 8);

	/* write to SIM */
	LOGP(DMM, LOGL_INFO, "Updating KC on SIM\n");
	nmsg = gsm_sim_msgb_alloc(subscr->sim_handle_update,
		SIM_JOB_UPDATE_BINARY);
	if (!nmsg)
		return;
	nsh = (struct sim_hdr *) nmsg->data;
	nsh->path[0] = 0x7f20;
	nsh->path[1] = 0;
	nsh->file = 0x6f20;
	data = msgb_put(nmsg, 9);
	memcpy(data, subscr->key, 8);
	data[8] = subscr->key_seq;
	sim_job(ms, nmsg);

	/* return signed response */
	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_AUTH_RESPONSE);
	if (!nmsg)
		return;
	nmme = (struct gsm48_mm_event *) nmsg->data;
	memcpy(nmme->sres, payload, 4);
	gsm48_mmevent_msg(ms, nmsg);

	msgb_free(msg);
}

/*
 * detach
 */

/* Detach card */
int gsm_subscr_remove(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;

	if (!subscr->sim_valid) {
		LOGP(DMM, LOGL_ERROR, "Cannot remove card, no card present\n");
		return -EINVAL;
	}

	/* remove card */
	nmsg = gsm48_mmr_msgb_alloc(GSM48_MMR_NREG_REQ);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmr_downmsg(ms, nmsg);

	return 0;
}

/*
 * state and lists
 */

static const char *subscr_ustate_names[] = {
	"U0_NULL",
	"U1_UPDATED",
	"U2_NOT_UPDATED",
	"U3_ROAMING_NA"
};

/* change to new U state */
void new_sim_ustate(struct gsm_subscriber *subscr, int state)
{
	LOGP(DMM, LOGL_INFO, "(ms %s) new state %s -> %s\n", subscr->ms->name,
		subscr_ustate_names[subscr->ustate],
		subscr_ustate_names[state]);

	subscr->ustate = state;
}

/* del forbidden PLMN. if MCC==0, flush complete list */
int gsm_subscr_del_forbidden_plmn(struct gsm_subscriber *subscr, uint16_t mcc,
	uint16_t mnc)
{
	struct gsm_sub_plmn_na *na, *na2;
	int deleted = 0;

	llist_for_each_entry_safe(na, na2, &subscr->plmn_na, entry) {
		if (!mcc || (na->mcc == mcc && na->mnc == mnc)) {
			LOGP(DPLMN, LOGL_INFO, "Delete from list of forbidden "
				"PLMNs (mcc=%s, mnc=%s)\n",
				gsm_print_mcc(mcc), gsm_print_mnc(mnc));
			llist_del(&na->entry);
			talloc_free(na);
			deleted = 1;
			if (mcc)
				break;
		}
	}

	if (deleted) {
		/* update plmn not allowed list on SIM */
		subscr_write_plmn_na(subscr->ms);
	}

	return -EINVAL;
}

/* add forbidden PLMN */
int gsm_subscr_add_forbidden_plmn(struct gsm_subscriber *subscr, uint16_t mcc,
					uint16_t mnc, uint8_t cause)
{
	struct gsm_sub_plmn_na *na;

	/* if already in the list, remove and add to tail */
	gsm_subscr_del_forbidden_plmn(subscr, mcc, mnc);

	LOGP(DPLMN, LOGL_INFO, "Add to list of forbidden PLMNs "
		"(mcc=%s, mnc=%s)\n", gsm_print_mcc(mcc), gsm_print_mnc(mnc));
	na = talloc_zero(l23_ctx, struct gsm_sub_plmn_na);
	if (!na)
		return -ENOMEM;
	na->mcc = mcc;
	na->mnc = mnc;
	na->cause = cause ? : -1; /* cause 0 is not allowed */
	llist_add_tail(&na->entry, &subscr->plmn_na);

	/* don't add Home PLMN to SIM */
	if (subscr->sim_valid && gsm_match_mnc(mcc, mnc, subscr->imsi))
		return -EINVAL;

	/* update plmn not allowed list on SIM */
	subscr_write_plmn_na(subscr->ms);

	return 0;
}

/* search forbidden PLMN */
int gsm_subscr_is_forbidden_plmn(struct gsm_subscriber *subscr, uint16_t mcc,
					uint16_t mnc)
{
	struct gsm_sub_plmn_na *na;

	llist_for_each_entry(na, &subscr->plmn_na, entry) {
		if (na->mcc == mcc && na->mnc == mnc)
			return 1;
	}

	return 0;
}

int gsm_subscr_get_key_seq(struct osmocom_ms *ms, struct gsm_subscriber *subscr)
{
	if (ms->settings.force_rekey)
		return 7;
	else
		return subscr->key_seq;
}

int gsm_subscr_dump_forbidden_plmn(struct osmocom_ms *ms,
			void (*print)(void *, const char *, ...), void *priv)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm_sub_plmn_na *temp;

	print(priv, "MCC    |MNC    |cause\n");
	print(priv, "-------+-------+-------\n");
	llist_for_each_entry(temp, &subscr->plmn_na, entry)
		print(priv, "%s    |%s%s    |#%d\n",
			gsm_print_mcc(temp->mcc), gsm_print_mnc(temp->mnc),
			((temp->mnc & 0x00f) == 0x00f) ? " ":"", temp->cause);

	return 0;
}

/* dump subscriber */
void gsm_subscr_dump(struct gsm_subscriber *subscr,
			void (*print)(void *, const char *, ...), void *priv)
{
	int i;
	struct gsm_sub_plmn_list *plmn_list;
	struct gsm_sub_plmn_na *plmn_na;

	print(priv, "Mobile Subscriber of MS '%s':\n", subscr->ms->name);

	if (!subscr->sim_valid) {
		print(priv, " No SIM present.\n");
		return;
	}

	print(priv, " IMSI: %s\n", subscr->imsi);
	if (subscr->iccid[0])
		print(priv, " ICCID: %s\n", subscr->iccid);
	if (subscr->sim_spn[0])
		print(priv, " Service Provider Name: %s\n", subscr->sim_spn);
	if (subscr->msisdn[0])
		print(priv, " MSISDN: %s\n", subscr->msisdn);
	if (subscr->sms_sca[0])
		print(priv, " SMS Service Center Address: %s\n",
			subscr->sms_sca);
	print(priv, " Status: %s  IMSI %s", subscr_ustate_names[subscr->ustate],
		(subscr->imsi_attached) ? "attached" : "detached");
	if (subscr->tmsi != 0xffffffff)
		print(priv, "  TMSI 0x%08x", subscr->tmsi);
	if (subscr->lac > 0x0000 && subscr->lac < 0xfffe) {
		print(priv, "\n");
		print(priv, "         LAI: MCC %s  MNC %s  LAC 0x%04x  "
			"(%s, %s)\n", gsm_print_mcc(subscr->mcc),
			gsm_print_mnc(subscr->mnc), subscr->lac,
			gsm_get_mcc(subscr->mcc),
			gsm_get_mnc(subscr->mcc, subscr->mnc));
	} else
		print(priv, "  LAI: invalid\n");
	if (subscr->key_seq != 7) {
		print(priv, " Key: sequence %d ", subscr->key_seq);
		for (i = 0; i < sizeof(subscr->key); i++)
			print(priv, " %02x", subscr->key[i]);
		print(priv, "\n");
	}
	if (subscr->plmn_valid)
		print(priv, " Registered PLMN: MCC %s  MNC %s  (%s, %s)\n",
			gsm_print_mcc(subscr->plmn_mcc),
			gsm_print_mnc(subscr->plmn_mnc),
			gsm_get_mcc(subscr->plmn_mcc),
			gsm_get_mnc(subscr->plmn_mcc, subscr->plmn_mnc));
	print(priv, " Access barred cells: %s\n",
		(subscr->acc_barr) ? "yes" : "no");
	print(priv, " Access classes:");
	for (i = 0; i < 16; i++)
		if ((subscr->acc_class & (1 << i)))
			print(priv, " C%d", i);
	print(priv, "\n");
	if (!llist_empty(&subscr->plmn_list)) {
		print(priv, " List of preferred PLMNs:\n");
		print(priv, "        MCC    |MNC\n");
		print(priv, "        -------+-------\n");
		llist_for_each_entry(plmn_list, &subscr->plmn_list, entry)
			print(priv, "        %s    |%s        (%s, %s)\n",
			gsm_print_mcc(plmn_list->mcc),
			gsm_print_mnc(plmn_list->mnc),
			gsm_get_mcc(plmn_list->mcc),
			gsm_get_mnc(plmn_list->mcc, plmn_list->mnc));
	}
	if (!llist_empty(&subscr->plmn_na)) {
		print(priv, " List of forbidden PLMNs:\n");
		print(priv, "        MCC    |MNC    |cause\n");
		print(priv, "        -------+-------+-------\n");
		llist_for_each_entry(plmn_na, &subscr->plmn_na, entry)
			print(priv, "        %s    |%s%s    |#%d        "
				"(%s, %s)\n", gsm_print_mcc(plmn_na->mcc),
				gsm_print_mnc(plmn_na->mnc),
				((plmn_na->mnc & 0x00f) == 0x00f) ? " ":"",
				plmn_na->cause, gsm_get_mcc(plmn_na->mcc),
				gsm_get_mnc(plmn_na->mcc, plmn_na->mnc));
	}
}

/*
 * SAP interface integration
 */

/* Attach SIM card over SAP */
int gsm_subscr_sapcard(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	int rc;

	if (subscr->sim_valid) {
		LOGP(DMM, LOGL_ERROR, "Cannot insert card, until current card "
			"is detached.\n");
		return -EBUSY;
	}

	/* reset subscriber */
	gsm_subscr_exit(ms);
	gsm_subscr_init(ms);

	subscr->sim_type = GSM_SIM_TYPE_SAP;
	sprintf(subscr->sim_name, "sap");
	subscr->sim_valid = 1;

	/* Try to connect to the SAP interface */
	vty_notify(ms, NULL);
	vty_notify(ms, "Connecting to the SAP interface...\n");
	rc = sap_open(ms, ms->settings.sap_socket_path);
	if (rc < 0) {
		LOGP(DSAP, LOGL_ERROR, "Failed during sap_open(), no SAP based SIM reader\n");
		vty_notify(ms, "SAP connection error!\n");
		ms->sap_wq.bfd.fd = -1;

		/* Detach SIM */
		subscr->sim_valid = 0;
		nmsg = gsm48_mmr_msgb_alloc(GSM48_MMR_NREG_REQ);
		if (!nmsg)
			return -ENOMEM;
		gsm48_mmr_downmsg(ms, nmsg);

		return rc;
	}

	return 0;
}

/* Deattach sapcard */
int gsm_subscr_remove_sapcard(struct osmocom_ms *ms)
{
	return sap_close(ms);
}
