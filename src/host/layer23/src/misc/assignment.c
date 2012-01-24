#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/gsm/gsm48_ie.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>

#include "assignment.h"

void parse_assignment(uint8_t *msg, unsigned len, struct gsm_sysinfo_freq *cell_arfcns, struct gsm_assignment *ga)
{
	struct gsm48_hdr *hdr;
	struct gsm48_ass_cmd *ac;
	struct gsm48_ho_cmd *hoc;
	struct gsm48_chan_desc *cd;
	int payload_len = 0; 
	uint8_t *payload_data;
	struct tlv_parsed tp;
	uint8_t *ma = 0;
	uint8_t ma_len;
	uint8_t ch_type, ch_subch, ch_ts;
	unsigned i, mask;

	hdr = (struct gsm48_hdr *) msg;

	/* handover */
	if (hdr->msg_type == 0x2b) {
		hoc = (struct gsm48_ho_cmd *) hdr->data;
		cd = &hoc->chan_desc;
		payload_len = len - 3 - sizeof(*hdr) - sizeof(*hoc);
		payload_data = hoc->data;
	}

	/* assignment */
	if (hdr->msg_type == 0x2e) {
		ac = (struct gsm48_ass_cmd *) hdr->data;
		cd = &ac->chan_desc;
		payload_len = len - 3 - sizeof(*hdr) - sizeof(*ac);
		payload_data = ac->data;
	}

	if (!payload_len)
		return;
		
	/* Parse TLV in the message */
	tlv_parse(&tp, &gsm48_rr_att_tlvdef, payload_data, payload_len, 0, 0);

	ma_len = 0;
	ma = NULL;
	mask = 0;

	/* Cell channel description */
	if (TLVP_PRESENT(&tp, GSM48_IE_CELL_CH_DESC)) {
		const uint8_t *v = TLVP_VAL(&tp, GSM48_IE_CELL_CH_DESC);
		uint8_t len = TLVP_LEN(&tp, GSM48_IE_CELL_CH_DESC);
		gsm48_decode_freq_list(cell_arfcns, (uint8_t *) v, len, 0xff, 0x02);
		mask = 0x02;
	} else if (TLVP_PRESENT(&tp, GSM48_IE_MA_AFTER)) {
		/* Mobile allocation */
		const uint8_t *v = TLVP_VAL(&tp, GSM48_IE_MA_AFTER);
		uint8_t len = TLVP_LEN(&tp, GSM48_IE_MA_AFTER);

		ma_len = len;
		ma = (uint8_t *) v;
		mask = 0x01;
	} else if (TLVP_PRESENT(&tp, GSM48_IE_FREQ_L_AFTER)) {
		/* Frequency list after time */
		const uint8_t *v = TLVP_VAL(&tp, GSM48_IE_FREQ_L_AFTER);
		uint8_t len = TLVP_LEN(&tp, GSM48_IE_FREQ_L_AFTER);
		gsm48_decode_freq_list(cell_arfcns, (uint8_t *) v, len, 0xff, 0x04);
		ma_len = 0;
		ma = NULL;
		mask = 0x04;
	} else {
		/* Use the old one */
		for (i=0; i<1024; i++) {
			cell_arfcns[i].mask &= ~0x02;
			if (cell_arfcns[i].mask & 0x01) {
				cell_arfcns[i].mask |= 0x02;
				mask = 0x02;
			}
		}
	}

	/* Channel mode (HR/FR/EFR/AMR) */
	if (TLVP_PRESENT(&tp, GSM48_IE_CHANMODE_1)) {
		const uint8_t *v = TLVP_VAL(&tp, GSM48_IE_CHANMODE_1);
		//uint8_t len = TLVP_LEN(&tp, GSM48_IE_CHANMODE_1);
		ga->chan_mode = v[0];
	}

	/* Multirate configuration */
	if (TLVP_PRESENT(&tp, GSM48_IE_MUL_RATE_CFG)) {
		const uint8_t *v = TLVP_VAL(&tp, GSM48_IE_MUL_RATE_CFG);
		//uint8_t len = TLVP_LEN(&tp, GSM48_IE_MUL_RATE_CFG);
		ga->rate_conf = v[1];
	}

	rsl_dec_chan_nr(cd->chan_nr, &ch_type, &ch_subch, &ch_ts);

	ga->h = cd->h0.h;
	ga->chan_nr = cd->chan_nr;

	if (!ga->h) {
		/* Non-Hopping */
		uint16_t arfcn = cd->h0.arfcn_low | (cd->h0.arfcn_high << 8);

		ga->tsc = cd->h0.tsc;
		ga->h0.band_arfcn = arfcn;
	} else {
		/* Hopping */
		uint16_t arfcn;
		int i, j, k;

		ga->tsc = cd->h1.tsc;
		ga->h1.maio = cd->h1.maio_low | (cd->h1.maio_high << 2);;
		ga->h1.hsn = cd->h1.hsn;

		/* decode mobile allocation */
		if (ma) {
			for (i=1, j=0; i<=1024; i++) {
				arfcn = i & 1023;
				if (cell_arfcns[arfcn].mask & mask) {
					k = ma_len - (j>>3) - 1;
					if (ma[k] & (1 << (j&7))) {
						ga->h1.ma[ga->h1.ma_len++] = arfcn;
					}
					j++;
				}
			}
			if (ga->h1.ma_len == 0) {
				/* cell information not found */
				/* just compute ma_len */
				for (i=0; i<(ma_len*8); i++){
					int k = i/8;
	
					if (ma[k] & (1 << (i&7))) {
						ga->h1.ma_len++;
					}
				}
			}
		} else {
			for (i=1, j=0; i<=1024; i++) {
				arfcn = i & 1023;
				if (cell_arfcns[arfcn].mask & mask) {
					ga->h1.ma[ga->h1.ma_len++] = arfcn;
				}
			}
		}
	}
}
