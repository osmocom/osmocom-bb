/*
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
#include <osmocom/bb/mobile/gsm480_ss.h>
#include <osmocom/core/talloc.h>
#include <osmocom/bb/mobile/vty.h>
#include <osmocom/gsm/protocol/gsm_04_80.h>
#include <osmocom/gsm/gsm48.h>

static uint32_t new_callref = 0x80000001;

static int gsm480_to_mm(struct msgb *msg, struct gsm_trans *trans,
	int msg_type);
static const struct value_string gsm480_err_names[] = {
	{ GSM0480_ERR_CODE_UNKNOWN_SUBSCRIBER,
		"UNKNOWN SUBSCRIBER" },
	{ GSM0480_ERR_CODE_ILLEGAL_SUBSCRIBER,
		"ILLEGAL SUBSCRIBER" },
	{ GSM0480_ERR_CODE_BEARER_SERVICE_NOT_PROVISIONED,
		"BEARER SERVICE NOT PROVISIONED" },
	{ GSM0480_ERR_CODE_TELESERVICE_NOT_PROVISIONED,
		"TELESERVICE NOT PROVISIONED" },
	{ GSM0480_ERR_CODE_ILLEGAL_EQUIPMENT,
		"ILLEGAL EQUIPMENT" },
	{ GSM0480_ERR_CODE_CALL_BARRED,
		"CALL BARRED" },
	{ GSM0480_ERR_CODE_ILLEGAL_SS_OPERATION,
		"ILLEGAL SS OPERATION" },
	{ GSM0480_ERR_CODE_SS_ERROR_STATUS,
		"SS ERROR STATUS" },
	{ GSM0480_ERR_CODE_SS_NOT_AVAILABLE,
		"SS NOT AVAILABLE" },
	{ GSM0480_ERR_CODE_SS_SUBSCRIPTION_VIOLATION,
		"SS SUBSCRIPTION VIOLATION" },
	{ GSM0480_ERR_CODE_SS_INCOMPATIBILITY,
		"SS INCOMPATIBILITY" },
	{ GSM0480_ERR_CODE_FACILITY_NOT_SUPPORTED,
		"FACILITY NOT SUPPORTED" },
	{ GSM0480_ERR_CODE_ABSENT_SUBSCRIBER,
		"ABSENT SUBSCRIBER" },
	{ GSM0480_ERR_CODE_SYSTEM_FAILURE,
		"SYSTEM FAILURE" },
	{ GSM0480_ERR_CODE_DATA_MISSING,
		"DATA MISSING" },
	{ GSM0480_ERR_CODE_UNEXPECTED_DATA_VALUE,
		"UNEXPECTED DATA VALUE" },
	{ GSM0480_ERR_CODE_PW_REGISTRATION_FAILURE,
		"PW REGISTRATION FAILURE" },
	{ GSM0480_ERR_CODE_NEGATIVE_PW_CHECK,
		"NEGATIVE PW CHECK" },
	{ GSM0480_ERR_CODE_NUM_PW_ATTEMPTS_VIOLATION,
		"NUM PW ATTEMPTS VIOLATION" },
	{ GSM0480_ERR_CODE_UNKNOWN_ALPHABET,
		"UNKNOWN ALPHABET" },
	{ GSM0480_ERR_CODE_USSD_BUSY,
		"USSD BUSY" },
	{ GSM0480_ERR_CODE_MAX_MPTY_PARTICIPANTS,
		"MAX MPTY PARTICIPANTS" },
	{ GSM0480_ERR_CODE_RESOURCES_NOT_AVAILABLE,
		"RESOURCES NOT AVAILABLE" },
	{0,	NULL }
};

/* taken from Wireshark */
static const struct value_string Teleservice_vals[] = {
	{0x00, "allTeleservices" },
	{0x10, "allSpeechTransmissionServices" },
	{0x11, "telephony" },
	{0x12, "emergencyCalls" },
	{0x20, "allShortMessageServices" },
	{0x21, "shortMessageMT-PP" },
	{0x22, "shortMessageMO-PP" },
	{0x60, "allFacsimileTransmissionServices" },
	{0x61, "facsimileGroup3AndAlterSpeech" },
	{0x62, "automaticFacsimileGroup3" },
	{0x63, "facsimileGroup4" },

	{0x70, "allDataTeleservices" },
	{0x80, "allTeleservices-ExeptSMS" },

	{0x90, "allVoiceGroupCallServices" },
	{0x91, "voiceGroupCall" },
	{0x92, "voiceBroadcastCall" },

	{0xd0, "allPLMN-specificTS" },
	{0xd1, "plmn-specificTS-1" },
	{0xd2, "plmn-specificTS-2" },
	{0xd3, "plmn-specificTS-3" },
	{0xd4, "plmn-specificTS-4" },
	{0xd5, "plmn-specificTS-5" },
	{0xd6, "plmn-specificTS-6" },
	{0xd7, "plmn-specificTS-7" },
	{0xd8, "plmn-specificTS-8" },
	{0xd9, "plmn-specificTS-9" },
	{0xda, "plmn-specificTS-A" },
	{0xdb, "plmn-specificTS-B" },
	{0xdc, "plmn-specificTS-C" },
	{0xdd, "plmn-specificTS-D" },
	{0xde, "plmn-specificTS-E" },
	{0xdf, "plmn-specificTS-F" },
	{ 0, NULL }
};

