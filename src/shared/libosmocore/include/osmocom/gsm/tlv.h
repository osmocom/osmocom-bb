#ifndef _TLV_H
#define _TLV_H

#include <stdint.h>
#include <string.h>

#include <osmocom/core/msgb.h>

/*! \defgroup tlv GSM L3 compatible TLV parser
 *  @{
 */
/*! \file tlv.h */

/* Terminology / wording
		tag	length		value	(in bits)

	    V	-	-		8
	   LV	-	8		N * 8
	  TLV	8	8		N * 8
	TL16V	8	16		N * 8
	TLV16	8	8		N * 16
	 TvLV	8	8/16		N * 8
	vTvLV	8/16	8/16		N * 8

*/

/*! \brief gross length of a LV type field */
#define LV_GROSS_LEN(x)		(x+1)
/*! \brief gross length of a TLV type field */
#define TLV_GROSS_LEN(x)	(x+2)
/*! \brief gross length of a TLV16 type field */
#define TLV16_GROSS_LEN(x)	((2*x)+2)
/*! \brief gross length of a TL16V type field */
#define TL16V_GROSS_LEN(x)	(x+3)
/*! \brief gross length of a L16TV type field */
#define L16TV_GROSS_LEN(x)	(x+3)

/*! \brief maximum length of TLV of one byte length */
#define TVLV_MAX_ONEBYTE	0x7f

/*! \brief gross length of a TVLV type field */
static inline uint16_t TVLV_GROSS_LEN(uint16_t len)
{
	if (len <= TVLV_MAX_ONEBYTE)
		return TLV_GROSS_LEN(len);
	else
		return TL16V_GROSS_LEN(len);
}

/*! \brief gross length of vTvL header (tag+len) */
static inline uint16_t VTVL_GAN_GROSS_LEN(uint16_t tag, uint16_t len)
{
	uint16_t ret = 2;

	if (tag > TVLV_MAX_ONEBYTE)
		ret++;

	if (len > TVLV_MAX_ONEBYTE)
		ret++;

	return ret;
}

/*! \brief gross length of vTvLV (tag+len+val) */
static inline uint16_t VTVLV_GAN_GROSS_LEN(uint16_t tag, uint16_t len)
{
	uint16_t ret;

	if (len <= TVLV_MAX_ONEBYTE)
		return TLV_GROSS_LEN(len);
	else
		return TL16V_GROSS_LEN(len);

	if (tag > TVLV_MAX_ONEBYTE)
		ret += 1;

	return ret;
}

/* TLV generation */

/*! \brief put (append) a LV field */
static inline uint8_t *lv_put(uint8_t *buf, uint8_t len,
				const uint8_t *val)
{
	*buf++ = len;
	memcpy(buf, val, len);
	return buf + len;
}

/*! \brief put (append) a TLV field */
static inline uint8_t *tlv_put(uint8_t *buf, uint8_t tag, uint8_t len,
				const uint8_t *val)
{
	*buf++ = tag;
	*buf++ = len;
	memcpy(buf, val, len);
	return buf + len;
}

/*! \brief put (append) a TLV16 field */
static inline uint8_t *tlv16_put(uint8_t *buf, uint8_t tag, uint8_t len,
				const uint16_t *val)
{
	*buf++ = tag;
	*buf++ = len;
	memcpy(buf, val, len*2);
	return buf + len*2;
}

/*! \brief put (append) a TL16V field */
static inline uint8_t *tl16v_put(uint8_t *buf, uint8_t tag, uint16_t len,
				const uint8_t *val)
{
	*buf++ = tag;
	*buf++ = len >> 8;
	*buf++ = len & 0xff;
	memcpy(buf, val, len);
	return buf + len*2;
}

/*! \brief put (append) a TvLV field */
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

/*! \brief put (append) a variable-length tag or variable-length length * */
static inline uint8_t *vt_gan_put(uint8_t *buf, uint16_t tag)
{
	if (tag > TVLV_MAX_ONEBYTE) {
		/* two-byte TAG */
		*buf++ = 0x80 | (tag >> 8);
		*buf++ = (tag & 0xff);
	} else
		*buf++ = tag;

	return buf;
}

/* \brief put (append) vTvL (GAN) field (tag + length)*/
static inline uint8_t *vtvl_gan_put(uint8_t *buf, uint16_t tag, uint16_t len)
{
	uint8_t *ret;

	ret = vt_gan_put(buf, tag);
	return vt_gan_put(ret, len);
}

