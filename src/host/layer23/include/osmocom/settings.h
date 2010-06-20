#ifndef _settings_h
#define _settings_h

struct gsm_settings {
	/* IMEI */
	char			imei[16];
	char			imeisv[17];
	char			imei_random;

	/* network search */
	int			plmn_mode; /* PLMN_MODE_* */

	/* SIM */
	int			simtype; /* selects card on power on */
	char 			emergency_imsi[20]; /* just in case... */

	/* test card simulator settings */
	char 			test_imsi[20]; /* just in case... */
	uint8_t			test_barr;
	uint8_t			test_rplmn_valid;
	uint16_t		test_rplmn_mcc, test_rplmn_mnc;
	uint8_t			test_always; /* ...search hplmn... */

	/* call related settings */
	uint8_t			cw; /* set if call-waiting is allowed */
	uint8_t			clip, clir;
};

int gsm_settings_init(struct osmocom_ms *ms);
char *gsm_check_imei(const char *imei, const char *sv);
int gsm_random_imei(struct gsm_settings *set);

#endif /* _settings_h */

