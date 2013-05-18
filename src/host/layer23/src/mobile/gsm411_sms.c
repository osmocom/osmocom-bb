/*
 * Code based on work of:
 * (C) 2008 by Daniel Willmann <daniel@totalueberwachung.de>
 * (C) 2009 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2010 by On-Waves
 *
 * (C) 2011 by Andreas Eversberg <jolly@eversberg.eu>
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <osmocom/core/msgb.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/transaction.h>
#include <osmocom/bb/mobile/gsm411_sms.h>
#include <osmocom/gsm/gsm0411_utils.h>
#include <osmocom/core/talloc.h>
#include <osmocom/bb/mobile/vty.h>

#define UM_SAPI_SMS 3

extern void *l23_ctx;
static uint32_t new_callref = 0x40000001;

static int gsm411_rl_recv(struct gsm411_smr_inst *inst, int msg_type,
                        struct msgb *msg);
static int gsm411_mn_recv(struct gsm411_smc_inst *inst, int msg_type,
			struct msgb *msg);
static int gsm411_mm_send(struct gsm411_smc_inst *inst, int msg_type,
			struct msgb *msg, int cp_msg_type);
static int gsm411_mn_send(struct gsm411_smr_inst *inst, int msg_type,
			struct msgb *msg);
/*
 * init / exit
 */

int gsm411_sms_init(struct osmocom_ms *ms)
{
	LOGP(DLSMS, LOGL_INFO, "init SMS\n");

	return 0;
}

int gsm411_sms_exit(struct osmocom_ms *ms)
{
	struct gsm_trans *trans, *trans2;

	LOGP(DLSMS, LOGL_INFO, "exit SMS processes for %s\n", ms->name);

	llist_for_each_entry_safe(trans, trans2, &ms->trans_list, entry) {
		if (trans->protocol == GSM48_PDISC_SMS) {
			LOGP(DLSMS, LOGL_NOTICE, "Free pendig "
				"SMS-transaction.\n");
			trans_free(trans);
		}
	}

	return 0;
}

/*
 * SMS content
 */

struct gsm_sms *sms_alloc(void)
{
	return talloc_zero(l23_ctx, struct gsm_sms);
}

void sms_free(struct gsm_sms *sms)
{
	talloc_free(sms);
}

struct gsm_sms *sms_from_text(const char *receiver, int dcs, const char *text)
{
	struct gsm_sms *sms = sms_alloc();

	if (!sms)
		return NULL;

	strncpy(sms->text, text, sizeof(sms->text)-1);

	/* FIXME: don't use ID 1 static */
	sms->reply_path_req = 0;
	sms->status_rep_req = 0;
	sms->ud_hdr_ind = 0;
	sms->protocol_id = 0; /* implicit */
	sms->data_coding_scheme = dcs;
	strncpy(sms->address, receiver, sizeof(sms->address)-1);
	/* Generate user_data */
	sms->user_data_len = gsm_7bit_encode(sms->user_data, sms->text);

	return sms;
}

static int gsm411_sms_report(struct osmocom_ms *ms, struct gsm_sms *sms,
	uint8_t cause)
{
	vty_notify(ms, NULL);
	if (!cause)
		vty_notify(ms, "SMS to %s successfull\n", sms->address);
	else
		vty_notify(ms, "SMS to %s failed: %s\n", sms->address,
			get_value_string(gsm411_rp_cause_strs, cause));

	return 0;
}
/*
 * transaction
 */

/* SMS Specific transaction release.
 * gets called by trans_free, DO NOT CALL YOURSELF!
 */
void _gsm411_sms_trans_free(struct gsm_trans *trans)
{
	gsm411_smr_clear(&trans->sms.smr_inst);
	gsm411_smc_clear(&trans->sms.smc_inst);

	if (trans->sms.sms) {
		LOGP(DLSMS, LOGL_ERROR, "Transaction contains SMS.\n");
		gsm411_sms_report(trans->ms, trans->sms.sms,
			GSM411_RP_CAUSE_MO_SMS_REJECTED);
		sms_free(trans->sms.sms);
		trans->sms.sms = NULL;
	}
}

