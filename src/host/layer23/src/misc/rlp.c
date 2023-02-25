/* RLP (Radio Link Protocol) as per 3GPP TS 24.022 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include "rlp.h"

/*! decode a RLP frame into its abstract representation. Doesn't check FCS correctness.
 *  \returns 0 in case of success; negative on error */
int rlp_decode(struct rlp_frame_decoded *out, uint8_t version, const uint8_t *data, size_t data_len)
{
	uint8_t n_s, n_r;

	/* we only support 240 bit so far */
	if (data_len != 240/8)
		return -EINVAL;

	/* we only support version 0+1 so far */
	if (version >= 2)
		return -EINVAL;

	memset(out, 0, sizeof(*out));
	out->version = version;

	out->c_r = data[0] & 1;
	n_s = (data[0] >> 3) | (data[1] & 1) << 5;
	n_r = (data[1] >> 2);
	//out->fcs = (data[240/8-3] << 16) | (data[240/8-2]) << 8 | (data[240/8-1] << 0);
	out->fcs = (data[240/8-1] << 16) | (data[240/8-2]) << 8 | (data[240/8-3] << 0);
	out->p_f = (data[1] >> 1) & 1;

	if (n_s == 0x3f) {
		out->ftype = RLP_FT_U;
		out->u_ftype = n_r & 0x1f;
	} else if (n_s == 0x3e) {
		out->ftype = RLP_FT_S;
		out->s_ftype = (data[0] >> 1) & 3;
		out->n_r = n_r;
	} else {
		out->ftype = RLP_FT_IS;
		out->s_ftype = (data[0] >> 1) & 3;
		out->n_s = n_s;
		out->n_r = n_r;
		memcpy(out->info, data+2, 240/8 - 5);
		out->info_len = 240/8 - 5;
	}

	return 0;
}

/*! encode a RLP frame from its abstract representation. Generates FCS.
 *  \returns number of output bytes used; negative on error */
int rlp_encode(uint8_t *out, size_t out_size, const struct rlp_frame_decoded *in)
{
	uint8_t out_len = 240/8;
	uint8_t n_s, n_r, s_bits;
	uint32_t fcs;

	/* we only support version 0+1 so far */
	if (in->version >= 2)
		return -EINVAL;

	/* we only support 240 bit so far */
	if (in->info_len != 240/8 - 5)
		return -EINVAL;

	if (out_size < out_len)
		return -EINVAL;

	memset(out, 0, out_len);

	if (in->c_r)
		out[0] |= 0x01;
	if (in->p_f)
		out[1] |= 0x02;

	switch (in->ftype) {
	case RLP_FT_U:
		n_s = 0x3f;
		n_r = in->u_ftype;
		s_bits = 0;
		break;
	case RLP_FT_S:
		n_s = 0x3e;
		n_r = in->n_r;
		s_bits = in->s_ftype;
		break;
	case RLP_FT_IS:
		n_s = in->n_s;
		n_r = in->n_r;
		s_bits = in->s_ftype;
		memcpy(out+2, in->info, in->info_len);
		break;
	default:
		return -EINVAL;
	}

	/* patch N(S) into output data */
	out[0] |= (n_s & 0x1F) << 3;
	out[1] |= (n_s & 0x20) >> 5;

	/* patch N(R) / M-bits into output data */
	out[1] |= (n_r & 0x3f) << 2;

	/* patch S-bits into output data */
	out[0] |= (s_bits & 3) << 1;

	/* compute FCS + add it to end of frame */
	fcs = rlp_fcs_compute(out, out_len -3);
	out[out_len - 3] = (fcs >> 0) & 0xff;
	out[out_len - 2] = (fcs >> 8) & 0xff;
	out[out_len - 1] = (fcs >> 16) & 0xff;

	return out_len;
}


