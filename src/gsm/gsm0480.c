/* Format functions for GSM 04.80 */

/*
 * (C) 2010 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2009 by Mike Haben <michael.haben@btinternet.com>
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

#include <osmocom/gsm/gsm0480.h>
#include <osmocom/gsm/gsm_utils.h>

#include <osmocom/core/logging.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_04_80.h>

#include <string.h>

static inline unsigned char *msgb_wrap_with_TL(struct msgb *msgb, uint8_t tag)
{
	uint8_t *data = msgb_push(msgb, 2);

	data[0] = tag;
	data[1] = msgb->len - 2;
	return data;
}

static inline unsigned char *msgb_push_TLV1(struct msgb *msgb, uint8_t tag,
					    uint8_t value)
{
	uint8_t *data = msgb_push(msgb, 3);

	data[0] = tag;
	data[1] = 1;
	data[2] = value;
	return data;
}

/* wrap an invoke around it... the other way around
 *
 * 1.) Invoke Component tag
 * 2.) Invoke ID Tag
 * 3.) Operation
 * 4.) Data
 */
int gsm0480_wrap_invoke(struct msgb *msg, int op, int link_id)
{
	/* 3. operation */
	msgb_push_TLV1(msg, GSM0480_OPERATION_CODE, op);

	/* 2. invoke id tag */
	msgb_push_TLV1(msg, GSM0480_COMPIDTAG_INVOKE_ID, link_id);

	/* 1. component tag */
	msgb_wrap_with_TL(msg, GSM0480_CTYPE_INVOKE);

	return 0;
}

/* wrap the GSM 04.08 Facility IE around it */
int gsm0480_wrap_facility(struct msgb *msg)
{
	msgb_wrap_with_TL(msg, GSM0480_IE_FACILITY);

	return 0;
}

struct msgb *gsm0480_create_unstructuredSS_Notify(int alertPattern, const char *text)
{
	struct msgb *msg;
	uint8_t *seq_len_ptr, *ussd_len_ptr, *data;
	int len;

	msg = msgb_alloc_headroom(1024, 128, "GSM 04.80");
	if (!msg)
		return NULL;

	/* SEQUENCE { */
	msgb_put_u8(msg, GSM_0480_SEQUENCE_TAG);
	seq_len_ptr = msgb_put(msg, 1);

	/* DCS { */
	msgb_put_u8(msg, ASN1_OCTET_STRING_TAG);
	msgb_put_u8(msg, 1);
	msgb_put_u8(msg, 0x0F);
	/* } DCS */

	/* USSD-String { */
	msgb_put_u8(msg, ASN1_OCTET_STRING_TAG);
	ussd_len_ptr = msgb_put(msg, 1);
	data = msgb_put(msg, 0);
	gsm_7bit_encode_n_ussd(data, msgb_tailroom(msg), text, &len);
	msgb_put(msg, len);
	ussd_len_ptr[0] = len;
	/* USSD-String } */

	/* alertingPattern { */
	msgb_put_u8(msg, ASN1_OCTET_STRING_TAG);
	msgb_put_u8(msg, 1);
	msgb_put_u8(msg, alertPattern);
	/* } alertingPattern */

	seq_len_ptr[0] = 3 + 2 + ussd_len_ptr[0] + 3;
	/* } SEQUENCE */

	return msg;
}

struct msgb *gsm0480_create_notifySS(const char *text)
{
	struct msgb *msg;
	uint8_t *data, *tmp_len;
	uint8_t *seq_len_ptr, *cal_len_ptr, *opt_len_ptr, *nam_len_ptr;
	int len;

	len = strlen(text);
	if (len < 1 || len > 160)
		return NULL;

	msg = msgb_alloc_headroom(1024, 128, "GSM 04.80");
	if (!msg)
		return NULL;

	msgb_put_u8(msg, GSM_0480_SEQUENCE_TAG);
	seq_len_ptr = msgb_put(msg, 1);

	/* ss_code for CNAP { */
	msgb_put_u8(msg, 0x81);
	msgb_put_u8(msg, 1);
	msgb_put_u8(msg, 0x19);
	/* } ss_code */


	/* nameIndicator { */
	msgb_put_u8(msg, 0xB4);
	nam_len_ptr = msgb_put(msg, 1);