/* release MM connection, free transaction */
static int gsm411_trans_free(struct gsm_trans *trans)
{
	struct msgb *nmsg;

	/* release MM connection */
	nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSMS_REL_REQ, trans->callref,
				trans->transaction_id, trans->sms.sapi);
	if (!nmsg)
		return -ENOMEM;
	LOGP(DLSMS, LOGL_INFO, "Sending MMSMS_REL_REQ\n");
	gsm48_mmxx_downmsg(trans->ms, nmsg);

	trans->callref = 0;
	trans_free(trans);

	return 0;
}

/*
 * receive SMS
 */

/* now here comes our SMS */
static int gsm340_rx_sms_deliver(struct osmocom_ms *ms, struct msgb *msg,
	struct gsm_sms *gsms)
{
	const char osmocomsms[] = ".osmocom/bb/sms.txt";
	int len;
	const char *home;
	char *sms_file;
	char vty_text[sizeof(gsms->text)], *p;
	FILE *fp;

	/* remove linefeeds and show at VTY */
	strcpy(vty_text, gsms->text);
	for (p = vty_text; *p; p++) {
		if (*p == '\n' || *p == '\r')
			*p = ' ';
	}
	vty_notify(ms, NULL);
	vty_notify(ms, "SMS from %s: '%s'\n", gsms->address, vty_text);

	home = getenv("HOME");
        if (!home) {
fail:
		fprintf(stderr, "Can't deliver SMS, be sure to create '%s' in "
			"your home directory.\n", osmocomsms);
		return GSM411_RP_CAUSE_MT_MEM_EXCEEDED;
	}
	len = strlen(home) + 1 + sizeof(osmocomsms);
	sms_file = talloc_size(l23_ctx, len);
	if (!sms_file)
		goto fail;
	snprintf(sms_file, len, "%s/%s", home, osmocomsms);

	fp = fopen(sms_file, "a");
	if (!fp)
		goto fail;
	fprintf(fp, "[SMS from %s]\n%s\n", gsms->address, gsms->text);
	fclose(fp);

	talloc_free(sms_file);

	return 0;
}

/* process an incoming TPDU (called from RP-DATA)
 * return value > 0: RP CAUSE for ERROR; < 0: silent error; 0 = success */
static int gsm340_rx_tpdu(struct gsm_trans *trans, struct msgb *msg)
{
	uint8_t *smsp = msgb_sms(msg);
	struct gsm_sms *gsms;
	unsigned int sms_alphabet;
	uint8_t sms_mti, sms_mms;
	uint8_t oa_len_bytes;
	uint8_t address_lv[12]; /* according to 03.40 / 9.1.2.5 */
	int rc = 0;

	gsms = sms_alloc();

	/* invert those fields where 0 means active/present */
	sms_mti = *smsp & 0x03;
	sms_mms = !!(*smsp & 0x04);
	gsms->status_rep_req = (*smsp & 0x20);
	gsms->ud_hdr_ind = (*smsp & 0x40);
	gsms->reply_path_req  = (*smsp & 0x80);
	smsp++;

	/* length in bytes of the originate address */
	oa_len_bytes = 2 + *smsp/2 + *smsp%2;
	if (oa_len_bytes > 12) {
		LOGP(DLSMS, LOGL_ERROR, "Originate Address > 12 bytes ?!?\n");
		rc = GSM411_RP_CAUSE_SEMANT_INC_MSG;
		goto out;
	}
	memset(address_lv, 0, sizeof(address_lv));
	memcpy(address_lv, smsp, oa_len_bytes);
	/* mangle first byte to reflect length in bytes, not digits */
	address_lv[0] = oa_len_bytes - 1;
	/* convert to real number */
	if (((smsp[1] & 0x70) >> 4) == 1)
		strcpy(gsms->address, "+");
	else if (((smsp[1] & 0x70) >> 4) == 2)
		strcpy(gsms->address, "0");
	else
		gsms->address[0] = '\0';
	gsm48_decode_bcd_number(gsms->address + strlen(gsms->address),
		sizeof(gsms->address) - strlen(gsms->address), address_lv, 1);
	smsp += oa_len_bytes;

	gsms->protocol_id = *smsp++;
	gsms->data_coding_scheme = *smsp++;

	sms_alphabet = gsm338_get_sms_alphabet(gsms->data_coding_scheme);
	if (sms_alphabet == 0xffffffff) {
		sms_free(gsms);
		return GSM411_RP_CAUSE_MO_NET_OUT_OF_ORDER;
	}

	/* get timestamp */
	gsms->time = gsm340_scts(smsp);
	smsp += 7;

	/* user data */
	gsms->user_data_len = *smsp++;
	if (gsms->user_data_len) {
		memcpy(gsms->user_data, smsp, gsms->user_data_len);

		switch (sms_alphabet) {
		case DCS_7BIT_DEFAULT:
			gsm_7bit_decode(gsms->text, smsp, gsms->user_data_len);
			break;
		case DCS_8BIT_DATA:
		case DCS_UCS2:
		case DCS_NONE:
			break;
		}
	}

	LOGP(DLSMS, LOGL_INFO, "RX SMS: MTI: 0x%02x, "
	     "MR: 0x%02x PID: 0x%02x, DCS: 0x%02x, OA: %s, "
	     "UserDataLength: 0x%02x, UserData: \"%s\"\n",
	     sms_mti, gsms->msg_ref,
	     gsms->protocol_id, gsms->data_coding_scheme, gsms->address,
	     gsms->user_data_len,
			sms_alphabet == DCS_7BIT_DEFAULT ? gsms->text :
				osmo_hexdump(gsms->user_data,
						gsms->user_data_len));

	switch (sms_mti) {
	case GSM340_SMS_DELIVER_SC2MS:
		/* MS is receiving an SMS */
		rc = gsm340_rx_sms_deliver(trans->ms, msg, gsms);
		break;
	case GSM340_SMS_STATUS_REP_SC2MS:
	case GSM340_SMS_SUBMIT_REP_SC2MS:
		LOGP(DLSMS, LOGL_NOTICE, "Unimplemented MTI 0x%02x\n", sms_mti);
		rc = GSM411_RP_CAUSE_IE_NOTEXIST;
		break;
	default:
		LOGP(DLSMS, LOGL_NOTICE, "Undefined MTI 0x%02x\n", sms_mti);
		rc = GSM411_RP_CAUSE_IE_NOTEXIST;
		break;
	}

out:
	sms_free(gsms);

	return rc;
}