static const uint32_t rlp_fcs_table[256] = {
	0x00B29D2D, 0x00643A5B, 0x0044D87A, 0x00927F0C, 0x00051C38, 0x00D3BB4E, 0x00F3596F, 0x0025FE19,
	0x008694BC, 0x005033CA, 0x0070D1EB, 0x00A6769D, 0x003115A9, 0x00E7B2DF, 0x00C750FE, 0x0011F788,
	0x00DA8E0F, 0x000C2979, 0x002CCB58, 0x00FA6C2E, 0x006D0F1A, 0x00BBA86C, 0x009B4A4D, 0x004DED3B,
	0x00EE879E, 0x003820E8, 0x0018C2C9, 0x00CE65BF, 0x0059068B, 0x008FA1FD, 0x00AF43DC, 0x0079E4AA,
	0x0062BB69, 0x00B41C1F, 0x0094FE3E, 0x00425948, 0x00D53A7C, 0x00039D0A, 0x00237F2B, 0x00F5D85D,
	0x0056B2F8, 0x0080158E, 0x00A0F7AF, 0x007650D9, 0x00E133ED, 0x0037949B, 0x001776BA, 0x00C1D1CC,
	0x000AA84B, 0x00DC0F3D, 0x00FCED1C, 0x002A4A6A, 0x00BD295E, 0x006B8E28, 0x004B6C09, 0x009DCB7F,
	0x003EA1DA, 0x00E806AC, 0x00C8E48D, 0x001E43FB, 0x008920CF, 0x005F87B9, 0x007F6598, 0x00A9C2EE,
	0x0049DA1E, 0x009F7D68, 0x00BF9F49, 0x0069383F, 0x00FE5B0B, 0x0028FC7D, 0x00081E5C, 0x00DEB92A,
	0x007DD38F, 0x00AB74F9, 0x008B96D8, 0x005D31AE, 0x00CA529A, 0x001CF5EC, 0x003C17CD, 0x00EAB0BB,
	0x0021C93C, 0x00F76E4A, 0x00D78C6B, 0x00012B1D, 0x00964829, 0x0040EF5F, 0x00600D7E, 0x00B6AA08,
	0x0015C0AD, 0x00C367DB, 0x00E385FA, 0x0035228C, 0x00A241B8, 0x0074E6CE, 0x005404EF, 0x0082A399,
	0x0099FC5A, 0x004F5B2C, 0x006FB90D, 0x00B91E7B, 0x002E7D4F, 0x00F8DA39, 0x00D83818, 0x000E9F6E,
	0x00ADF5CB, 0x007B52BD, 0x005BB09C, 0x008D17EA, 0x001A74DE, 0x00CCD3A8, 0x00EC3189, 0x003A96FF,
	0x00F1EF78, 0x0027480E, 0x0007AA2F, 0x00D10D59, 0x00466E6D, 0x0090C91B, 0x00B02B3A, 0x00668C4C,
	0x00C5E6E9, 0x0013419F, 0x0033A3BE, 0x00E504C8, 0x007267FC, 0x00A4C08A, 0x008422AB, 0x005285DD,
	0x001F18F0, 0x00C9BF86, 0x00E95DA7, 0x003FFAD1, 0x00A899E5, 0x007E3E93, 0x005EDCB2, 0x00887BC4,
	0x002B1161, 0x00FDB617, 0x00DD5436, 0x000BF340, 0x009C9074, 0x004A3702, 0x006AD523, 0x00BC7255,
	0x00770BD2, 0x00A1ACA4, 0x00814E85, 0x0057E9F3, 0x00C08AC7, 0x00162DB1, 0x0036CF90, 0x00E068E6,
	0x00430243, 0x0095A535, 0x00B54714, 0x0063E062, 0x00F48356, 0x00222420, 0x0002C601, 0x00D46177,
	0x00CF3EB4, 0x001999C2, 0x00397BE3, 0x00EFDC95, 0x0078BFA1, 0x00AE18D7, 0x008EFAF6, 0x00585D80,
	0x00FB3725, 0x002D9053, 0x000D7272, 0x00DBD504, 0x004CB630, 0x009A1146, 0x00BAF367, 0x006C5411,
	0x00A72D96, 0x00718AE0, 0x005168C1, 0x0087CFB7, 0x0010AC83, 0x00C60BF5, 0x00E6E9D4, 0x00304EA2,
	0x00932407, 0x00458371, 0x00656150, 0x00B3C626, 0x0024A512, 0x00F20264, 0x00D2E045, 0x00044733,
	0x00E45FC3, 0x0032F8B5, 0x00121A94, 0x00C4BDE2, 0x0053DED6, 0x008579A0, 0x00A59B81, 0x00733CF7,
	0x00D05652, 0x0006F124, 0x00261305, 0x00F0B473, 0x0067D747, 0x00B17031, 0x00919210, 0x00473566,
	0x008C4CE1, 0x005AEB97, 0x007A09B6, 0x00ACAEC0, 0x003BCDF4, 0x00ED6A82, 0x00CD88A3, 0x001B2FD5,
	0x00B84570, 0x006EE206, 0x004E0027, 0x0098A751, 0x000FC465, 0x00D96313, 0x00F98132, 0x002F2644,
	0x00347987, 0x00E2DEF1, 0x00C23CD0, 0x00149BA6, 0x0083F892, 0x00555FE4, 0x0075BDC5, 0x00A31AB3,
	0x00007016, 0x00D6D760, 0x00F63541, 0x00209237, 0x00B7F103, 0x00615675, 0x0041B454, 0x00971322,
	0x005C6AA5, 0x008ACDD3, 0x00AA2FF2, 0x007C8884, 0x00EBEBB0, 0x003D4CC6, 0x001DAEE7, 0x00CB0991,
	0x00686334, 0x00BEC442, 0x009E2663, 0x00488115, 0x00DFE221, 0x00094557, 0x0029A776, 0x00FF0000
};

/*! compute RLP FCS according to 3GPP TS 24.022 Section 4.4 */
uint32_t rlp_fcs_compute(const uint8_t *in, size_t in_len)
{
	uint32_t divider = 0;
	size_t i;

	for (i = 0; i < in_len; i++) {
		uint8_t input = in[i] ^ (divider & 0xff);
		divider = (divider >> 8) ^ rlp_fcs_table[input];
	}

	return divider;
}