	/* callingName { */
	msgb_put_u8(msg, 0xA0);
	opt_len_ptr = msgb_put(msg, 1);
	msgb_put_u8(msg, 0xA0);
	cal_len_ptr = msgb_put(msg, 1);

	/* namePresentationAllowed { */
	/* add the DCS value */
	msgb_put_u8(msg, 0x80);
	msgb_put_u8(msg, 1);
	msgb_put_u8(msg, 0x0F);

	/* add the lengthInCharacters */
	msgb_put_u8(msg, 0x81);
	msgb_put_u8(msg, 1);
	msgb_put_u8(msg, strlen(text));

	/* add the actual string */
	msgb_put_u8(msg, 0x82);
	tmp_len = msgb_put(msg, 1);
	data = msgb_put(msg, 0);
	gsm_7bit_encode_n_ussd(data, msgb_tailroom(msg), text, &len);
	tmp_len[0] = len;
	msgb_put(msg, len);

	/* }; namePresentationAllowed */

	cal_len_ptr[0] = 3 + 3 + 2 + len;
	opt_len_ptr[0] = cal_len_ptr[0] + 2;
	/* }; callingName */

	nam_len_ptr[0] = opt_len_ptr[0] + 2;
	/* ); nameIndicator */

	/* write the lengths... */
	seq_len_ptr[0] = 3 + nam_len_ptr[0] + 2;

	return msg;
}

/* Forward declarations */
static int parse_ss(const struct gsm48_hdr *hdr,
		    uint16_t len, struct ss_request *req);
static int parse_ss_info_elements(const uint8_t *ussd_ie, uint16_t len,
				  struct ss_request *req);
static int parse_facility_ie(const uint8_t *facility_ie, uint16_t length,
			     struct ss_request *req);
static int parse_ss_invoke(const uint8_t *invoke_data, uint16_t length,
					struct ss_request *req);
static int parse_process_uss_req(const uint8_t *uss_req_data, uint16_t length,
					struct ss_request *req);
static int parse_ss_for_bs_req(const uint8_t *ss_req_data,
				     uint16_t length,
				     struct ss_request *req);

/* Decode a mobile-originated USSD-request message */
int gsm0480_decode_ussd_request(const struct gsm48_hdr *hdr, uint16_t len,
				struct ussd_request *req)
{
	struct ss_request ss;
	int rc = 0;

	memset(&ss, 0, sizeof(ss));

	if (len < sizeof(*hdr) + 2) {
		LOGP(0, LOGL_DEBUG, "USSD Request is too short.\n");
		return 0;
	}

	if (gsm48_hdr_pdisc(hdr) == GSM48_PDISC_NC_SS) {
		req->transaction_id = hdr->proto_discr & 0x70;

		ss.transaction_id = req->transaction_id;
		rc = parse_ss(hdr, len, &ss);

		/* convert from ss_request to legacy ussd_request */
		req->transaction_id = ss.transaction_id;
		req->invoke_id = ss.invoke_id;
		if (ss.ussd_text[0] == 0xFF)
			req->text[0] = '\0';
		else {
			memcpy(req->text, ss.ussd_text, sizeof(req->text));
			req->text[sizeof(req->text)-1] = '\0';
		}
	}

	if (!rc)
		LOGP(0, LOGL_DEBUG, "Error occurred while parsing received USSD!\n");

	return rc;
}

/* Decode a mobile-originated SS request message */
int gsm0480_decode_ss_request(const struct gsm48_hdr *hdr, uint16_t len,
				struct ss_request *req)
{
	int rc = 0;

	if (len < sizeof(*hdr) + 2) {
		LOGP(0, LOGL_DEBUG, "SS Request is too short.\n");
		return 0;
	}

	if (gsm48_hdr_pdisc(hdr) == GSM48_PDISC_NC_SS) {
		req->transaction_id = hdr->proto_discr & 0x70;
		rc = parse_ss(hdr, len, req);
	}

	if (!rc)
		LOGP(0, LOGL_DEBUG, "Error occurred while parsing received SS!\n");

	return rc;
}