static int gsm411_send_rp_ack(struct gsm_trans *trans, uint8_t msg_ref)
{
	struct msgb *msg = gsm411_msgb_alloc();

	LOGP(DLSMS, LOGL_INFO, "TX: SMS RP ACK\n");

	gsm411_push_rp_header(msg, GSM411_MT_RP_ACK_MO, msg_ref);
	return gsm411_smr_send(&trans->sms.smr_inst, GSM411_SM_RL_REPORT_REQ,
		msg);
}

static int gsm411_send_rp_error(struct gsm_trans *trans,
				uint8_t msg_ref, uint8_t cause)
{
	struct msgb *msg = gsm411_msgb_alloc();

	msgb_tv_put(msg, 1, cause);

	LOGP(DLSMS, LOGL_NOTICE, "TX: SMS RP ERROR, cause %d (%s)\n", cause,
		get_value_string(gsm411_rp_cause_strs, cause));

	gsm411_push_rp_header(msg, GSM411_MT_RP_ERROR_MO, msg_ref);
	return gsm411_smr_send(&trans->sms.smr_inst, GSM411_SM_RL_REPORT_REQ,
		msg);
}

/* Receive a 04.11 TPDU inside RP-DATA / user data */
static int gsm411_rx_rp_ud(struct msgb *msg, struct gsm_trans *trans,
			  struct gsm411_rp_hdr *rph,
			  uint8_t src_len, uint8_t *src,
			  uint8_t dst_len, uint8_t *dst,
			  uint8_t tpdu_len, uint8_t *tpdu)
{
	int rc = 0;

	if (dst_len && dst)
		LOGP(DLSMS, LOGL_ERROR, "RP-DATA (MT) with DST ?!?\n");

	if (!src_len || !src || !tpdu_len || !tpdu) {
		LOGP(DLSMS, LOGL_ERROR,
			"RP-DATA (MO) without DST or TPDU ?!?\n");
		gsm411_send_rp_error(trans, rph->msg_ref,
				     GSM411_RP_CAUSE_INV_MAND_INF);
		return -EIO;
	}
	msg->l4h = tpdu;

	LOGP(DLSMS, LOGL_INFO, "DST(%u,%s)\n", src_len,
		osmo_hexdump(src, src_len));
	LOGP(DLSMS, LOGL_INFO, "TPDU(%li,%s)\n", msg->tail-msg->l4h,
		osmo_hexdump(msg->l4h, msg->tail-msg->l4h));

	rc = gsm340_rx_tpdu(trans, msg);
	if (rc == 0)
		return gsm411_send_rp_ack(trans, rph->msg_ref);
	else if (rc > 0)
		return gsm411_send_rp_error(trans, rph->msg_ref, rc);
	else
		return rc;
}

