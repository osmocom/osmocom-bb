int rsl_enc_chan_h0(struct gsm48_chan_desc *cd, uint8_t tsc, uint16_t arfcn)
{
	cd->tsc = tsc;
	cd->h0 = 0;
	cd->h0.arfcn_low = arfcn & 0xff;
	cd->h0.arfcn_high = arfcn >> 8;

	return 0;
}

int rsl_enc_chan_h1(struct gsm48_chan_desc *cd, uint8_t tsc, uint8_t maio, uint8_t hsn)
{
	cd->tsc = tsc;
	cd->h1 = 1;
	cd->h1.maio_low = maio & 0x03;
	cd->h1.maio_high = maio >> 2;
	cd->h1.hsn = hsn;

	return 0;
}


int rsl_dec_chan_h0(struct gsm48_chan_desc *cd, uint8_t *tsc, uint16_t *arfcn)
{
	*tsc = cd->tsc;
	*arfcn = cd->h0.arfcn_low | (cd->h0.arfcn_high << 8);

	return 0;
}

int rsl_dec_chan_h1(struct gsm48_chan_desc *cd, uint8_t *tsc, uint8_t *maio, uint8_t *hsn)
{
	*tsc = cd->h1.tsc;
	*arfcn = cd->h1.maio_low | (cd->h1.maio_high << 2);
	*hsn = cd->h1.hsn;

	return 0;
}


tlv_parser.c: add into tlv_parse_one() right before switch-case statement.

	/* single octet TV IE */
	if ((tag & 0x80)) {
		*o_tag = tag & 0xf0;;
		*o_val = buf;
		*o_len = 1;
		return 1;
	}

/* encode 'classmark 1' */
int gsm48_encode_classmark1(struct msgb *msg, uint8_t rev_lev, uint8_t es_ind, uint8_t a5_1, uint8_t pwr_lev)
{
	struct gsm48_classmark1 cm;

	memset(&cm, 0, sizeof(cm));
	cm.rev_lev = rev_lev;
	cm.es_ind = es_ind;
	cm.a5_1 = a5_1;
	cm.pwr_lev = pwr_lev;
        msgb_v_put(msg, *((uint8_t *)&cm));

	return 0;
}

/* encode 'mobile identity' */
int gsm48_encode_mi(struct msgb *msg, struct gsm_subscriber *subscr, uint8_t mi_type)
{
	u_int8_t buf[11];
	u_int8_t *ie;

	switch(mi_type) {
	case GSM_MI_TYPE_TMSI:
		gsm48_generate_mid_from_tmsi(buf, subscr->tmsi);
		break;
	case GSM_MI_TYPE_IMSI:
		gsm48_generate_mid_from_imsi(buf, subscr->imsi);
		break;
	case GSM_MI_TYPE_IMEI:
	case GSM_MI_TYPE_IMEISV:
		gsm48_generate_mid_from_imsi(buf, subscr->imeisv);
		break;
	case GSM_MI_TYPE_NONE:
	default:
	        buf[0] = GSM48_IE_MOBILE_ID;
	        buf[1] = 1;
	        buf[2] = 0xf0 | GSM_MI_TYPE_NONE;
		break;
	}
	/* MI as LV */
	ie = msgb_put(msg, 1 + buf[1]);
	memcpy(ie, buf + 1, 1 + buf[1]);

	return 0;
}