/* taken from Wireshark */
static const struct value_string Bearerservice_vals[] = {
	{0x00, "allBearerServices" },
	{0x10, "allDataCDA-Services" },
	{0x11, "dataCDA-300bps" },
	{0x12, "dataCDA-1200bps" },
	{0x13, "dataCDA-1200-75bps" },
	{0x14, "dataCDA-2400bps" },
	{0x15, "dataCDA-4800bps" },
	{0x16, "dataCDA-9600bps" },
	{0x17, "general-dataCDA" },

	{0x18, "allDataCDS-Services" },
	{0x1A, "dataCDS-1200bps" },
	{0x1C, "dataCDS-2400bps" },
	{0x1D, "dataCDS-4800bps" },
	{0x1E, "dataCDS-9600bps" },
	{0x1F, "general-dataCDS" },

	{0x20, "allPadAccessCA-Services" },
	{0x21, "padAccessCA-300bps" },
	{0x22, "padAccessCA-1200bps" },
	{0x23, "padAccessCA-1200-75bps" },
	{0x24, "padAccessCA-2400bps" },
	{0x25, "padAccessCA-4800bps" },
	{0x26, "padAccessCA-9600bps" },
	{0x27, "general-padAccessCA" },

	{0x28, "allDataPDS-Services" },
	{0x2C, "dataPDS-2400bps" },
	{0x2D, "dataPDS-4800bps" },
	{0x2E, "dataPDS-9600bps" },
	{0x2F, "general-dataPDS" },

	{0x30, "allAlternateSpeech-DataCDA" },
	{0x38, "allAlternateSpeech-DataCDS" },
	{0x40, "allSpeechFollowedByDataCDA" },
	{0x48, "allSpeechFollowedByDataCDS" },

	{0x50, "allDataCircuitAsynchronous" },
	{0x60, "allAsynchronousServices" },
	{0x58, "allDataCircuitSynchronous" },
	{0x68, "allSynchronousServices" },

	{0xD0, "allPLMN-specificBS" },
	{0xD1, "plmn-specificBS-1" },
	{0xD2, "plmn-specificBS-2" },
	{0xD3, "plmn-specificBS-3" },
	{0xD4, "plmn-specificBS-4" },
	{0xD5, "plmn-specificBS-5" },
	{0xD6, "plmn-specificBS-6" },
	{0xD7, "plmn-specificBS-7" },
	{0xD8, "plmn-specificBS-8" },
	{0xD9, "plmn-specificBS-9" },
	{0xDA, "plmn-specificBS-A" },
	{0xDB, "plmn-specificBS-B" },
	{0xDC, "plmn-specificBS-C" },
	{0xDD, "plmn-specificBS-D" },
	{0xDE, "plmn-specificBS-E" },
	{0xDF, "plmn-specificBS-F" },
	{ 0, NULL }
};

static int gsm480_ss_result(struct osmocom_ms *ms, const char *response,
	uint8_t error)
{
	vty_notify(ms, NULL);
	if (response) {
		char text[256], *t = text, *s;

		strncpy(text, response, sizeof(text) - 1);
		text[sizeof(text) - 1] = '\0';
		while ((s = strchr(text, '\r')))
			*s = '\n';
		while ((s = strsep(&t, "\n"))) {
			vty_notify(ms, "Service response: %s\n", s);
		}
	} else if (error)
		vty_notify(ms, "Service request failed: %s\n",
			get_value_string(gsm480_err_names, error));
	else
		vty_notify(ms, "Service request failed.\n");

	return 0;
}

enum {
	GSM480_SS_ST_IDLE = 0,
	GSM480_SS_ST_REGISTER,
	GSM480_SS_ST_ACTIVE,
};

/*
 * init / exit
 */

int gsm480_ss_init(struct osmocom_ms *ms)
{
	LOGP(DSS, LOGL_INFO, "init SS\n");

	return 0;
}

int gsm480_ss_exit(struct osmocom_ms *ms)
{
	struct gsm_trans *trans, *trans2;

	LOGP(DSS, LOGL_INFO, "exit SS processes for %s\n", ms->name);

	llist_for_each_entry_safe(trans, trans2, &ms->trans_list, entry) {
		if (trans->protocol == GSM48_PDISC_NC_SS) {
			LOGP(DSS, LOGL_NOTICE, "Free pendig "
				"SS-transaction.\n");
			trans_free(trans);
		}
	}

	return 0;
}

/*
 * transaction
 */

/* SS Specific transaction release.
 * gets called by trans_free, DO NOT CALL YOURSELF!
 */
void _gsm480_ss_trans_free(struct gsm_trans *trans)
{
	if (trans->ss.msg) {
		LOGP(DSS, LOGL_INFO, "Free pending SS request\n");
		msgb_free(trans->ss.msg);
		trans->ss.msg = NULL;
	}
	vty_notify(trans->ms, NULL);
	vty_notify(trans->ms, "Service connection terminated.\n");
}

/* release MM connection, free transaction */
static int gsm480_trans_free(struct gsm_trans *trans)
{
	struct msgb *nmsg;

	/* release MM connection */
	nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSS_REL_REQ, trans->callref,
				trans->transaction_id, 0);
	if (!nmsg)
		return -ENOMEM;
	LOGP(DSS, LOGL_INFO, "Sending MMSS_REL_REQ\n");
	gsm48_mmxx_downmsg(trans->ms, nmsg);

	trans->callref = 0;
	trans_free(trans);

	return 0;
}

/*
 * endcoding
 */

#define GSM480_ALLOC_SIZE       512+128
#define GSM480_ALLOC_HEADROOM   128