/* Receive a 04.11 RP-DATA message in accordance with Section 7.3.1.2 */
static int gsm411_rx_rp_data(struct msgb *msg, struct gsm_trans *trans,
			     struct gsm411_rp_hdr *rph)
{
	uint8_t src_len, dst_len, rpud_len;
	uint8_t *src = NULL, *dst = NULL , *rp_ud = NULL;

	/* in the MO case, this should always be zero length */
	src_len = rph->data[0];
	if (src_len)
		src = &rph->data[1];

	dst_len = rph->data[1+src_len];
	if (dst_len)
		dst = &rph->data[1+src_len+1];

	rpud_len = rph->data[1+src_len+1+dst_len];
	if (rpud_len)
		rp_ud = &rph->data[1+src_len+1+dst_len+1];

	LOGP(DLSMS, LOGL_INFO, "RX_RP-DATA: src_len=%u, dst_len=%u ud_len=%u\n",
		src_len, dst_len, rpud_len);
	return gsm411_rx_rp_ud(msg, trans, rph, src_len, src, dst_len, dst,
				rpud_len, rp_ud);
}

/* receive RL DATA */
static int gsm411_rx_rl_data(struct msgb *msg, struct gsm48_hdr *gh,
			     struct gsm_trans *trans)
{
	struct gsm411_rp_hdr *rp_data = (struct gsm411_rp_hdr*)&gh->data;
	uint8_t msg_type =  rp_data->msg_type & 0x07;
	int rc = 0;

	switch (msg_type) {
	case GSM411_MT_RP_DATA_MT:
		LOGP(DLSMS, LOGL_INFO, "RX SMS RP-DATA (MT)\n");
		rc = gsm411_rx_rp_data(msg, trans, rp_data);
		break;
	default:
		LOGP(DLSMS, LOGL_NOTICE, "Invalid RP type 0x%02x\n", msg_type);
		gsm411_trans_free(trans);
		rc = -EINVAL;
		break;
	}

	return rc;
}

/*
 * send SMS
 */

/* Receive a 04.11 RP-ACK message (response to RP-DATA from us) */
static int gsm411_rx_rp_ack(struct msgb *msg, struct gsm_trans *trans,
			    struct gsm411_rp_hdr *rph)
{
	struct osmocom_ms *ms = trans->ms;
	struct gsm_sms *sms = trans->sms.sms;

	/* Acnkowledgement to MT RP_DATA, i.e. the MS confirms it
	 * successfully received a SMS.  We can now safely mark it as
	 * transmitted */

	if (!sms) {
		LOGP(DLSMS, LOGL_ERROR, "RX RP-ACK but no sms in "
			"transaction?!?\n");
		return gsm411_send_rp_error(trans, rph->msg_ref,
					    GSM411_RP_CAUSE_PROTOCOL_ERR);
	}

	gsm411_sms_report(ms, sms, 0);

	sms_free(sms);
	trans->sms.sms = NULL;

	return 0;
}

static int gsm411_rx_rp_error(struct msgb *msg, struct gsm_trans *trans,
			      struct gsm411_rp_hdr *rph)
{
	struct osmocom_ms *ms = trans->ms;
	struct gsm_sms *sms = trans->sms.sms;
	uint8_t cause_len = rph->data[0];
	uint8_t cause = rph->data[1];

	/* Error in response to MT RP_DATA, i.e. the MS did not
	 * successfully receive the SMS.  We need to investigate
	 * the cause and take action depending on it */

	LOGP(DLSMS, LOGL_NOTICE, "%s: RX SMS RP-ERROR, cause %d:%d (%s)\n",
	     trans->ms->name, cause_len, cause,
	     get_value_string(gsm411_rp_cause_strs, cause));

