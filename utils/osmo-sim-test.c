/* libosmosim test application - currently simply dumps a USIM */
/* (C) 2012 by Harald Welte <laforge@gnumonks.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/sim/sim.h>
#include <osmocom/gsm/tlv.h>


/* FIXME: this needs to be moved to card_fs_uicc.c */

/* 11.1.1 */
static struct msgb *_select_file(struct osim_chan_hdl *st, uint8_t p1, uint8_t p2,
			const uint8_t *data, uint8_t data_len)
{
	struct msgb *msg, *resp;
	char *dst;

	msg = osim_new_apdumsg(0x00, 0xA4, p1, p2, data_len, 256);
	dst = msgb_put(msg, data_len);
	memcpy(dst, data, data_len);

	osim_transceive_apdu(st, msg);

	return msg;
}

/* 11.1.1 */
static struct msgb *select_adf(struct osim_chan_hdl *st, const uint8_t *adf, uint8_t adf_len)
{
	int sw;

	return _select_file(st, 0x04, 0x04, adf,adf_len);
}

/* 11.1.1 */
static struct msgb *select_file(struct osim_chan_hdl *st, uint16_t fid)
{
	uint16_t cfid = htons(fid);

	return _select_file(st, 0x00, 0x04, (uint8_t *)&cfid, 2);
}

/* 11.1.9 */
static int verify_pin(struct osim_chan_hdl *st, uint8_t pin_nr, uint8_t *pin)
{
	struct msgb *msg;
	char *pindst;
	int sw;

	if (strlen(pin) > 8)
		return -EINVAL;

	msg = osim_new_apdumsg(0x00, 0x20, 0x00, pin_nr, 8, 0);
	pindst = msgb_put(msg, 8);
	memset(pindst, 0xFF, 8);
	strncpy(pindst, pin, strlen(pin));

	return osim_transceive_apdu(st, msg);
}

/* 11.1.5 */
static struct msgb *read_record_nr(struct osim_chan_hdl *st, uint8_t rec_nr, uint16_t rec_size)
{
	struct msgb *msg;

	msg = osim_new_apdumsg(0x00, 0xB2, rec_nr, 0x04, 0, rec_size);

	osim_transceive_apdu(st, msg);

	return msg;
}

/* 11.1.6 */
static struct msgb *update_record_nr(struct osim_chan_hdl *st, uint8_t rec_nr,
				     const uint8_t *data, uint16_t rec_size)
{
	struct msgb *msg;
	uint8_t *cur;

	msg = osim_new_apdumsg(0x00, 0xDC, rec_nr, 0x04, rec_size, 0);
	cur = msgb_put(msg, rec_size);
	memcpy(cur, data, rec_size);

	osim_transceive_apdu(st, msg);

	return msg;
}

/* 11.1.3 */
static struct msgb *read_binary(struct osim_chan_hdl *st, uint16_t offset, uint16_t len)
{
	struct msgb *msg;

	if (offset > 0x7fff || len > 256)
		return NULL;

	msg = osim_new_apdumsg(0x00, 0xB0, offset >> 8, offset & 0xff, 0, len & 0xff);

	osim_transceive_apdu(st, msg);

	return msg;
}

/* 11.1.4 */
static struct msgb *update_binary(struct osim_chan_hdl *st, uint16_t offset,
				  const uint8_t *data, uint16_t len)
{
	struct msgb *msg;
	uint8_t *cur;

	if (offset > 0x7fff || len > 256)
		return NULL;

	msg = osim_new_apdumsg(0x00, 0xD6, offset >> 8, offset & 0xff, len & 0xff, 0);
	cur = msgb_put(msg, len);
	memcpy(cur, data, len);

	osim_transceive_apdu(st, msg);

	return msg;
}



static int dump_fcp_template(struct tlv_parsed *tp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tp->lv); i++) {
		if (TLVP_PRESENT(tp, i))
			printf("Tag 0x%02x (%s): %s\n", i,
				get_value_string(ts102221_fcp_vals, i),
				osmo_hexdump(TLVP_VAL(tp, i), TLVP_LEN(tp, i)));
	}

	return 0;
}

static int dump_fcp_template_msg(struct msgb *msg)
{
	struct tlv_parsed tp;
	int rc;

	rc = tlv_parse(&tp, &ts102221_fcp_tlv_def, msgb_apdu_de(msg)+2, msgb_apdu_le(msg)-2, 0, 0);
	if (rc < 0)
		return rc;

	return dump_fcp_template(&tp);
}