struct msgb *gsm480_msgb_alloc(void)
{
	return msgb_alloc_headroom(GSM480_ALLOC_SIZE, GSM480_ALLOC_HEADROOM,
		"GSM 04.80");
}

static inline unsigned char *msgb_wrap_with_L(struct msgb *msgb)
{
	uint8_t *data = msgb_push(msgb, 1);

	data[0] = msgb->len - 1;
	return data;
}

/* support function taken from OpenBSC */
static inline unsigned char *msgb_wrap_with_TL(struct msgb *msgb, uint8_t tag)
{
	uint8_t *data = msgb_push(msgb, 2);

	data[0] = tag;
	data[1] = msgb->len - 2;
	return data;
}

static inline void msgb_wrap_with_TL_asn(struct msgb *msg, uint8_t tag)
{
	int len = msg->len;
	uint8_t *data = msgb_push(msg, (len >= 128) ? 3 : 2);

	*data++ = tag;
	if (len >= 128)
		*data++ = 0x81;
	*data = len;
	return;
}

/* support function taken from OpenBSC */
static inline unsigned char *msgb_push_TLV1(struct msgb *msgb, uint8_t tag,
uint8_t value)
{
	uint8_t *data = msgb_push(msgb, 3);

	data[0] = tag;
	data[1] = 1;
	data[2] = value;
	return data;
}

static const char *ss_code_by_char(const char *code, uint8_t *ss_code)
{
	if (!strncmp(code, "21", 2)) {
		*ss_code = 33;
		return code + 2;
	}
	if (!strncmp(code, "67", 2)) {
		*ss_code = 41;
		return code + 2;
	}
	if (!strncmp(code, "61", 2)) {
		*ss_code = 42;
		return code + 2;
	}
	if (!strncmp(code, "62", 2)) {
		*ss_code = 43;
		return code + 2;
	}
	if (!strncmp(code, "002", 3)) {
		*ss_code = 32;
		return code + 3;
	}
	if (!strncmp(code, "004", 3)) {
		*ss_code = 40;
		return code + 3;
	}

	return NULL;
}

static const char *decode_ss_code(uint8_t ss_code)
{
	static char unknown[16];
	
	switch (ss_code) {
	case 33:
		return "CFU";
	case 41:
		return "CFB";
	case 42:
		return "CFNR";
	case 43:
		return "CF Not Reachable";
	case 32:
		return "All CF";
	case 40:
		return "All conditional CF";
	default:
		sprintf(unknown, "Unknown %d", ss_code);
		return unknown;
	}
}

static int gsm480_tx_release_compl(struct gsm_trans *trans, uint8_t cause)
{
	struct msgb *msg;
	struct gsm48_hdr *gh;

	msg = gsm480_msgb_alloc();
	if (!msg)
		return -ENOMEM;

	gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	gh->proto_discr = GSM48_PDISC_NC_SS | (trans->transaction_id << 4);
	gh->msg_type = GSM0480_MTYPE_RELEASE_COMPLETE;

	if (cause) {
		uint8_t *tlv = msgb_put(msg, 4);
		*tlv = GSM48_IE_CAUSE;
		*tlv = 2;
		*tlv = 0x80 | cause;
		*tlv = 0x80 | GSM48_CAUSE_LOC_USER;
	}
	return gsm480_to_mm(msg, trans, GSM48_MMSS_DATA_REQ);
}

static int return_imei(struct osmocom_ms *ms)
{
	char text[32];
	struct gsm_settings *set = &ms->settings;

	sprintf(text, "IMEI: %s SV: %s", set->imei,
		set->imeisv + strlen(set->imei));
	gsm480_ss_result(ms, text, 0);

	return 0;
}

/* prepend invoke-id, facility IE and facility message */
static int gsm480_tx_invoke(struct gsm_trans *trans, struct msgb *msg,
	uint8_t msg_type)
{
	struct gsm48_hdr *gh;

	/* Pre-pend the invoke ID */
        msgb_push_TLV1(msg, GSM0480_COMPIDTAG_INVOKE_ID, trans->ss.invoke_id);

	/* Wrap this up as invoke vomponent */
	if (msg_type == GSM0480_MTYPE_FACILITY)
		msgb_wrap_with_TL_asn(msg, GSM0480_CTYPE_RETURN_RESULT);
	else
		msgb_wrap_with_TL_asn(msg, GSM0480_CTYPE_INVOKE);

	/* Wrap this up as facility IE */
	if (msg_type == GSM0480_MTYPE_FACILITY)
		msgb_wrap_with_L(msg);
	else
		msgb_wrap_with_TL(msg, GSM0480_IE_FACILITY);

	/* FIXME: If phase 2, we need SSVERSION to be added */

	/* Push L3 header */
	gh = (struct gsm48_hdr *) msgb_push(msg, sizeof(*gh));
	gh->proto_discr = GSM48_PDISC_NC_SS | (trans->transaction_id << 4);
	gh->msg_type = msg_type;

	if (msg_type == GSM0480_MTYPE_FACILITY) {
		/* directly transmit data on established connection */
		return gsm480_to_mm(msg, trans, GSM48_MMSS_DATA_REQ);
	} else {
		/* store header until our MM connection is established */
		trans->ss.msg = msg;

		/* request establishment */
		msg = gsm480_msgb_alloc();
		if (!msg) {
			trans_free(trans);
			return -ENOMEM;
		}
		return gsm480_to_mm(msg, trans, GSM48_MMSS_EST_REQ);
	}
}