	if (!sms) {
		LOGP(DLSMS, LOGL_ERROR,
			"RX RP-ERR, but no sms in transaction?!?\n");
		return -EINVAL;
#if 0
		return gsm411_send_rp_error(trans, rph->msg_ref,
					    GSM411_RP_CAUSE_PROTOCOL_ERR);
#endif
	}

	gsm411_sms_report(ms, sms, cause);

	sms_free(sms);
	trans->sms.sms = NULL;

	return 0;
}

/* receive RL REPORT */
static int gsm411_rx_rl_report(struct msgb *msg, struct gsm48_hdr *gh,
			     struct gsm_trans *trans)
{
	struct gsm411_rp_hdr *rp_data = (struct gsm411_rp_hdr*)&gh->data;
	uint8_t msg_type =  rp_data->msg_type & 0x07;
	int rc = 0;

	switch (msg_type) {
	case GSM411_MT_RP_ACK_MT:
		LOGP(DLSMS, LOGL_INFO, "RX SMS RP-ACK (MT)\n");
		rc = gsm411_rx_rp_ack(msg, trans, rp_data);
		break;
	case GSM411_MT_RP_ERROR_MT:
		LOGP(DLSMS, LOGL_INFO, "RX SMS RP-ERROR (MT)\n");
		rc = gsm411_rx_rp_error(msg, trans, rp_data);
		break;
	default:
		LOGP(DLSMS, LOGL_NOTICE, "Invalid RP type 0x%02x\n", msg_type);
		gsm411_trans_free(trans);
		rc = -EINVAL;
		break;
	}

	return rc;
}

/* generate a msgb containing a TPDU derived from struct gsm_sms,
 * returns total size of TPDU */
static int gsm340_gen_tpdu(struct msgb *msg, struct gsm_sms *sms)
{
	uint8_t *smsp;
	uint8_t da[12];	/* max len per 03.40 */
	uint8_t da_len = 0;
	uint8_t octet_len;
	unsigned int old_msg_len = msg->len;
	uint8_t sms_vpf = GSM340_TP_VPF_NONE;
	uint8_t sms_vp;

	/* generate first octet with masked bits */
	smsp = msgb_put(msg, 1);
	/* TP-MTI (message type indicator) */
	*smsp = GSM340_SMS_SUBMIT_MS2SC;
	/* TP-RD */
	if (0 /* FIXME */)
		*smsp |= 0x04;
	/* TP-VPF */
	*smsp |= (sms_vpf << 3);
	/* TP-SRI(deliver)/SRR(submit) */
	if (sms->status_rep_req)
		*smsp |= 0x20;
	/* TP-UDHI (indicating TP-UD contains a header) */
	if (sms->ud_hdr_ind)
		*smsp |= 0x40;
	/* TP-RP */
	if (sms->reply_path_req)
		*smsp |= 0x80;

	/* generate message ref */
	smsp = msgb_put(msg, 1);
	*smsp = sms->msg_ref;

	/* generate destination address */
	if (sms->address[0] == '+')
		da_len = gsm340_gen_oa(da, sizeof(da), 0x1, 0x1,
							sms->address + 1);
	else
		da_len = gsm340_gen_oa(da, sizeof(da), 0x0, 0x1, sms->address);
	smsp = msgb_put(msg, da_len);
	memcpy(smsp, da, da_len);

	/* generate TP-PID */
	smsp = msgb_put(msg, 1);
	*smsp = sms->protocol_id;

	/* generate TP-DCS */
	smsp = msgb_put(msg, 1);
	*smsp = sms->data_coding_scheme;

	/* generate TP-VP */
	switch (sms_vpf) {
	case GSM340_TP_VPF_NONE:
		sms_vp = 0;
		break;
	default:
		fprintf(stderr, "VPF unsupported, please fix!\n");
		exit(0);
	}
	smsp = msgb_put(msg, sms_vp);

	/* generate TP-UDL */
	smsp = msgb_put(msg, 1);
	*smsp = sms->user_data_len;

	/* generate TP-UD */
	switch (gsm338_get_sms_alphabet(sms->data_coding_scheme)) {
	case DCS_7BIT_DEFAULT:
		octet_len = sms->user_data_len*7/8;
		if (sms->user_data_len*7%8 != 0)
			octet_len++;
		/* Warning, user_data_len indicates the amount of septets
		 * (characters), we need amount of octets occupied */
		smsp = msgb_put(msg, octet_len);
		memcpy(smsp, sms->user_data, octet_len);
		break;
	case DCS_UCS2:
	case DCS_8BIT_DATA:
		smsp = msgb_put(msg, sms->user_data_len);
		memcpy(smsp, sms->user_data, sms->user_data_len);
		break;
	default:
		LOGP(DLSMS, LOGL_NOTICE, "Unhandled Data Coding Scheme: "
			"0x%02X\n", sms->data_coding_scheme);
		break;
	}

	return msg->len - old_msg_len;
}