struct osim_fcp_fd_decoded {
	enum osim_file_type type;
	enum osim_ef_type ef_type;
	uint16_t rec_len;
	uint8_t num_rec;
};

static const enum osim_file_type iso2ftype[8] = {
	[0] = TYPE_EF,
	[1] = TYPE_EF_INT,
	[7] = TYPE_DF,
};

static const enum osim_ef_type iso2eftype[8] = {
	[1] = EF_TYPE_TRANSP,
	[2] = EF_TYPE_RECORD_FIXED,
	[6] = EF_TYPE_RECORD_CYCLIC,
};

static int osim_fcp_fd_decode(struct osim_fcp_fd_decoded *ofd, const uint8_t *fcp, int fcp_len)
{
	memset(ofd, 0, sizeof(*ofd));

	if (fcp_len != 2 && fcp_len != 5)
		return -EINVAL;

	ofd->type = iso2ftype[(fcp[0] >> 3) & 7];
	if (ofd->type != TYPE_DF)
		ofd->ef_type = iso2eftype[fcp[0] & 7];

	if (fcp[1] != 0x21)
		return -EINVAL;

	if (fcp_len >= 5) {
		ofd->rec_len = ntohs(*(uint16_t *)(fcp+2));
		ofd->num_rec = fcp[4];
	}

	return 0;
}

extern struct osim_card_profile *osim_cprof_usim(void *ctx);

static struct msgb *try_select_adf_usim(struct osim_chan_hdl *st)
{
	struct tlv_parsed tp;
	struct osim_fcp_fd_decoded ofd;
	struct msgb *msg, *msg2;
	uint8_t *cur;
	int rc, i;

	msg = select_file(st, 0x2f00);
	rc = tlv_parse(&tp, &ts102221_fcp_tlv_def, msgb_apdu_de(msg)+2, msgb_apdu_le(msg)-2, 0, 0);
	if (rc < 0)
		return NULL;

	dump_fcp_template(&tp);

	if (!TLVP_PRESENT(&tp, UICC_FCP_T_FILE_DESC) ||
	    TLVP_LEN(&tp, UICC_FCP_T_FILE_DESC) < 5) {
		msgb_free(msg);
		return NULL;
	}

	rc = osim_fcp_fd_decode(&ofd, TLVP_VAL(&tp, UICC_FCP_T_FILE_DESC),
				TLVP_LEN(&tp, UICC_FCP_T_FILE_DESC));
	if (rc < 0) {
		msgb_free(msg);
		return NULL;
	}

	if (ofd.type != TYPE_EF || ofd.ef_type != EF_TYPE_RECORD_FIXED) {
		msgb_free(msg);
		return NULL;
	}

	msgb_free(msg);

	printf("ofd rec_len = %u, num_rec = %u\n", ofd.rec_len, ofd.num_rec);

	for (i = 0; i < ofd.num_rec; i++) {
		msg = read_record_nr(st, i+1, ofd.rec_len);
		if (!msg)
			return NULL;

		cur = msgb_apdu_de(msg);
		if (msgb_apdu_le(msg) < 5) {
			msgb_free(msg);
			return NULL;
		}

		if (cur[0] != 0x61 || cur[1] < 0x03 || cur[1] > 0x7f ||
		    cur[2] != 0x4F || cur[3] < 0x01 || cur[3] > 0x10) {
			msgb_free(msg);
			return NULL;
		}

		/* FIXME: actually check if it is an AID that we support, or
		 * iterate until we find one that we support */

		msg2 = select_adf(st, cur+4, cur[3]);

		/* attach the USIM profile, FIXME: do this based on AID match */
		st->card->prof = osim_cprof_usim(st->card);
		st->cwd = osim_file_desc_find_name(st->card->prof->mf, "ADF.USIM");

		msgb_free(msg);

		return msg2;
	}

	return NULL;
}