static int gsm480_tx_cf(struct gsm_trans *trans, uint8_t msg_type,
	uint8_t op_code, uint8_t ss_code, const char *dest)
{
	struct msgb *msg;

	/* allocate message */
	msg = gsm480_msgb_alloc();
	if (!msg) {
		trans_free(trans);
		return -ENOMEM;
	}

	if (dest) {
		uint8_t tlv[32];
		int rc;

	        /* Forwarding To address */
		tlv[0] = 0x84;
		tlv[2] = 0x80; /* no extension */
		tlv[2] |= ((dest[0] == '+') ? 0x01 : 0x00) << 4; /* type */
		tlv[2] |= 0x1; /* plan*/
		rc = gsm48_encode_bcd_number(tlv + 1, sizeof(tlv) - 1, 1,
					dest + (dest[0] == '+'));
		if (rc < 0) {
			msgb_free(msg);
			trans_free(trans);
			return -EINVAL;
		}
		memcpy(msgb_put(msg, rc + 1), tlv, rc + 1);
	}

	/* Encode ss-Code */
	msgb_push_TLV1(msg, ASN1_OCTET_STRING_TAG, ss_code);

	/* Then wrap these as a Sequence */
	msgb_wrap_with_TL_asn(msg, GSM_0480_SEQUENCE_TAG);

	/* Pre-pend the operation code */
	msgb_push_TLV1(msg, GSM0480_OPERATION_CODE, op_code);

	return gsm480_tx_invoke(trans, msg, msg_type);
}

static int gsm480_tx_ussd(struct gsm_trans *trans, uint8_t msg_type,
	const char *text)
{
	struct msgb *msg;
	int length;

	/* allocate message */
	msg = gsm480_msgb_alloc();
	if (!msg) {
		trans_free(trans);
		return -ENOMEM;
	}

	/* Encode service request */
	length = gsm_7bit_encode(msg->data, text);
	msgb_put(msg, length);

	/* Then wrap it as an Octet String */
	msgb_wrap_with_TL_asn(msg, ASN1_OCTET_STRING_TAG);

	/* Pre-pend the DCS octet string */
	msgb_push_TLV1(msg, ASN1_OCTET_STRING_TAG, 0x0F);

	/* Then wrap these as a Sequence */
	msgb_wrap_with_TL_asn(msg, GSM_0480_SEQUENCE_TAG);

	if (msg_type == GSM0480_MTYPE_FACILITY) {
		/* Pre-pend the operation code */
		msgb_push_TLV1(msg, GSM0480_OPERATION_CODE,
				GSM0480_OP_CODE_USS_REQUEST);

		/* Then wrap these as a Sequence */
		msgb_wrap_with_TL_asn(msg, GSM_0480_SEQUENCE_TAG);
	} else {
		/* Pre-pend the operation code */
		msgb_push_TLV1(msg, GSM0480_OPERATION_CODE,
				GSM0480_OP_CODE_PROCESS_USS_REQ);
	}

	return gsm480_tx_invoke(trans, msg, msg_type);
}

/* create and send service code */
int ss_send(struct osmocom_ms *ms, const char *code, int new_trans)
{
	struct gsm_trans *trans = NULL, *transt;
	int transaction_id;

	/* look for an old transaction */
	if (!new_trans) {
		llist_for_each_entry(transt, &ms->trans_list, entry) {
			if (transt->protocol == GSM48_PDISC_NC_SS) {
				trans = transt;
				break;
			}
		}
	}

	/* if there is an old transaction, check if we can send data */
	if (trans) {
		if (trans->ss.state != GSM480_SS_ST_ACTIVE) {
			LOGP(DSS, LOGL_INFO, "Pending trans not active.\n");
			gsm480_ss_result(trans->ms, "Current service pending",
				0);
			return 0;
		}
		if (!strcmp(code, "hangup")) {
			gsm480_tx_release_compl(trans, 0);
			gsm480_trans_free(trans);
			return 0;
		}
		LOGP(DSS, LOGL_INFO, "Existing transaction.\n");
		return gsm480_tx_ussd(trans, GSM0480_MTYPE_FACILITY, code);
	}

	/* do nothing, if hangup is received */
	if (!strcmp(code, "hangup"))
		return 0;

	/* internal codes */
	if (!strcmp(code, "*#06#")) {
		return return_imei(ms);
	}

	/* no running, no transaction */
	if (!ms->started || ms->shutdown) {
		gsm480_ss_result(ms, "<phone is down>", 0);
		return -EIO;
	}

	/* allocate transaction with dummy reference */
	transaction_id = trans_assign_trans_id(ms, GSM48_PDISC_NC_SS,
		0);
	if (transaction_id < 0) {
		LOGP(DSS, LOGL_ERROR, "No transaction ID available\n");
		gsm480_ss_result(ms, NULL,
			GSM0480_ERR_CODE_RESOURCES_NOT_AVAILABLE);
		return -ENOMEM;
	}
	trans = trans_alloc(ms, GSM48_PDISC_NC_SS, transaction_id,
		new_callref++);
	if (!trans) {
		LOGP(DSS, LOGL_ERROR, "No memory for trans\n");
		gsm480_ss_result(ms, NULL,
			GSM0480_ERR_CODE_RESOURCES_NOT_AVAILABLE);
		return -ENOMEM;
	}

	/* go register sent state */
	trans->ss.state = GSM480_SS_ST_REGISTER;

	/* FIXME: generate invoke ID */
	trans->ss.invoke_id = 5;

	/* interrogate */
	if (code[0] == '*' && code[1] == '#' && code[strlen(code) - 1] == '#') {
		uint8_t ss_code = 0;

		ss_code_by_char(code + 2, &ss_code);
		if (ss_code)
			return gsm480_tx_cf(trans, GSM0480_MTYPE_REGISTER,
				GSM0480_OP_CODE_INTERROGATE_SS, ss_code, NULL);
	} else
	/* register / activate */
	if (code[0] == '*' && code[strlen(code) - 1] == '#') {
		uint8_t ss_code = 0;
		const char *to;
		char dest[32];

		/* double star */
		if (code[1] == '*')
			code++;

		to = ss_code_by_char(code + 1, &ss_code);
	
		/* register */
		if (ss_code && to && to[0] == '*') {
			strncpy(dest, to + 1, sizeof(dest) - 1);
			dest[sizeof(dest) - 1] = '\0';
			dest[strlen(dest) - 1] = '\0';
			return gsm480_tx_cf(trans, GSM0480_MTYPE_REGISTER,
				GSM0480_OP_CODE_REGISTER_SS, ss_code, dest);
		}
		/* activate */
		if (ss_code && to && to[0] == '#') {
			return gsm480_tx_cf(trans, GSM0480_MTYPE_REGISTER,
				GSM0480_OP_CODE_ACTIVATE_SS, ss_code, NULL);
		}
	} else
	/* erasure */
	if (code[0] == '#' && code[1] == '#' && code[strlen(code) - 1] == '#') {
		uint8_t ss_code = 0;

		ss_code_by_char(code + 2, &ss_code);
	
		if (ss_code)
			return gsm480_tx_cf(trans, GSM0480_MTYPE_REGISTER,
				GSM0480_OP_CODE_ERASE_SS, ss_code, NULL);
	} else
	/* deactivate */
	if (code[0] == '#' && code[strlen(code) - 1] == '#') {
		uint8_t ss_code = 0;

		ss_code_by_char(code + 1, &ss_code);
	
		if (ss_code)
			return gsm480_tx_cf(trans, GSM0480_MTYPE_REGISTER,
				GSM0480_OP_CODE_DEACTIVATE_SS, ss_code, NULL);
	}

	/* other codes */
	return gsm480_tx_ussd(trans, GSM0480_MTYPE_REGISTER, code);
}