/* Take a SMS in gsm_sms structure and send it. */
static int gsm411_tx_sms_submit(struct osmocom_ms *ms, const char *sms_sca,
	struct gsm_sms *sms)
{
	struct msgb *msg;
	struct gsm_trans *trans;
	uint8_t *data, *rp_ud_len;
	uint8_t msg_ref = 42;
	int rc;
	int transaction_id;
	uint8_t sca[11];	/* max len per 03.40 */

	LOGP(DLSMS, LOGL_INFO, "..._sms_submit()\n");

	/* no running, no transaction */
	if (!ms->started || ms->shutdown) {
		LOGP(DLSMS, LOGL_ERROR, "Phone is down\n");
		gsm411_sms_report(ms, sms, GSM411_RP_CAUSE_MO_TEMP_FAIL);
		sms_free(sms);
		return -EIO;
	}

	/* allocate transaction with dummy reference */
	transaction_id = trans_assign_trans_id(ms, GSM48_PDISC_SMS, 0);
	if (transaction_id < 0) {
		LOGP(DLSMS, LOGL_ERROR, "No transaction ID available\n");
		gsm411_sms_report(ms, sms, GSM411_RP_CAUSE_MO_CONGESTION);
		sms_free(sms);
		return -ENOMEM;
	}
	trans = trans_alloc(ms, GSM48_PDISC_SMS, transaction_id, new_callref++);
	if (!trans) {
		LOGP(DLSMS, LOGL_ERROR, "No memory for trans\n");
		gsm411_sms_report(ms, sms, GSM411_RP_CAUSE_MO_TEMP_FAIL);
		sms_free(sms);
		return -ENOMEM;
	}
	gsm411_smc_init(&trans->sms.smc_inst, transaction_id, 0,
		gsm411_mn_recv, gsm411_mm_send);
	gsm411_smr_init(&trans->sms.smr_inst, transaction_id, 0,
		gsm411_rl_recv, gsm411_mn_send);
	trans->sms.sms = sms;
	trans->sms.sapi = UM_SAPI_SMS;

	msg = gsm411_msgb_alloc();

	/* no orig Address */
	data = (uint8_t *)msgb_put(msg, 1);
	data[0] = 0x00;	/* originator length == 0 */

	/* Destination Address */
        sca[1] = 0x80; /* no extension */
	sca[1] |= ((sms_sca[0] == '+') ? 0x01 : 0x00) << 4; /* type */
	sca[1] |= 0x1; /* plan*/

	rc = gsm48_encode_bcd_number(sca, sizeof(sca), 1,
				sms_sca + (sms_sca[0] == '+'));
	if (rc < 0) {
error:
		gsm411_sms_report(ms, sms, GSM411_RP_CAUSE_SEMANT_INC_MSG);
		gsm411_trans_free(trans);
		msgb_free(msg);
		return rc;
	}
	data = msgb_put(msg, rc);
	memcpy(data, sca, rc);

	/* obtain a pointer for the rp_ud_len, so we can fill it later */
	rp_ud_len = (uint8_t *)msgb_put(msg, 1);

	/* generate the 03.40 TPDU */
	rc = gsm340_gen_tpdu(msg, sms);
	if (rc < 0)
		goto error;
	*rp_ud_len = rc;

	LOGP(DLSMS, LOGL_INFO, "TX: SMS DELIVER\n");

	gsm411_push_rp_header(msg, GSM411_MT_RP_DATA_MO, msg_ref);
	return gsm411_smr_send(&trans->sms.smr_inst, GSM411_SM_RL_DATA_REQ,
		msg);
}

/* create and send SMS */
int sms_send(struct osmocom_ms *ms, const char *sms_sca, const char *number,
	const char *text)
{
	struct gsm_sms *sms = sms_from_text(number, 0, text);

	if (!sms)
		return -ENOMEM;

	return gsm411_tx_sms_submit(ms, sms_sca, sms);
}

