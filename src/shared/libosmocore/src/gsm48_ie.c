/* GSM Mobile Radio Interface Layer 3 messages
 * 3GPP TS 04.08 version 7.21.0 Release 1998 / ETSI TS 100 940 V7.21.0 */

/* (C) 2008 by Harald Welte <laforge@gnumonks.org>
 * (C) 2009-2010 by Andreas Eversberg
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
#include <string.h>
#include <errno.h>

#include <osmocore/utils.h>
#include <osmocore/msgb.h>
#include <osmocore/tlv.h>
#include <osmocore/mncc.h>
#include <osmocore/protocol/gsm_04_08.h>

static const char bcd_num_digits[] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', '*', '#', 'a', 'b', 'c', '\0'
};

/* decode a 'called/calling/connect party BCD number' as in 10.5.4.7 */
int gsm48_decode_bcd_number(char *output, int output_len,
			    const uint8_t *bcd_lv, int h_len)
{
	uint8_t in_len = bcd_lv[0];
	int i;

	for (i = 1 + h_len; i <= in_len; i++) {
		/* lower nibble */
		output_len--;
		if (output_len <= 1)
			break;
		*output++ = bcd_num_digits[bcd_lv[i] & 0xf];

		/* higher nibble */
		output_len--;
		if (output_len <= 1)
			break;
		*output++ = bcd_num_digits[bcd_lv[i] >> 4];
	}
	if (output_len >= 1)
		*output++ = '\0';

	return 0;
}

/* convert a single ASCII character to call-control BCD */
static int asc_to_bcd(const char asc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bcd_num_digits); i++) {
		if (bcd_num_digits[i] == asc)
			return i;
	}
	return -EINVAL;
}

/* convert a ASCII phone number to 'called/calling/connect party BCD number' */
int gsm48_encode_bcd_number(uint8_t *bcd_lv, uint8_t max_len,
		      int h_len, const char *input)
{
	int in_len = strlen(input);
	int i;
	uint8_t *bcd_cur = bcd_lv + 1 + h_len;

	/* two digits per byte, plus type byte */
	bcd_lv[0] = in_len/2 + h_len;
	if (in_len % 2)
		bcd_lv[0]++;

	if (bcd_lv[0] > max_len)
		return -EIO;

	for (i = 0; i < in_len; i++) {
		int rc = asc_to_bcd(input[i]);
		if (rc < 0)
			return rc;
		if (i % 2 == 0)
			*bcd_cur = rc;
		else
			*bcd_cur++ |= (rc << 4);
	}
	/* append padding nibble in case of odd length */
	if (i % 2)
		*bcd_cur++ |= 0xf0;

	/* return how many bytes we used */
	return (bcd_cur - bcd_lv);
}

/* decode 'bearer capability' */
int gsm48_decode_bearer_cap(struct gsm_mncc_bearer_cap *bcap,
			     const uint8_t *lv)
{
	uint8_t in_len = lv[0];
	int i, s;

	if (in_len < 1)
		return -EINVAL;

	bcap->speech_ver[0] = -1; /* end of list, of maximum 7 values */

	/* octet 3 */
	bcap->transfer = lv[1] & 0x07;
	bcap->mode = (lv[1] & 0x08) >> 3;
	bcap->coding = (lv[1] & 0x10) >> 4;
	bcap->radio = (lv[1] & 0x60) >> 5;

	if (bcap->transfer == GSM_MNCC_BCAP_SPEECH) {
		i = 1;
		s = 0;
		while(!(lv[i] & 0x80)) {
			i++; /* octet 3a etc */
			if (in_len < i)
				return 0;
			bcap->speech_ver[s++] = lv[i] & 0x0f;
			bcap->speech_ver[s] = -1; /* end of list */
			if (i == 2) /* octet 3a */
				bcap->speech_ctm = (lv[i] & 0x20) >> 5;
			if (s == 7) /* maximum speech versions + end of list */
				return 0;
		}
	} else {
		i = 1;
		while (!(lv[i] & 0x80)) {
			i++; /* octet 3a etc */
			if (in_len < i)
				return 0;
			/* ignore them */
		}
		/* FIXME: implement OCTET 4+ parsing */
	}

	return 0;
}