/*
 * decoding
 */

static int parse_tag_asn1(const uint8_t *data, int len,
	const uint8_t **tag_data, int *tag_len)
{
	/* at least 2 bytes (tag + len) */
	if (len < 2)
		return -1;

	/* extended length */
	if (data[1] == 0x81) {
		/* at least 2 bytes (tag + 0x81 + len) */
		if (len < 3)
			return -1;
		*tag_len = data[2];
		*tag_data = data + 3;
		len -= 3;
	} else {
		*tag_len = data[1];
		*tag_data = data + 2;
		len -= 2;
	}

	/* check for buffer overflow */
	if (len < *tag_len)
		return -1;
	
	/* return length */
	return len;
}

static int gsm480_rx_ussd(struct gsm_trans *trans, const uint8_t *data,
	int len)
{
	int num_chars;
	char text[256];
	int i;
	const uint8_t *tag_data;
	int tag_len;

	/* sequence tag */
	if (parse_tag_asn1(data, len, &tag_data, &tag_len) < 0) {
		LOGP(DSS, LOGL_NOTICE, "2. Sequence tag too short\n");
		return -EINVAL;
	}
	if (data[0] != GSM_0480_SEQUENCE_TAG) {
		LOGP(DSS, LOGL_NOTICE, "Expecting 2. Sequence Tag\n");
		return -EINVAL;
	}
	len = tag_len;
	data = tag_data;

	/* DSC */
	if (parse_tag_asn1(data, len, &tag_data, &tag_len) < 1) {
		LOGP(DSS, LOGL_NOTICE, "DSC tag too short\n");
		return -EINVAL;
	}
	if (data[0] != ASN1_OCTET_STRING_TAG || tag_len != 1) {
		LOGP(DSS, LOGL_NOTICE, "Expecting DSC tag\n");
		return -EINVAL;
	}
	if (tag_data[0] != 0x0f) {
		LOGP(DSS, LOGL_NOTICE, "DSC not 0x0f\n");
		return -EINVAL;
	}
	len -= tag_data - data + tag_len;
	data = tag_data + tag_len;

	/* text */
	if (parse_tag_asn1(data, len, &tag_data, &tag_len) < 0) {
		LOGP(DSS, LOGL_NOTICE, "Text tag too short\n");
		return -EINVAL;
	}
	if (data[0] != ASN1_OCTET_STRING_TAG) {
		LOGP(DSS, LOGL_NOTICE, "Expecting text tag\n");
		return -EINVAL;
	}
	num_chars = tag_len * 8 / 7;
	/* Prevent a mobile-originated buffer-overrun! */
	if (num_chars > sizeof(text) - 1)
		num_chars = sizeof(text) - 1;
	text[sizeof(text) - 1] = '\0';
	gsm_7bit_decode(text, tag_data, num_chars);

	for (i = 0; text[i]; i++) {
		if (text[i] == '\r')
			text[i] = '\n';
	}
	/* remove last CR, if exists */
	if (text[0] && text[strlen(text) - 1] == '\n')
		text[strlen(text) - 1] = '\0';
	gsm480_ss_result(trans->ms, text, 0);

	return 0;
}

