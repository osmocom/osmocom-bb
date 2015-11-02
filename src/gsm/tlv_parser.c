/* (C) 2008-2010 by Harald Welte <laforge@gnumonks.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/tlv.h>

/*! \addtogroup tlv
 *  @{
 */
/*! \file tlv_parser.c */

struct tlv_definition tvlv_att_def;
struct tlv_definition vtvlv_gan_att_def;

/*! \brief Dump pasred TLV structure to stdout */
int tlv_dump(struct tlv_parsed *dec)
{
	int i;

	for (i = 0; i <= 0xff; i++) {
		if (!dec->lv[i].val)
			continue;
		printf("T=%02x L=%d\n", i, dec->lv[i].len);
	}
	return 0;
}

/*! \brief Parse a single TLV encoded IE
 *  \param[out] o_tag the tag of the IE that was found
 *  \param[out] o_len length of the IE that was found
 *  \param[out] o_val pointer to the data of the IE that was found
 *  \param[in] def structure defining the valid TLV tags / configurations
 *  \param[in] buf the input data buffer to be parsed
 *  \param[in] buf_len length of the input data buffer
 *  \returns number of bytes consumed by the TLV entry / IE parsed
 */
int tlv_parse_one(uint8_t *o_tag, uint16_t *o_len, const uint8_t **o_val,
		  const struct tlv_definition *def,
		  const uint8_t *buf, int buf_len)
{
	uint8_t tag;
	int len;

	tag = *buf;
	*o_tag = tag;

	/* single octet TV IE */
	if (def->def[tag & 0xf0].type == TLV_TYPE_SINGLE_TV) {
		*o_tag = tag & 0xf0;
		*o_val = buf;
		*o_len = 1;
		return 1;
	}

	/* FIXME: use tables for known IEI */
	switch (def->def[tag].type) {
	case TLV_TYPE_T:
		/* GSM TS 04.07 11.2.4: Type 1 TV or Type 2 T */
		*o_val = buf;
		*o_len = 0;
		len = 1;
		break;
	case TLV_TYPE_TV:
		*o_val = buf+1;
		*o_len = 1;
		len = 2;
		break;
	case TLV_TYPE_FIXED:
		*o_val = buf+1;
		*o_len = def->def[tag].fixed_len;
		len = def->def[tag].fixed_len + 1;
		break;
	case TLV_TYPE_TLV:
tlv:		/* GSM TS 04.07 11.2.4: Type 4 TLV */
		if (buf + 1 > buf + buf_len)
			return -1;
		*o_val = buf+2;
		*o_len = *(buf+1);
		len = *o_len + 2;
		if (len > buf_len)
			return -2;
		break;
	case TLV_TYPE_vTvLV_GAN:	/* 44.318 / 11.1.4 */
		/* FIXME: variable-length TAG! */
		if (*(buf+1) & 0x80) {
			/* like TL16Vbut without highest bit of len */
			if (2 > buf_len)
				return -1;
			*o_val = buf+3;
			*o_len = (*(buf+1) & 0x7F) << 8 | *(buf+2);
			len = *o_len + 3;
			if (len > buf_len)
				return -2;
		} else {
			/* like TLV */
			goto tlv;
		}
		break;
	case TLV_TYPE_TvLV:
		if (*(buf+1) & 0x80) {
			/* like TLV, but without highest bit of len */
			if (buf + 1 > buf + buf_len)
				return -1;
			*o_val = buf+2;
			*o_len = *(buf+1) & 0x7f;
			len = *o_len + 2;
			if (len > buf_len)
				return -2;
			break;
		}
		/* like TL16V, fallthrough */
	case TLV_TYPE_TL16V:
		if (2 > buf_len)
			return -1;
		*o_val = buf+3;
		*o_len = *(buf+1) << 8 | *(buf+2);
		len = *o_len + 3;
		if (len > buf_len)
			return -2;
		break;
	default:
		return -3;
	}

	return len;
}

/*! \brief Parse an entire buffer of TLV encoded Information Elements
 *  \param[out] dec caller-allocated pointer to \ref tlv_parsed
 *  \param[in] def structure defining the valid TLV tags / configurations
 *  \param[in] buf the input data buffer to be parsed
 *  \param[in] buf_len length of the input data buffer
 *  \param[in] lv_tag an initial LV tag at the start of the buffer
 *  \param[in] lv_tag2 a second initial LV tag following the \a lv_tag
 *  \returns number of bytes consumed by the TLV entry / IE parsed
 */
int tlv_parse(struct tlv_parsed *dec, const struct tlv_definition *def,
	      const uint8_t *buf, int buf_len, uint8_t lv_tag,
	      uint8_t lv_tag2)
{
	int ofs = 0, num_parsed = 0;
	uint16_t len;

	memset(dec, 0, sizeof(*dec));

	if (lv_tag) {
		if (ofs > buf_len)
			return -1;
		dec->lv[lv_tag].val = &buf[ofs+1];
		dec->lv[lv_tag].len = buf[ofs];
		len = dec->lv[lv_tag].len + 1;
		if (ofs + len > buf_len)
			return -2;
		num_parsed++;
		ofs += len;
	}
	if (lv_tag2) {
		if (ofs > buf_len)
			return -1;
		dec->lv[lv_tag2].val = &buf[ofs+1];
		dec->lv[lv_tag2].len = buf[ofs];
		len = dec->lv[lv_tag2].len + 1;
		if (ofs + len > buf_len)
			return -2;
		num_parsed++;
		ofs += len;
	}

	while (ofs < buf_len) {
		int rv;
		uint8_t tag;
		const uint8_t *val;

		rv = tlv_parse_one(&tag, &len, &val, def,
		                   &buf[ofs], buf_len-ofs);
		if (rv < 0)
			return rv;
		dec->lv[tag].val = val;
		dec->lv[tag].len = len;
		ofs += rv;
		num_parsed++;
	}
	//tlv_dump(dec);
	return num_parsed;
}

/*! \brief take a master (src) tlvdev and fill up all empty slots in 'dst' */
void tlv_def_patch(struct tlv_definition *dst, const struct tlv_definition *src)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dst->def); i++) {
		if (src->def[i].type == TLV_TYPE_NONE)
			continue;
		if (dst->def[i].type == TLV_TYPE_NONE)
			dst->def[i] = src->def[i];
	}
}

static __attribute__((constructor)) void on_dso_load_tlv(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(tvlv_att_def.def); i++)
		tvlv_att_def.def[i].type = TLV_TYPE_TvLV;

	for (i = 0; i < ARRAY_SIZE(vtvlv_gan_att_def.def); i++)
		vtvlv_gan_att_def.def[i].type = TLV_TYPE_vTvLV_GAN;
}

/*! @} */
