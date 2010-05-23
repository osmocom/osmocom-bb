#ifndef _settings_h
#define _settings_h

struct gsm_settings {
	int			simtype; /* selects card on power on */

	/* test card simulator settings */
	char 			test_imsi[20]; /* just in case... */
	uint8_t			test_barr;
	uint8_t			test_rplmn_valid;
	uint16_t		test_rplmn_mcc, test_rplmn_mnc;
	uint8_t			test_always; /* ...search hplmn... */
};

int gsm_settings_init(struct osmocom_ms *ms);

#endif /* _settings_h */