/* encode 'bearer capability' */
int gsm48_encode_bearer_cap(struct msgb *msg, int lv_only,
			     const struct gsm_mncc_bearer_cap *bcap)
{
	uint8_t lv[32 + 1];
	int i = 1, s;

	lv[1] = bcap->transfer;
	lv[1] |= bcap->mode << 3;
	lv[1] |= bcap->coding << 4;
	lv[1] |= bcap->radio << 5;

	if (bcap->transfer == GSM_MNCC_BCAP_SPEECH) {
		for (s = 0; bcap->speech_ver[s] >= 0; s++) {
			i++; /* octet 3a etc */
			lv[i] = bcap->speech_ver[s];
			if (i == 2) /* octet 3a */
				lv[i] |= bcap->speech_ctm << 5;
		}
		lv[i] |= 0x80; /* last IE of octet 3 etc */
	} else {
		/* FIXME: implement OCTET 4+ encoding */
	}

	lv[0] = i;
	if (lv_only)
		msgb_lv_put(msg, lv[0], lv+1);
	else
		msgb_tlv_put(msg, GSM48_IE_BEARER_CAP, lv[0], lv+1);

	return 0;
}

/* decode 'call control cap' */
int gsm48_decode_cccap(struct gsm_mncc_cccap *ccap, const uint8_t *lv)
{
	uint8_t in_len = lv[0];

	if (in_len < 1)
		return -EINVAL;

	/* octet 3 */
	ccap->dtmf = lv[1] & 0x01;
	ccap->pcp = (lv[1] & 0x02) >> 1;

	return 0;
}

/* encode 'call control cap' */
int gsm48_encode_cccap(struct msgb *msg,
			const struct gsm_mncc_cccap *ccap)
{
	uint8_t lv[2];

	lv[0] = 1;
	lv[1] = 0;
	if (ccap->dtmf)
		lv [1] |= 0x01;
	if (ccap->pcp)
		lv [1] |= 0x02;

	msgb_tlv_put(msg, GSM48_IE_CC_CAP, lv[0], lv+1);

	return 0;
}

/* decode 'called party BCD number' */
int gsm48_decode_called(struct gsm_mncc_number *called,
			 const uint8_t *lv)
{
	uint8_t in_len = lv[0];

	if (in_len < 1)
		return -EINVAL;

	/* octet 3 */
	called->plan = lv[1] & 0x0f;
	called->type = (lv[1] & 0x70) >> 4;

	/* octet 4..N */
	gsm48_decode_bcd_number(called->number, sizeof(called->number), lv, 1);

	return 0;
}

/* encode 'called party BCD number' */
int gsm48_encode_called(struct msgb *msg,
			 const struct gsm_mncc_number *called)
{
	uint8_t lv[18];
	int ret;

	/* octet 3 */
	lv[1] = called->plan;
	lv[1] |= called->type << 4;

	/* octet 4..N, octet 2 */
	ret = gsm48_encode_bcd_number(lv, sizeof(lv), 1, called->number);
	if (ret < 0)
		return ret;

	msgb_tlv_put(msg, GSM48_IE_CALLED_BCD, lv[0], lv+1);

	return 0;
}

/* decode callerid of various IEs */
int gsm48_decode_callerid(struct gsm_mncc_number *callerid,
			 const uint8_t *lv)
{
	uint8_t in_len = lv[0];
	int i = 1;

	if (in_len < 1)
		return -EINVAL;

	/* octet 3 */
	callerid->plan = lv[1] & 0x0f;
	callerid->type = (lv[1] & 0x70) >> 4;

	/* octet 3a */
	if (!(lv[1] & 0x80)) {
		callerid->screen = lv[2] & 0x03;
		callerid->present = (lv[2] & 0x60) >> 5;
		i = 2;
	}

	/* octet 4..N */
	gsm48_decode_bcd_number(callerid->number, sizeof(callerid->number), lv, i);

	return 0;
}

/* encode callerid of various IEs */
int gsm48_encode_callerid(struct msgb *msg, int ie, int max_len,
			   const struct gsm_mncc_number *callerid)
{
	uint8_t lv[max_len - 1];
	int h_len = 1;
	int ret;

	/* octet 3 */
	lv[1] = callerid->plan;
	lv[1] |= callerid->type << 4;

	if (callerid->present || callerid->screen) {
		/* octet 3a */
		lv[2] = callerid->screen;
		lv[2] |= callerid->present << 5;
		lv[2] |= 0x80;
		h_len++;
	} else
		lv[1] |= 0x80;

	/* octet 4..N, octet 2 */
	ret = gsm48_encode_bcd_number(lv, sizeof(lv), h_len, callerid->number);
	if (ret < 0)
		return ret;

	msgb_tlv_put(msg, ie, lv[0], lv+1);

	return 0;
}

