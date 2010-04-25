#ifndef _TLV_H
#define _TLV_H

#include <stdint.h>
#include <string.h>

#include <osmocore/msgb.h>

/* Terminology / wording
		tag	length		value	(in bits)

	    V	-	-		8
	   LV	-	8		N * 8
	  TLV	8	8		N * 8
	TL16V	8	16		N * 8
	TLV16	8	8		N * 16
	 TvLV	8	8/16		N * 8

*/

#define LV_GROSS_LEN(x)		(x+1)
#define TLV_GROSS_LEN(x)	(x+2)
#define TLV16_GROSS_LEN(x)	((2*x)+2)
#define TL16V_GROSS_LEN(x)	(x+3)
#define L16TV_GROSS_LEN(x)	(x+3)

#define TVLV_MAX_ONEBYTE	0x7f

static inline uint16_t TVLV_GROSS_LEN(uint16_t len)
{
	if (len <= TVLV_MAX_ONEBYTE)
		return TLV_GROSS_LEN(len);
	else
		return TL16V_GROSS_LEN(len);
}

/* TLV generation */

static inline uint8_t *lv_put(uint8_t *buf, uint8_t len,
				const uint8_t *val)
{
	*buf++ = len;
	memcpy(buf, val, len);
	return buf + len;
}

static inline uint8_t *tlv_put(uint8_t *buf, uint8_t tag, uint8_t len,
				const uint8_t *val)
{
	*buf++ = tag;
	*buf++ = len;
	memcpy(buf, val, len);
	return buf + len;
}

static inline uint8_t *tlv16_put(uint8_t *buf, uint8_t tag, uint8_t len,
				const uint16_t *val)
{
	*buf++ = tag;
	*buf++ = len;
	memcpy(buf, val, len*2);
	return buf + len*2;
}

static inline uint8_t *tl16v_put(uint8_t *buf, uint8_t tag, uint16_t len,
				const uint8_t *val)
{
	*buf++ = tag;
	*buf++ = len >> 8;
	*buf++ = len & 0xff;
	memcpy(buf, val, len);
	return buf + len*2;
}

static inline uint8_t *tvlv_put(uint8_t *buf, uint8_t tag, uint16_t len,
				 const uint8_t *val)
{
	uint8_t *ret;

	if (len <= TVLV_MAX_ONEBYTE) {
		ret = tlv_put(buf, tag, len, val);
		buf[1] |= 0x80;
	} else
		ret = tl16v_put(buf, tag, len, val);

	return ret;
}

static inline uint8_t *msgb_tlv16_put(struct msgb *msg, uint8_t tag, uint8_t len, const uint16_t *val)
{
	uint8_t *buf = msgb_put(msg, TLV16_GROSS_LEN(len));
	return tlv16_put(buf, tag, len, val);
}

static inline uint8_t *msgb_tl16v_put(struct msgb *msg, uint8_t tag, uint16_t len,
					const uint8_t *val)
{
	uint8_t *buf = msgb_put(msg, TL16V_GROSS_LEN(len));
	return tl16v_put(buf, tag, len, val);
}

static inline uint8_t *msgb_tvlv_put(struct msgb *msg, uint8_t tag, uint16_t len,
				      const uint8_t *val)
{
	uint8_t *buf = msgb_put(msg, TVLV_GROSS_LEN(len));
	return tvlv_put(buf, tag, len, val);
}

static inline uint8_t *msgb_l16tv_put(struct msgb *msg, uint16_t len, uint8_t tag,
                                       const uint8_t *val)
{
	uint8_t *buf = msgb_put(msg, L16TV_GROSS_LEN(len));

	*buf++ = len >> 8;
	*buf++ = len & 0xff;
	*buf++ = tag;
	memcpy(buf, val, len);
	return buf + len;
}

static inline uint8_t *v_put(uint8_t *buf, uint8_t val)
{
	*buf++ = val;
	return buf;
}

static inline uint8_t *tv_put(uint8_t *buf, uint8_t tag, 
				uint8_t val)
{
	*buf++ = tag;
	*buf++ = val;
	return buf;
}