/*
 * message flow between layers
 */

/* push MMSMS header and send to MM */
static int gsm411_to_mm(struct msgb *msg, struct gsm_trans *trans,
	int msg_type)
{
	struct gsm48_mmxx_hdr *mmh;

	/* push RR header */
	msgb_push(msg, sizeof(struct gsm48_mmxx_hdr));
	mmh = (struct gsm48_mmxx_hdr *)msg->data;
	mmh->msg_type = msg_type;
	mmh->ref = trans->callref;
	mmh->transaction_id = trans->transaction_id;
	mmh->sapi = trans->sms.sapi;
	mmh->emergency = 0;

	/* send message to MM */
	LOGP(DLSMS, LOGL_INFO, "Sending '%s' to MM (callref=%x, "
		"transaction_id=%d, sapi=%d)\n", get_mmxx_name(msg_type),
		trans->callref, trans->transaction_id, trans->sms.sapi);
	return gsm48_mmxx_downmsg(trans->ms, msg);
}

/* mm_send: receive MMSMS sap message from SMC */
static int gsm411_mm_send(struct gsm411_smc_inst *inst, int msg_type,
			struct msgb *msg, int cp_msg_type)
{
	struct gsm_trans *trans =
		container_of(inst, struct gsm_trans, sms.smc_inst);
	int rc = 0;

	switch (msg_type) {
	case GSM411_MMSMS_EST_REQ:
		gsm411_to_mm(msg, trans, msg_type);
		break;
	case GSM411_MMSMS_DATA_REQ:
		gsm411_push_cp_header(msg, trans->protocol,
			trans->transaction_id, cp_msg_type);
		msg->l3h = msg->data;
		LOGP(DLSMS, LOGL_INFO, "sending CP message (trans=%x)\n",
			trans->transaction_id);
		rc = gsm411_to_mm(msg, trans, msg_type);
		break;
	case GSM411_MMSMS_REL_REQ:
		LOGP(DLSMS, LOGL_INFO, "Got MMSMS_REL_REQ, destroying "
			"transaction.\n");
		gsm411_to_mm(msg, trans, msg_type);
		gsm411_trans_free(trans);
		break;
	default:
		msgb_free(msg);
		rc = -EINVAL;
	}

	return rc;
}

/* mm_send: receive MNSMS sap message from SMR */
static int gsm411_mn_send(struct gsm411_smr_inst *inst, int msg_type,
			struct msgb *msg)
{
	struct gsm_trans *trans =
		container_of(inst, struct gsm_trans, sms.smr_inst);

	/* forward to SMC */
	return gsm411_smc_send(&trans->sms.smc_inst, msg_type, msg);
}

/* receive SM-RL sap message from SMR
 * NOTE: Message is freed by sender
 */
static int gsm411_rl_recv(struct gsm411_smr_inst *inst, int msg_type,
                        struct msgb *msg)
{
	struct gsm_trans *trans =
		container_of(inst, struct gsm_trans, sms.smr_inst);
	struct gsm48_hdr *gh = msgb_l3(msg);
	int rc = 0;

	switch (msg_type) {
	case GSM411_SM_RL_DATA_IND:
		rc = gsm411_rx_rl_data(msg, gh, trans);
		break;
	case GSM411_SM_RL_REPORT_IND:
		if (!gh)
			LOGP(DLSMS, LOGL_INFO, "Release transaction on empty "
				"report.\n");
		else {
			LOGP(DLSMS, LOGL_INFO, "Release transaction on RL "
				"report.\n");
			rc = gsm411_rx_rl_report(msg, gh, trans);
		}
		break;
	default:
		rc = -EINVAL;
	}

	return rc;
}

/* receive MNSMS sap message from SMC
 * NOTE: Message is freed by sender
 */
static int gsm411_mn_recv(struct gsm411_smc_inst *inst, int msg_type,
			struct msgb *msg)
{
	struct gsm_trans *trans =
		container_of(inst, struct gsm_trans, sms.smc_inst);
	struct gsm48_hdr *gh = msgb_l3(msg);
	int rc = 0;