/* decode 'cause' */
int gsm48_decode_cause(struct gsm_mncc_cause *cause,
			const uint8_t *lv)
{
	uint8_t in_len = lv[0];
	int i;

	if (in_len < 2)
		return -EINVAL;

	cause->diag_len = 0;

	/* octet 3 */
	cause->location = lv[1] & 0x0f;
	cause->coding = (lv[1] & 0x60) >> 5;

	i = 1;
	if (!(lv[i] & 0x80)) {
		i++; /* octet 3a */
		if (in_len < i+1)
			return 0;
		cause->rec = 1;
		cause->rec_val = lv[i] & 0x7f;
	}
	i++;

	/* octet 4 */
	cause->value = lv[i] & 0x7f;
	i++;

	if (in_len < i) /* no diag */
		return 0;

	if (in_len - (i-1) > 32) /* maximum 32 octets */
		return 0;

	/* octet 5-N */
	memcpy(cause->diag, lv + i, in_len - (i-1));
	cause->diag_len = in_len - (i-1);

	return 0;
}

/* encode 'cause' */
int gsm48_encode_cause(struct msgb *msg, int lv_only,
			const struct gsm_mncc_cause *cause)
{
	uint8_t lv[32+4];
	int i;

	if (cause->diag_len > 32)
		return -EINVAL;

	/* octet 3 */
	lv[1] = cause->location;
	lv[1] |= cause->coding << 5;

	i = 1;
	if (cause->rec) {
		i++; /* octet 3a */
		lv[i] = cause->rec_val;
	}
	lv[i] |= 0x80; /* end of octet 3 */

	/* octet 4 */
	i++;
	lv[i] = 0x80 | cause->value;

	/* octet 5-N */
	if (cause->diag_len) {
		memcpy(lv + i, cause->diag, cause->diag_len);
		i += cause->diag_len;
	}

	lv[0] = i;
	if (lv_only)
		msgb_lv_put(msg, lv[0], lv+1);
	else
		msgb_tlv_put(msg, GSM48_IE_CAUSE, lv[0], lv+1);

	return 0;
}

/* decode 'calling number' */
int gsm48_decode_calling(struct gsm_mncc_number *calling,
			 const uint8_t *lv)
{
	return gsm48_decode_callerid(calling, lv);
}

/* encode 'calling number' */
int gsm48_encode_calling(struct msgb *msg, 
			  const struct gsm_mncc_number *calling)
{
	return gsm48_encode_callerid(msg, GSM48_IE_CALLING_BCD, 14, calling);
}

/* decode 'connected number' */
int gsm48_decode_connected(struct gsm_mncc_number *connected,
			 const uint8_t *lv)
{
	return gsm48_decode_callerid(connected, lv);
}

/* encode 'connected number' */
int gsm48_encode_connected(struct msgb *msg,
			    const struct gsm_mncc_number *connected)
{
	return gsm48_encode_callerid(msg, GSM48_IE_CONN_BCD, 14, connected);
}

/* decode 'redirecting number' */
int gsm48_decode_redirecting(struct gsm_mncc_number *redirecting,
			 const uint8_t *lv)
{
	return gsm48_decode_callerid(redirecting, lv);
}

/* encode 'redirecting number' */
int gsm48_encode_redirecting(struct msgb *msg,
			      const struct gsm_mncc_number *redirecting)
{
	return gsm48_encode_callerid(msg, GSM48_IE_REDIR_BCD, 19, redirecting);
}

/* decode 'facility' */
int gsm48_decode_facility(struct gsm_mncc_facility *facility,
			   const uint8_t *lv)
{
	uint8_t in_len = lv[0];

	if (in_len < 1)
		return -EINVAL;

	if (in_len > sizeof(facility->info))
		return -EINVAL;

	memcpy(facility->info, lv+1, in_len);
	facility->len = in_len;

	return 0;
}

/* encode 'facility' */
int gsm48_encode_facility(struct msgb *msg, int lv_only,
			   const struct gsm_mncc_facility *facility)
{
	uint8_t lv[GSM_MAX_FACILITY + 1];

	if (facility->len < 1 || facility->len > GSM_MAX_FACILITY)
		return -EINVAL;

	memcpy(lv+1, facility->info, facility->len);
	lv[0] = facility->len;
	if (lv_only)
		msgb_lv_put(msg, lv[0], lv+1);
	else
		msgb_tlv_put(msg, GSM48_IE_FACILITY, lv[0], lv+1);

	return 0;
}

/* decode 'notify' */
int gsm48_decode_notify(int *notify, const uint8_t *v)
{
	*notify = v[0] & 0x7f;

	return 0;
}