static int gsm480_rx_cf(struct gsm_trans *trans, const uint8_t *data,
	int len)
{
	struct osmocom_ms *ms = trans->ms;
	const uint8_t *tag_data, *data2;
	int tag_len, len2;
	char number[32];

	LOGP(DSS, LOGL_INFO, "call forwarding reply: len %d data %s\n", len,
		osmo_hexdump(data, len));

	vty_notify(ms, NULL);

	/* forwarding feature list */
	if (parse_tag_asn1(data, len, &tag_data, &tag_len) < 0) {
		LOGP(DSS, LOGL_NOTICE, "Tag too short\n");
		return -EINVAL;
	}
	if (data[0] == 0x80) {
		if ((tag_data[0] & 0x01))
			vty_notify(ms, "Status: activated\n");
		else
			vty_notify(ms, "Status: deactivated\n");
		return 0;
	}

	switch(data[0]) {
	case 0xa3:
		len = tag_len;
		data = tag_data;
		break;
	case 0xa0: /* forwarding info */
		len = tag_len;
		data = tag_data;
		if (parse_tag_asn1(data, len, &tag_data, &tag_len) < 1) {
			LOGP(DSS, LOGL_NOTICE, "Tag too short\n");
			return -EINVAL;
		}
		/* check for SS code */
		if (data[0] != 0x04)
			break;
		vty_notify(ms, "Reply for %s\n", decode_ss_code(tag_data[0]));
		len -= tag_data - data + tag_len;
		data = tag_data + tag_len;
		/* sequence tag */
		if (parse_tag_asn1(data, len, &tag_data, &tag_len) < 0) {
			LOGP(DSS, LOGL_NOTICE, "Tag too short\n");
			return -EINVAL;
		}
		if (data[0] != GSM_0480_SEQUENCE_TAG) {
			LOGP(DSS, LOGL_NOTICE, "Expecting sequence tag\n");
			return -EINVAL;
		}
		len = tag_len;
		data = tag_data;
		break;
	default:
		vty_notify(ms, "Call Forwarding reply unsupported.\n");
		return 0;
	}
	
	while (len) {
		/* sequence tag */
		if (parse_tag_asn1(data, len, &tag_data, &tag_len) < 0) {
			LOGP(DSS, LOGL_NOTICE, "Tag too short\n");
			return -EINVAL;
		}
		if (data[0] != GSM_0480_SEQUENCE_TAG) {
			len -= tag_data - data + tag_len;
			data = tag_data + tag_len;
			LOGP(DSS, LOGL_NOTICE, "Skipping tag 0x%x\n", data[0]);
			continue;
		}
		len -= tag_data - data + tag_len;
		data = tag_data + tag_len;
		len2 = tag_len;
		data2 = tag_data;

		while (len2) {
			/* tags in sequence */
			if (parse_tag_asn1(data2, len2, &tag_data, &tag_len)
				< 1) {
				LOGP(DSS, LOGL_NOTICE, "Tag too short\n");
				return -EINVAL;
			}
			LOGP(DSS, LOGL_INFO, "Tag: len %d data %s\n", tag_len,
				osmo_hexdump(tag_data, tag_len));
			switch (data2[0]) {
			case 0x82:
				vty_notify(ms, "Bearer Service: %s\n",
					get_value_string(Bearerservice_vals,
								tag_data[0]));
				break;
			case 0x83:
				vty_notify(ms, "Teleservice: %s\n",
					get_value_string(Teleservice_vals,
								tag_data[0]));
				break;
			case 0x84:
				if ((tag_data[0] & 0x01))
					vty_notify(ms, "Status: activated\n");
				else
					vty_notify(ms, "Status: deactivated\n");
				break;
			case 0x85:
				if (((tag_data[0] & 0x70) >> 4) == 1)
					strcpy(number, "+");
				else if (((tag_data[0] & 0x70) >> 4) == 1)
					strcpy(number, "+");
				else
					number[0] = '\0';
				gsm48_decode_bcd_number(number + strlen(number),
					sizeof(number) - strlen(number),
					tag_data - 1, 1);
				vty_notify(ms, "Destination: %s\n", number);
				break;
			}
			len2 -= tag_data - data2 + tag_len;
			data2 = tag_data + tag_len;
		}
	}

	return 0;
}