	switch (msg_type) {
	case GSM411_MNSMS_EST_IND:
	case GSM411_MNSMS_DATA_IND:
		LOGP(DLSMS, LOGL_INFO, "MNSMS-DATA/EST-IND\n");
		rc = gsm411_smr_recv(&trans->sms.smr_inst, msg_type, msg);
		break;
	case GSM411_MNSMS_ERROR_IND:
		if (gh)
			LOGP(DLSMS, LOGL_INFO, "MNSMS-ERROR-IND, cause %d "
				"(%s)\n", gh->data[0],
				get_value_string(gsm411_cp_cause_strs,
							gh->data[0]));
		else
			LOGP(DLSMS, LOGL_INFO, "MNSMS-ERROR-IND, no cause\n");
		rc = gsm411_smr_recv(&trans->sms.smr_inst, msg_type, msg);
		break;
	default:
		rc = -EINVAL;
	}

	return rc;
}

/* receive est/data message from MM layer */
static int gsm411_mmsms_ind(int mmsms_msg, struct gsm_trans *trans,
	struct msgb *msg)
{
	struct osmocom_ms *ms = trans->ms;
	struct gsm48_hdr *gh = msgb_l3(msg);
	int msg_type = gh->msg_type & 0xbf;
	uint8_t transaction_id = ((gh->proto_discr & 0xf0) ^ 0x80) >> 4;
		/* flip */

	/* pull the MMSMS header */
	msgb_pull(msg, sizeof(struct gsm48_mmxx_hdr));

	LOGP(DLSMS, LOGL_INFO, "(ms %s) Received est/data '%u'\n", ms->name,
		msg_type);

	/* 5.4: For MO, if a CP-DATA is received for a new
	 * transaction, equals reception of an implicit
	 * last CP-ACK for previous transaction */
	if (trans->sms.smc_inst.cp_state == GSM411_CPS_IDLE
	 && msg_type == GSM411_MT_CP_DATA) {
		int i;
		struct gsm_trans *ptrans;

		/* Scan through all remote initiated transactions */
		for (i=8; i<15; i++) {
			if (i == transaction_id)
				continue;

			ptrans = trans_find_by_id(ms, GSM48_PDISC_SMS, i);
			if (!ptrans)
				continue;

			LOGP(DLSMS, LOGL_INFO, "Implicit CP-ACK for "
				"trans_id=%x\n", i);

			/* Finish it for good */
			gsm411_trans_free(ptrans);
		}
	}
	return gsm411_smc_recv(&trans->sms.smc_inst, mmsms_msg, msg, msg_type);
}

/* receive message from MM layer */
int gsm411_rcv_sms(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	int msg_type = mmh->msg_type;
	int sapi = mmh->sapi;
	struct gsm_trans *trans;
	int rc = 0;

	trans = trans_find_by_callref(ms, mmh->ref);
	if (!trans) {
		LOGP(DLSMS, LOGL_INFO, " -> (new transaction sapi=%d)\n", sapi);
		trans = trans_alloc(ms, GSM48_PDISC_SMS, mmh->transaction_id,
					mmh->ref);
		if (!trans)
			return -ENOMEM;
		gsm411_smc_init(&trans->sms.smc_inst, trans->transaction_id, 0,
			gsm411_mn_recv, gsm411_mm_send);
		gsm411_smr_init(&trans->sms.smr_inst, trans->transaction_id, 0,
			gsm411_rl_recv, gsm411_mn_send);
		trans->sms.sapi = mmh->sapi;
	}

	LOGP(DLSMS, LOGL_INFO, "(ms %s) Received '%s' from MM\n", ms->name,
		get_mmxx_name(msg_type));

	switch (msg_type) {
	case GSM48_MMSMS_EST_CNF:
		rc = gsm411_smc_recv(&trans->sms.smc_inst, GSM411_MMSMS_EST_CNF,
			msg, 0);
		break;
	case GSM48_MMSMS_EST_IND:
	case GSM48_MMSMS_DATA_IND:
		rc = gsm411_mmsms_ind(msg_type, trans, msg);
		break;
	case GSM48_MMSMS_REL_IND:
	case GSM48_MMSMS_ERR_IND:
		LOGP(DLSMS, LOGL_INFO, "MM connection released.\n");
		trans_free(trans);
		break;
	default:
		LOGP(DLSMS, LOGL_NOTICE, "Message unhandled.\n");
		rc = -ENOTSUP;
	}

	return rc;
}