/* encode 'notify' */
int gsm48_encode_notify(struct msgb *msg, int notify)
{
	msgb_v_put(msg, notify | 0x80);

	return 0;
}

/* decode 'signal' */
int gsm48_decode_signal(int *signal, const uint8_t *v)
{
	*signal = v[0];

	return 0;
}

/* encode 'signal' */
int gsm48_encode_signal(struct msgb *msg, int signal)
{
	msgb_tv_put(msg, GSM48_IE_SIGNAL, signal);

	return 0;
}

/* decode 'keypad' */
int gsm48_decode_keypad(int *keypad, const uint8_t *lv)
{
	uint8_t in_len = lv[0];

	if (in_len < 1)
		return -EINVAL;

	*keypad = lv[1] & 0x7f;

	return 0;
}

/* encode 'keypad' */
int gsm48_encode_keypad(struct msgb *msg, int keypad)
{
	msgb_tv_put(msg, GSM48_IE_KPD_FACILITY, keypad);

	return 0;
}

/* decode 'progress' */
int gsm48_decode_progress(struct gsm_mncc_progress *progress,
			   const uint8_t *lv)
{
	uint8_t in_len = lv[0];

	if (in_len < 2)
		return -EINVAL;

	progress->coding = (lv[1] & 0x60) >> 5;
	progress->location = lv[1] & 0x0f;
	progress->descr = lv[2] & 0x7f;

	return 0;
}

/* encode 'progress' */
int gsm48_encode_progress(struct msgb *msg, int lv_only,
			   const struct gsm_mncc_progress *p)
{
	uint8_t lv[3];

	lv[0] = 2;
	lv[1] = 0x80 | ((p->coding & 0x3) << 5) | (p->location & 0xf);
	lv[2] = 0x80 | (p->descr & 0x7f);
	if (lv_only)
		msgb_lv_put(msg, lv[0], lv+1);
	else
		msgb_tlv_put(msg, GSM48_IE_PROGR_IND, lv[0], lv+1);

	return 0;
}

/* decode 'user-user' */
int gsm48_decode_useruser(struct gsm_mncc_useruser *uu,
			   const uint8_t *lv)
{
	uint8_t in_len = lv[0];
	char *info = uu->info;
	int info_len = sizeof(uu->info);
	int i;

	if (in_len < 1)
		return -EINVAL;

	uu->proto = lv[1];

	for (i = 2; i <= in_len; i++) {
		info_len--;
		if (info_len <= 1)
			break;
		*info++ = lv[i];
	}
	if (info_len >= 1)
		*info++ = '\0';

	return 0;
}

/* encode 'useruser' */
int gsm48_encode_useruser(struct msgb *msg, int lv_only,
			   const struct gsm_mncc_useruser *uu)
{
	uint8_t lv[GSM_MAX_USERUSER + 2];

	if (strlen(uu->info) > GSM_MAX_USERUSER)
		return -EINVAL;

	lv[0] = 1 + strlen(uu->info);
	lv[1] = uu->proto;
	memcpy(lv + 2, uu->info, strlen(uu->info));
	if (lv_only)
		msgb_lv_put(msg, lv[0], lv+1);
	else
		msgb_tlv_put(msg, GSM48_IE_USER_USER, lv[0], lv+1);

	return 0;
}

/* decode 'ss version' */
int gsm48_decode_ssversion(struct gsm_mncc_ssversion *ssv,
			    const uint8_t *lv)
{
	uint8_t in_len = lv[0];

	if (in_len < 1 || in_len < sizeof(ssv->info))
		return -EINVAL;

	memcpy(ssv->info, lv + 1, in_len);
	ssv->len = in_len;

	return 0;
}

/* encode 'ss version' */
int gsm48_encode_ssversion(struct msgb *msg,
			   const struct gsm_mncc_ssversion *ssv)
{
	uint8_t lv[GSM_MAX_SSVERSION + 1];

	if (ssv->len > GSM_MAX_SSVERSION)
		return -EINVAL;

	lv[0] = ssv->len;
	memcpy(lv + 1, ssv->info, ssv->len);
	msgb_tlv_put(msg, GSM48_IE_SS_VERS, lv[0], lv+1);

	return 0;
}

/* decode 'more data' does not require a function, because it has no value */

/* encode 'more data' */
int gsm48_encode_more(struct msgb *msg)
{
	uint8_t *ie;

	ie = msgb_put(msg, 1);
	ie[0] = GSM48_IE_MORE_DATA;

	return 0;
}