static int dump_file(struct osim_chan_hdl *chan, uint16_t fid)
{
	struct tlv_parsed tp;
	struct osim_fcp_fd_decoded ffdd;
	struct msgb *msg, *rmsg;
	int rc, i, offset;

	msg = select_file(chan, fid);
	if (!msg) {
		printf("Unable to select file\n");
		return -EIO;
	}
	printf("SW: %s\n", osim_print_sw(chan->card, msgb_apdu_sw(msg)));
	if (msgb_apdu_sw(msg) != 0x9000) {
		printf("status 0x%04x selecting file\n", msgb_apdu_sw(msg));
		goto out;
	}

	rc = tlv_parse(&tp, &ts102221_fcp_tlv_def, msgb_apdu_de(msg)+2, msgb_apdu_le(msg)-2, 0, 0);
	if (rc < 0) {
		printf("Unable to parse FCP\n");
		goto out;
	}

	if (!TLVP_PRESENT(&tp, UICC_FCP_T_FILE_DESC) ||
	    TLVP_LEN(&tp, UICC_FCP_T_FILE_DESC) < 2) {
		printf("No file descriptor present ?!?\n");
		goto out;
	}

	rc = osim_fcp_fd_decode(&ffdd, TLVP_VAL(&tp, UICC_FCP_T_FILE_DESC),
				TLVP_LEN(&tp, UICC_FCP_T_FILE_DESC));
	if (rc < 0) {
		printf("Unable to decode File Descriptor\n");
		goto out;
	}

	if (ffdd.type != TYPE_EF) {
		printf("File Type != EF\n");
		goto out;
	}

	printf("EF type: %u\n", ffdd.ef_type);

	switch (ffdd.ef_type) {
	case EF_TYPE_RECORD_FIXED:
		for (i = 0; i < ffdd.num_rec; i++) {
			rmsg = read_record_nr(chan, i+1, ffdd.rec_len);
			if (!msg)
				return -EIO;
			printf("SW: %s\n", osim_print_sw(chan->card, msgb_apdu_sw(msg)));
			printf("Rec %03u: %s\n", i+1,
				osmo_hexdump(msgb_apdu_de(rmsg), msgb_apdu_le(rmsg)));
		}
		break;
	case EF_TYPE_TRANSP:
		if (!TLVP_PRESENT(&tp, UICC_FCP_T_FILE_SIZE))
			goto out;
		i = ntohs(*(uint16_t *)TLVP_VAL(&tp, UICC_FCP_T_FILE_SIZE));
		printf("File size: %d bytes\n", i);

		for (offset = 0; offset < i-1; ) {
			uint16_t remain_len = i - offset;
			uint16_t read_len = OSMO_MIN(remain_len, 256);
			rmsg = read_binary(chan, offset, read_len);
			if (!rmsg)
				return -EIO;
			offset += read_len;
			printf("Content: %s\n",
				osmo_hexdump(msgb_apdu_de(rmsg), msgb_apdu_le(rmsg)));
		}
		break;
	default:
		goto out;
	}

out:
	msgb_free(msg);
	return -EINVAL;
}

int main(int argc, char **argv)
{
	struct osim_reader_hdl *reader;
	struct osim_card_hdl *card;
	struct osim_chan_hdl *chan;
	struct msgb *msg;
	int rc;

	reader = osim_reader_open(OSIM_READER_DRV_PCSC, 0, "", NULL);
	if (!reader)
		exit(1);
	card = osim_card_open(reader, OSIM_PROTO_T0);
	if (!card)
		exit(2);
	chan = llist_entry(card->channels.next, struct osim_chan_hdl, list);
	if (!chan)
		exit(3);

	msg = try_select_adf_usim(chan);
	if (!msg || msgb_apdu_sw(msg) != 0x9000)
		exit(4);
	dump_fcp_template_msg(msg);
	msgb_free(msg);

	msg = select_file(chan, 0x6fc5);
	dump_fcp_template_msg(msg);
	printf("SW: %s\n", osim_print_sw(chan->card, msgb_apdu_sw(msg)));
	msgb_free(msg);

	verify_pin(chan, 1, "1653");

	msg = select_file(chan, 0x6f06);
	dump_fcp_template_msg(msg);
	msgb_free(msg);

	{
		struct osim_file_desc *ofd;
		llist_for_each_entry(ofd, &chan->cwd->child_list, list) {
			struct msgb *m;
			printf("\n\n================ %s (%s) ==================\n",
				ofd->short_name, ofd->long_name);

			m = select_file(chan, ofd->fid);
			dump_fcp_template_msg(m);
			msgb_free(m);
			dump_file(chan, ofd->fid);
		}
	}

	exit(0);
}