/* \brief put (append) vTvLV (GAN) field (tag + length + val) */
static inline uint8_t *vtvlv_gan_put(uint8_t *buf, uint16_t tag, uint16_t len,
				      const uint8_t *val)
{
	uint8_t *ret;

	ret = vtvl_gan_put(buf, tag, len );

	memcpy(ret, val, len);
	ret = buf + len;

	return ret;
}

/*! \brief put (append) a TLV16 field to \ref msgb */
static inline uint8_t *msgb_tlv16_put(struct msgb *msg, uint8_t tag, uint8_t len, const uint16_t *val)
{
	uint8_t *buf = msgb_put(msg, TLV16_GROSS_LEN(len));
	return tlv16_put(buf, tag, len, val);
}

/*! \brief put (append) a TL16V field to \ref msgb */
static inline uint8_t *msgb_tl16v_put(struct msgb *msg, uint8_t tag, uint16_t len,
					const uint8_t *val)
{
	uint8_t *buf = msgb_put(msg, TL16V_GROSS_LEN(len));
	return tl16v_put(buf, tag, len, val);
}

/*! \brief put (append) a TvLV field to \ref msgb */
static inline uint8_t *msgb_tvlv_put(struct msgb *msg, uint8_t tag, uint16_t len,
				      const uint8_t *val)
{
	uint8_t *buf = msgb_put(msg, TVLV_GROSS_LEN(len));
	return tvlv_put(buf, tag, len, val);
}

/*! \brief put (append) a vTvLV field to \ref msgb */
static inline uint8_t *msgb_vtvlv_gan_put(struct msgb *msg, uint16_t tag,
					  uint16_t len, const uint8_t *val)
{
	uint8_t *buf = msgb_put(msg, VTVLV_GAN_GROSS_LEN(tag, len));
	return vtvlv_gan_put(buf, tag, len, val);
}

/*! \brief put (append) a L16TV field to \ref msgb */
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

/*! \brief put (append) a V field */
static inline uint8_t *v_put(uint8_t *buf, uint8_t val)
{
	*buf++ = val;
	return buf;
}

/*! \brief put (append) a TV field */
static inline uint8_t *tv_put(uint8_t *buf, uint8_t tag, 
				uint8_t val)
{
	*buf++ = tag;
	*buf++ = val;
	return buf;
}

/*! \brief put (append) a TVfixed field */
static inline uint8_t *tv_fixed_put(uint8_t *buf, uint8_t tag,
				    unsigned int len, const uint8_t *val)
{
	*buf++ = tag;
	memcpy(buf, val, len);
	return buf + len;
}

/*! \brief put (append) a TV16 field
 *  \param[in,out] buf data buffer
 *  \param[in] tag Tag value
 *  \param[in] val Value (in host byte order!)
 */
static inline uint8_t *tv16_put(uint8_t *buf, uint8_t tag, 
				 uint16_t val)
{
	*buf++ = tag;
	*buf++ = val >> 8;
	*buf++ = val & 0xff;
	return buf;
}

/*! \brief put (append) a LV field to a \ref msgb
 *  \returns pointer to first byte after newly-put information */
static inline uint8_t *msgb_lv_put(struct msgb *msg, uint8_t len, const uint8_t *val)
{
	uint8_t *buf = msgb_put(msg, LV_GROSS_LEN(len));
	return lv_put(buf, len, val);
}

/*! \brief put (append) a TLV field to a \ref msgb
 *  \returns pointer to first byte after newly-put information */
static inline uint8_t *msgb_tlv_put(struct msgb *msg, uint8_t tag, uint8_t len, const uint8_t *val)
{
	uint8_t *buf = msgb_put(msg, TLV_GROSS_LEN(len));
	return tlv_put(buf, tag, len, val);
}

/*! \brief put (append) a TV field to a \ref msgb
 *  \returns pointer to first byte after newly-put information */
static inline uint8_t *msgb_tv_put(struct msgb *msg, uint8_t tag, uint8_t val)
{
	uint8_t *buf = msgb_put(msg, 2);
	return tv_put(buf, tag, val);
}

/*! \brief put (append) a TVfixed field to a \ref msgb
 *  \returns pointer to first byte after newly-put information */
static inline uint8_t *msgb_tv_fixed_put(struct msgb *msg, uint8_t tag,
					unsigned int len, const uint8_t *val)
{
	uint8_t *buf = msgb_put(msg, 1+len);
	return tv_fixed_put(buf, tag, len, val);
}

/*! \brief put (append) a V field to a \ref msgb
 *  \returns pointer to first byte after newly-put information */