/* 'val' is still in host byte order! */
static inline uint8_t *tv16_put(uint8_t *buf, uint8_t tag, 
				 uint16_t val)
{
	*buf++ = tag;
	*buf++ = val >> 8;
	*buf++ = val & 0xff;
	return buf;
}

static inline uint8_t *msgb_lv_put(struct msgb *msg, uint8_t len, const uint8_t *val)
{
	uint8_t *buf = msgb_put(msg, LV_GROSS_LEN(len));
	return lv_put(buf, len, val);
}

static inline uint8_t *msgb_tlv_put(struct msgb *msg, uint8_t tag, uint8_t len, const uint8_t *val)
{
	uint8_t *buf = msgb_put(msg, TLV_GROSS_LEN(len));
	return tlv_put(buf, tag, len, val);
}

static inline uint8_t *msgb_tv_put(struct msgb *msg, uint8_t tag, uint8_t val)
{
	uint8_t *buf = msgb_put(msg, 2);
	return tv_put(buf, tag, val);
}

static inline uint8_t *msgb_v_put(struct msgb *msg, uint8_t val)
{
	uint8_t *buf = msgb_put(msg, 1);
	return v_put(buf, val);
}

static inline uint8_t *msgb_tv16_put(struct msgb *msg, uint8_t tag, uint16_t val)
{
	uint8_t *buf = msgb_put(msg, 3);
	return tv16_put(buf, tag, val);
}

static inline uint8_t *msgb_tlv_push(struct msgb *msg, uint8_t tag, uint8_t len, const uint8_t *val)
{
	uint8_t *buf = msgb_push(msg, TLV_GROSS_LEN(len));
	return tlv_put(buf, tag, len, val);
}

static inline uint8_t *msgb_tv_push(struct msgb *msg, uint8_t tag, uint8_t val)
{
	uint8_t *buf = msgb_push(msg, 2);
	return tv_put(buf, tag, val);
}

static inline uint8_t *msgb_tv16_push(struct msgb *msg, uint8_t tag, uint16_t val)
{
	uint8_t *buf = msgb_push(msg, 3);
	return tv16_put(buf, tag, val);
}

static inline uint8_t *msgb_tvlv_push(struct msgb *msg, uint8_t tag, uint16_t len,
				      const uint8_t *val)
{
	uint8_t *buf = msgb_push(msg, TVLV_GROSS_LEN(len));
	return tvlv_put(buf, tag, len, val);
}

/* TLV parsing */

struct tlv_p_entry {
	uint16_t len;
	const uint8_t *val;
};

enum tlv_type {
	TLV_TYPE_NONE,
	TLV_TYPE_FIXED,
	TLV_TYPE_T,
	TLV_TYPE_TV,
	TLV_TYPE_TLV,
	TLV_TYPE_TL16V,
	TLV_TYPE_TvLV,
	TLV_TYPE_SINGLE_TV
};

struct tlv_def {
	enum tlv_type type;
	uint8_t fixed_len;
};

struct tlv_definition {
	struct tlv_def def[0xff];
};

struct tlv_parsed {
	struct tlv_p_entry lv[0xff];
};

extern struct tlv_definition tvlv_att_def;

int tlv_parse_one(uint8_t *o_tag, uint16_t *o_len, const uint8_t **o_val,
                  const struct tlv_definition *def,
                  const uint8_t *buf, int buf_len);
int tlv_parse(struct tlv_parsed *dec, const struct tlv_definition *def,
	      const uint8_t *buf, int buf_len, uint8_t lv_tag, uint8_t lv_tag2);
/* take a master (src) tlvdev and fill up all empty slots in 'dst' */
void tlv_def_patch(struct tlv_definition *dst, const struct tlv_definition *src);

#define TLVP_PRESENT(x, y)	((x)->lv[y].val)
#define TLVP_LEN(x, y)		(x)->lv[y].len
#define TLVP_VAL(x, y)		(x)->lv[y].val

#endif /* _TLV_H */