static int gsm480_rx_result(struct gsm_trans *trans, const uint8_t *data,
	int len, int msg_type)
{
	const uint8_t *tag_data;
	int tag_len;
	int rc = 0;

	LOGP(DSS, LOGL_INFO, "Result received (len %d)\n", len);

	if (len && data[0] == 0x8d) {
		LOGP(DSS, LOGL_NOTICE, "Skipping mysterious 0x8d\n");
		len--;
		data++;
	}

	/* invoke ID */
	if (parse_tag_asn1(data, len, &tag_data, &tag_len) < 1) {
		LOGP(DSS, LOGL_NOTICE, "Invoke ID too short\n");
		return -EINVAL;
	}
	if (data[0] != GSM0480_COMPIDTAG_INVOKE_ID || tag_len != 1) {
		LOGP(DSS, LOGL_NOTICE, "Expecting invoke ID\n");
		return -EINVAL;
	}

	if (msg_type == GSM0480_CTYPE_RETURN_RESULT) {
		if (trans->ss.invoke_id != data[2]) {
			LOGP(DSS, LOGL_NOTICE, "Invoke ID mismatch\n");
		}
	}
	/* Store invoke ID, in case we wan't to send a result. */
	trans->ss.invoke_id = tag_data[0];
	len -= tag_data - data + tag_len;
	data = tag_data + tag_len;

	if (!len) {
		gsm480_ss_result(trans->ms, "<no result>", 0);
		return 0;
	}

	/* sequence tag */
	if (parse_tag_asn1(data, len, &tag_data, &tag_len) < 0) {
		LOGP(DSS, LOGL_NOTICE, "Sequence tag too short\n");
		return -EINVAL;
	}
	if (data[0] != GSM_0480_SEQUENCE_TAG) {
		LOGP(DSS, LOGL_NOTICE, "Expecting Sequence Tag, trying "
			"Operation Tag\n");
		goto operation;
	}
	len = tag_len;
	data = tag_data;

	/* operation */
operation:
	if (parse_tag_asn1(data, len, &tag_data, &tag_len) < 1) {
		LOGP(DSS, LOGL_NOTICE, "Operation too short\n");
		return -EINVAL;
	}
	if (data[0] != GSM0480_OPERATION_CODE || tag_len != 1) {
		LOGP(DSS, LOGL_NOTICE, "Expecting Operation Code\n");
		return -EINVAL;
	}
	len -= tag_data - data + tag_len;
	data = tag_data + tag_len;

	switch (tag_data[0]) {
	case GSM0480_OP_CODE_PROCESS_USS_REQ:
	case GSM0480_OP_CODE_USS_REQUEST:
		rc = gsm480_rx_ussd(trans, data, len);
		break;
	case GSM0480_OP_CODE_INTERROGATE_SS:
	case GSM0480_OP_CODE_REGISTER_SS:
	case GSM0480_OP_CODE_ACTIVATE_SS:
	case GSM0480_OP_CODE_DEACTIVATE_SS:
	case GSM0480_OP_CODE_ERASE_SS:
		rc = gsm480_rx_cf(trans, data, len);
		break;
	default:
		LOGP(DSS, LOGL_NOTICE, "Operation code not USS\n");
		rc = -EINVAL;
	}

	return rc;
}

/* facility from BSC */
static int gsm480_rx_fac_ie(struct gsm_trans *trans, const uint8_t *data,
	int len)
{
	int rc = 0;
	const uint8_t *tag_data;
	int tag_len;

	LOGP(DSS, LOGL_INFO, "Facility received (len %d)\n", len);

	if (parse_tag_asn1(data, len, &tag_data, &tag_len) < 0) {
		LOGP(DSS, LOGL_NOTICE, "Facility too short\n");
		return -EINVAL;
	}

	switch (data[0]) {
	case GSM0480_CTYPE_INVOKE:
	case GSM0480_CTYPE_RETURN_RESULT:
		rc = gsm480_rx_result(trans, tag_data, tag_len, data[0]);
		break;
	case GSM0480_CTYPE_RETURN_ERROR:
		// FIXME: return error code
		gsm480_ss_result(trans->ms, "<error received>", 0);
		break;
	case GSM0480_CTYPE_REJECT:
		gsm480_ss_result(trans->ms, "<service rejected>", 0);
		break;
	default:
		LOGP(DSS, LOGL_NOTICE, "CTYPE unknown\n");
		rc = -EINVAL;
	}

	return rc;
}

static int gsm480_rx_cause_ie(struct gsm_trans *trans, const uint8_t *data,
	int len)
{
	uint8_t value;

	LOGP(DSS, LOGL_INFO, "Cause received (len %d)\n", len);

	if (len < 2) {
		LOGP(DSS, LOGL_NOTICE, "Cause too short\n");
		return -EINVAL;
	}
	if (!(data[1] & 0x80)) {
		if (len < 3) {
			LOGP(DSS, LOGL_NOTICE, "Cause too short\n");
			return -EINVAL;
		}
		value = data[3] & 0x7f;
	} else
		value = data[2] & 0x7f;

	LOGP(DSS, LOGL_INFO, "Received Cause %d\n", value);

	/* this is an error */
	return -EINVAL;
}

/* release complete from BSC */
static int gsm480_rx_release_comp(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	int rc = 0;

	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len, 0, 0);
	if (TLVP_PRESENT(&tp, GSM48_IE_FACILITY)) {
		rc = gsm480_rx_fac_ie(trans, TLVP_VAL(&tp, GSM48_IE_FACILITY),
			*(TLVP_VAL(&tp, GSM48_IE_FACILITY)-1));
	} else {
		/* facility optional */
		LOGP(DSS, LOGL_INFO, "No facility IE received\n");
		if (TLVP_PRESENT(&tp, GSM48_IE_CAUSE)) {
			rc = gsm480_rx_cause_ie(trans,
				TLVP_VAL(&tp, GSM48_IE_CAUSE),
				*(TLVP_VAL(&tp, GSM48_IE_CAUSE)-1));
		}
	}

	if (rc < 0)
		gsm480_ss_result(trans->ms, NULL, 0);
	if (rc > 0)
		gsm480_ss_result(trans->ms, NULL, rc);

	/* remote releases */
	gsm480_trans_free(trans);

	return rc;
}

/* facility from BSC */
static int gsm480_rx_facility(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	int rc = 0;

	/* go register state */
	trans->ss.state = GSM480_SS_ST_ACTIVE;

	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len,
		GSM48_IE_FACILITY, 0);
	if (TLVP_PRESENT(&tp, GSM48_IE_FACILITY)) {
		rc = gsm480_rx_fac_ie(trans, TLVP_VAL(&tp, GSM48_IE_FACILITY),
			*(TLVP_VAL(&tp, GSM48_IE_FACILITY)-1));
	} else {
		LOGP(DSS, LOGL_INFO, "No facility IE received\n");
		/* release 3.7.5 */
		gsm480_tx_release_compl(trans, 96);
		/* local releases */
		gsm480_trans_free(trans);
	}

	if (rc < 0)
		gsm480_ss_result(trans->ms, NULL, 0);
	if (rc > 0)
		gsm480_ss_result(trans->ms, NULL, rc);

	return rc;
}