static inline uint8_t *msgb_v_put(struct msgb *msg, uint8_t val)
{
	uint8_t *buf = msgb_put(msg, 1);
	return v_put(buf, val);
}

/*! \brief put (append) a TV16 field to a \ref msgb
 *  \returns pointer to first byte after newly-put information */
static inline uint8_t *msgb_tv16_put(struct msgb *msg, uint8_t tag, uint16_t val)
{
	uint8_t *buf = msgb_put(msg, 3);
	return tv16_put(buf, tag, val);
}

/*! \brief push (prepend) a TLV field to a \ref msgb
 *  \returns pointer to first byte of newly-pushed information */
static inline uint8_t *msgb_tlv_push(struct msgb *msg, uint8_t tag, uint8_t len, const uint8_t *val)
{
	uint8_t *buf = msgb_push(msg, TLV_GROSS_LEN(len));
	tlv_put(buf, tag, len, val);
	return buf;
}

/*! \brief push (prepend) a TV field to a \ref msgb
 *  \returns pointer to first byte of newly-pushed information */
static inline uint8_t *msgb_tv_push(struct msgb *msg, uint8_t tag, uint8_t val)
{
	uint8_t *buf = msgb_push(msg, 2);
	tv_put(buf, tag, val);
	return buf;
}

/*! \brief push (prepend) a TV16 field to a \ref msgb
 *  \returns pointer to first byte of newly-pushed information */
static inline uint8_t *msgb_tv16_push(struct msgb *msg, uint8_t tag, uint16_t val)
{
	uint8_t *buf = msgb_push(msg, 3);
	tv16_put(buf, tag, val);
	return buf;
}

/*! \brief push (prepend) a TvLV field to a \ref msgb
 *  \returns pointer to first byte of newly-pushed information */
static inline uint8_t *msgb_tvlv_push(struct msgb *msg, uint8_t tag, uint16_t len,
				      const uint8_t *val)
{
	uint8_t *buf = msgb_push(msg, TVLV_GROSS_LEN(len));
	tvlv_put(buf, tag, len, val);
	return buf;
}

/* \brief push (prepend) a vTvL header to a \ref msgb
 */
static inline uint8_t *msgb_vtvl_gan_push(struct msgb *msg, uint16_t tag,
					   uint16_t len)
{
	uint8_t *buf = msgb_push(msg, VTVL_GAN_GROSS_LEN(tag, len));
	vtvl_gan_put(buf, tag, len);
	return buf;
}


static inline uint8_t *msgb_vtvlv_gan_push(struct msgb *msg, uint16_t tag,
					   uint16_t len, const uint8_t *val)
{
	uint8_t *buf = msgb_push(msg, VTVLV_GAN_GROSS_LEN(tag, len));
	vtvlv_gan_put(buf, tag, len, val);
	return buf;
}

/* TLV parsing */

/*! \brief Entry in a TLV parser array */
struct tlv_p_entry {
	uint16_t len;		/*!< \brief length */
	const uint8_t *val;	/*!< \brief pointer to value */
};

/*! \brief TLV type */
enum tlv_type {
	TLV_TYPE_NONE,		/*!< \brief no type */
	TLV_TYPE_FIXED,		/*!< \brief fixed-length value-only */
	TLV_TYPE_T,		/*!< \brief tag-only */
	TLV_TYPE_TV,		/*!< \brief tag-value (8bit) */
	TLV_TYPE_TLV,		/*!< \brief tag-length-value */
	TLV_TYPE_TL16V,		/*!< \brief tag, 16 bit length, value */
	TLV_TYPE_TvLV,		/*!< \brief tag, variable length, value */
	TLV_TYPE_SINGLE_TV,	/*!< \brief tag and value (both 4 bit) in 1 byte */
	TLV_TYPE_vTvLV_GAN,	/*!< \brief variable-length tag, variable-length length */
};

/*! \brief Definition of a single IE (Information Element) */
struct tlv_def {
	enum tlv_type type;	/*!< \brief TLV type */
	uint8_t fixed_len;	/*!< \brief length in case of \ref TLV_TYPE_FIXED */
};

/*! \brief Definition of All 256 IE / TLV */
struct tlv_definition {
	struct tlv_def def[256];
};

/*! \brief result of the TLV parser */
struct tlv_parsed {
	struct tlv_p_entry lv[256];
};

extern struct tlv_definition tvlv_att_def;
extern struct tlv_definition vtvlv_gan_att_def;

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

/*! @} */

#endif /* _TLV_H */
