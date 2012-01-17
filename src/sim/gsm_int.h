
int gsm_hpplmn_decode(struct osim_decoded_data *dd,
		     const struct osim_file_desc *desc,
		     int len, uint8_t *data);

int gsm_lp_decode(struct osim_decoded_data *dd,
		 const struct osim_file_desc *desc,
		 int len, uint8_t *data);

int gsm_imsi_decode(struct osim_decoded_data *dd,
		   const struct osim_file_desc *desc,
		   int len, uint8_t *data);