/* regisster from BSC */
static int gsm480_rx_register(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	int rc = 0;

	/* go register state */
	trans->ss.state = GSM480_SS_ST_ACTIVE;

	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len, 0, 0);
	if (TLVP_PRESENT(&tp, GSM48_IE_FACILITY)) {
		rc = gsm480_rx_fac_ie(trans, TLVP_VAL(&tp, GSM48_IE_FACILITY),
			*(TLVP_VAL(&tp, GSM48_IE_FACILITY)-1));
	} else {
		/* facility optional */
		LOGP(DSS, LOGL_INFO, "No facility IE received\n");
		/* release 3.7.5 */
		gsm480_tx_release_compl(trans, 96);
		/* local releases */
		gsm480_trans_free(trans);
	}

	if (rc < 0)
		gsm480_ss_result(trans->ms, NULL, 0);
	if (rc > 0)
		gsm480_ss_result(trans->ms, NULL, rc);

	return rc;
}

/*
 * message handling
 */

/* push MMSS header and send to MM */
static int gsm480_to_mm(struct msgb *msg, struct gsm_trans *trans,
	int msg_type)
{
	struct gsm48_mmxx_hdr *mmh;

	/* set l3H */
	msg->l3h = msg->data;

	/* push RR header */
	msgb_push(msg, sizeof(struct gsm48_mmxx_hdr));
	mmh = (struct gsm48_mmxx_hdr *)msg->data;
	mmh->msg_type = msg_type;
	mmh->ref = trans->callref;
	mmh->transaction_id = trans->transaction_id;
	mmh->sapi = 0;
	mmh->emergency = 0;

	/* send message to MM */
	LOGP(DSS, LOGL_INFO, "Sending '%s' to MM (callref=%x, "
		"transaction_id=%d)\n", get_mmxx_name(msg_type), trans->callref,
		trans->transaction_id);
	return gsm48_mmxx_downmsg(trans->ms, msg);
}

/* receive est confirm from MM layer */
static int gsm480_mmss_est(int mmss_msg, struct gsm_trans *trans,
	struct msgb *msg)
{
	struct osmocom_ms *ms = trans->ms;
	struct msgb *temp;

	LOGP(DSS, LOGL_INFO, "(ms %s) Received confirm, sending pending SS\n",
		ms->name);

	/* remove transaction, if no SS message */
	if (!trans->ss.msg) {
		LOGP(DSS, LOGL_ERROR, "(ms %s) No pending SS!\n", ms->name);
		gsm480_trans_free(trans);
		return -EINVAL;
	}

	/* detach message and then send */
	temp = trans->ss.msg;
	trans->ss.msg = NULL;
	return gsm480_to_mm(temp, trans, GSM48_MMSS_DATA_REQ);
}

/* receive data indication from MM layer */
static int gsm480_mmss_ind(int mmss_msg, struct gsm_trans *trans,
	struct msgb *msg)
{
	struct osmocom_ms *ms = trans->ms;
	struct gsm48_hdr *gh = msgb_l3(msg);
	int msg_type = gh->msg_type & 0xbf;
	int rc = 0;

	/* pull the MMSS header */
	msgb_pull(msg, sizeof(struct gsm48_mmxx_hdr));

	LOGP(DSS, LOGL_INFO, "(ms %s) Received est/data '%u'\n", ms->name,
		msg_type);

	switch (msg_type) {
	case GSM0480_MTYPE_RELEASE_COMPLETE:
		rc = gsm480_rx_release_comp(trans, msg);
		break;
	case GSM0480_MTYPE_FACILITY:
		rc = gsm480_rx_facility(trans, msg);
		break;
	case GSM0480_MTYPE_REGISTER:
		rc = gsm480_rx_register(trans, msg);
		break;
	default:
		LOGP(DSS, LOGL_NOTICE, "Message unhandled.\n");
		/* release 3.7.4 */
		gsm480_tx_release_compl(trans, 97);
		gsm480_trans_free(trans);
		rc = -ENOTSUP;
	}
	return 0;
}

/* receive message from MM layer */
int gsm480_rcv_ss(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	int msg_type = mmh->msg_type;
	struct gsm_trans *trans;
	int rc = 0;

	trans = trans_find_by_callref(ms, mmh->ref);
	if (!trans) {
		LOGP(DSS, LOGL_INFO, " -> (new transaction)\n");
		trans = trans_alloc(ms, GSM48_PDISC_NC_SS, mmh->transaction_id,
					mmh->ref);
		if (!trans)
			return -ENOMEM;
	}

	LOGP(DSS, LOGL_INFO, "(ms %s) Received '%s' from MM\n", ms->name,
		get_mmxx_name(msg_type));

	switch (msg_type) {
	case GSM48_MMSS_EST_CNF:
		rc = gsm480_mmss_est(msg_type, trans, msg);
		break;
	case GSM48_MMSS_EST_IND:
	case GSM48_MMSS_DATA_IND:
		rc = gsm480_mmss_ind(msg_type, trans, msg);
		break;
	case GSM48_MMSS_REL_IND:
	case GSM48_MMSS_ERR_IND:
		LOGP(DSS, LOGL_INFO, "MM connection released.\n");
		trans_free(trans);
		break;
	default:
		LOGP(DSS, LOGL_NOTICE, "Message unhandled.\n");
		rc = -ENOTSUP;
	}

	return rc;
}

