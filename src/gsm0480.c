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

#include <osmocore/gsm0480.h>
#include <osmocore/gsm_utils.h>

#include <osmocore/protocol/gsm_04_80.h>

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
	len = gsm_7bit_encode(data, text);
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
	len = gsm_7bit_encode(data, text);
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