static int parse_ss(const struct gsm48_hdr *hdr, uint16_t len, struct ss_request *req)
{
	int rc = 1;
	uint8_t msg_type = hdr->msg_type & 0xBF;  /* message-type - section 3.4 */

	switch (msg_type) {
	case GSM0480_MTYPE_RELEASE_COMPLETE:
		LOGP(0, LOGL_DEBUG, "SS Release Complete\n");
		/* could also parse out the optional Cause/Facility data */
		req->ussd_text[0] = 0xFF;
		break;
	case GSM0480_MTYPE_REGISTER:
	case GSM0480_MTYPE_FACILITY:
		rc &= parse_ss_info_elements(&hdr->data[0], len - sizeof(*hdr), req);
		break;
	default:
		LOGP(0, LOGL_DEBUG, "Unknown GSM 04.80 message-type field 0x%02x\n",
			hdr->msg_type);
		rc = 0;
		break;
	}

	return rc;
}

static int parse_ss_info_elements(const uint8_t *ss_ie, uint16_t len,
				  struct ss_request *req)
{
	int rc = -1;
	/* Information Element Identifier - table 3.2 & GSM 04.08 section 10.5 */
	uint8_t iei;
	uint8_t iei_length;

	iei = ss_ie[0];
	iei_length = ss_ie[1];

	/* If the data does not fit, report an error */
	if (len - 2 < iei_length)
		return 0;

	switch (iei) {
	case GSM48_IE_CAUSE:
		break;
	case GSM0480_IE_FACILITY:
		rc = parse_facility_ie(ss_ie + 2, iei_length, req);
		break;
	case GSM0480_IE_SS_VERSION:
		break;
	default:
		LOGP(0, LOGL_DEBUG, "Unhandled GSM 04.08 or 04.80 IEI 0x%02x\n",
			iei);
		rc = 0;
		break;
	}

	return rc;
}

static int parse_facility_ie(const uint8_t *facility_ie, uint16_t length,
			     struct ss_request *req)
{
	int rc = 1;
	uint8_t offset = 0;

	while (offset + 2 <= length) {
		/* Component Type tag - table 3.7 */
		uint8_t component_type = facility_ie[offset];
		uint8_t component_length = facility_ie[offset+1];

		/* size check */
		if (offset + 2 + component_length > length) {
			LOGP(0, LOGL_ERROR, "Component does not fit.\n");
			return 0;
		}

		switch (component_type) {
		case GSM0480_CTYPE_INVOKE:
			rc &= parse_ss_invoke(facility_ie+2,
					      component_length,
					      req);
			break;
		case GSM0480_CTYPE_RETURN_RESULT:
			break;
		case GSM0480_CTYPE_RETURN_ERROR:
			break;
		case GSM0480_CTYPE_REJECT:
			break;
		default:
			LOGP(0, LOGL_DEBUG, "Unknown GSM 04.80 Facility "
				"Component Type 0x%02x\n", component_type);
			rc = 0;
			break;
		}
		offset += (component_length+2);
	};

	return rc;
}

/* Parse an Invoke component - see table 3.3 */
static int parse_ss_invoke(const uint8_t *invoke_data, uint16_t length,
			   struct ss_request *req)
{
	int rc = 1;
	uint8_t offset;

	if (length < 3)
		return 0;

	/* mandatory part */
	if (invoke_data[0] != GSM0480_COMPIDTAG_INVOKE_ID) {
		LOGP(0, LOGL_DEBUG, "Unexpected GSM 04.80 Component-ID tag "
		     "0x%02x (expecting Invoke ID tag)\n", invoke_data[0]);
	}

	offset = invoke_data[1] + 2;
	req->invoke_id = invoke_data[2];

	/* look ahead once */
	if (offset + 1 > length)
		return 0;

	/* optional part */
	if (invoke_data[offset] == GSM0480_COMPIDTAG_LINKED_ID)
		offset += invoke_data[offset+1] + 2;  /* skip over it */

	/* mandatory part */
	if (invoke_data[offset] == GSM0480_OPERATION_CODE) {
		if (offset + 2 > length)
			return 0;
		uint8_t operation_code = invoke_data[offset+2];
		req->opcode = operation_code;
		switch (operation_code) {
		case GSM0480_OP_CODE_PROCESS_USS_REQ:
			rc = parse_process_uss_req(invoke_data + offset + 3,
						   length - offset - 3,
						   req);
			break;
		case GSM0480_OP_CODE_ACTIVATE_SS:
		case GSM0480_OP_CODE_DEACTIVATE_SS:
		case GSM0480_OP_CODE_INTERROGATE_SS:
			rc = parse_ss_for_bs_req(invoke_data + offset + 3,
						 length - offset - 3,
						 req);
			break;
		default:
			LOGP(0, LOGL_DEBUG, "GSM 04.80 operation code 0x%02x "
				"is not yet handled\n", operation_code);
			rc = 0;
			break;
		}
	} else {
		LOGP(0, LOGL_DEBUG, "Unexpected GSM 04.80 Component-ID tag 0x%02x "
			"(expecting Operation Code tag)\n",
			invoke_data[0]);
		rc = 0;
	}

