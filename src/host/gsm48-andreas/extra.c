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