	return rc;
}

/* Parse the parameters of a Process UnstructuredSS Request */
static int parse_process_uss_req(const uint8_t *uss_req_data, uint16_t length,
				 struct ss_request *req)
{
	int rc = 0;
	int num_chars;
	uint8_t dcs;


	/* we need at least that much */
	if (length < 8)
		return 0;


	if (uss_req_data[0] == GSM_0480_SEQUENCE_TAG) {
		if (uss_req_data[2] == ASN1_OCTET_STRING_TAG) {
			dcs = uss_req_data[4];
			if ((dcs == 0x0F) &&
			    (uss_req_data[5] == ASN1_OCTET_STRING_TAG)) {
				num_chars = (uss_req_data[6] * 8) / 7;
				/* Prevent a mobile-originated buffer-overrun! */
				if (num_chars > MAX_LEN_USSD_STRING)
					num_chars = MAX_LEN_USSD_STRING;
				gsm_7bit_decode_n_ussd((char *)req->ussd_text,
							sizeof(req->ussd_text),
							&(uss_req_data[7]), num_chars);
				rc = 1;
			}
		}
	}
	return rc;
}

/* Parse the parameters of a Interrogate/Activate/DeactivateSS Request */
static int parse_ss_for_bs_req(const uint8_t *ss_req_data,
			       uint16_t length,
			       struct ss_request *req)
{
	int rc = 0;


	/* we need at least that much */
	if (length < 5)
		return 0;


	if (ss_req_data[0] == GSM_0480_SEQUENCE_TAG) {
		if ((ss_req_data[2] == ASN1_OCTET_STRING_TAG) &&
			ss_req_data[3] == 1) {
			req->ss_code = ss_req_data[4];

			rc = 1;
		}
	}
	return rc;
}

struct msgb *gsm0480_create_ussd_resp(uint8_t invoke_id, uint8_t trans_id, const char *text)
{
	struct msgb *msg;
	struct gsm48_hdr *gh;
	uint8_t *ptr8;
	int response_len;

	msg = msgb_alloc_headroom(1024, 128, "GSM 04.80");
	if (!msg)
		return NULL;

	/* First put the payload text into the message */
	ptr8 = msgb_put(msg, 0);
	gsm_7bit_encode_n_ussd(ptr8, msgb_tailroom(msg), text, &response_len);
	msgb_put(msg, response_len);

	/* Then wrap it as an Octet String */
	msgb_wrap_with_TL(msg, ASN1_OCTET_STRING_TAG);

	/* Pre-pend the DCS octet string */
	msgb_push_TLV1(msg, ASN1_OCTET_STRING_TAG, 0x0F);

	/* Then wrap these as a Sequence */
	msgb_wrap_with_TL(msg, GSM_0480_SEQUENCE_TAG);

	/* Pre-pend the operation code */
	msgb_push_TLV1(msg, GSM0480_OPERATION_CODE,
			GSM0480_OP_CODE_PROCESS_USS_REQ);

	/* Wrap the operation code and IA5 string as a sequence */
	msgb_wrap_with_TL(msg, GSM_0480_SEQUENCE_TAG);

	/* Pre-pend the invoke ID */
	msgb_push_TLV1(msg, GSM0480_COMPIDTAG_INVOKE_ID, invoke_id);

	/* Wrap this up as a Return Result component */
	msgb_wrap_with_TL(msg, GSM0480_CTYPE_RETURN_RESULT);

	/* Wrap the component in a Facility message */
	msgb_wrap_with_TL(msg, GSM0480_IE_FACILITY);

	/* And finally pre-pend the L3 header */
	gh = (struct gsm48_hdr *) msgb_push(msg, sizeof(*gh));
	gh->proto_discr = GSM48_PDISC_NC_SS | trans_id
					| (1<<7);  /* TI direction = 1 */
	gh->msg_type = GSM0480_MTYPE_RELEASE_COMPLETE;

	return msg;
}
