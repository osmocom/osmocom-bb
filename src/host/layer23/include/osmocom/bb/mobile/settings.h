#ifndef _settings_h
#define _settings_h

/* type of test SIM key */
enum {
	GSM_SIM_KEY_XOR = 0,
	GSM_SIM_KEY_COMP128
};

struct gsm_settings {
	/* IMEI */
	char			imei[16];
	char			imeisv[17];
	char			imei_random;

	/* network search */
	int			plmn_mode; /* PLMN_MODE_* */
	int8_t			min_rxlev_db; /* min DB to access */

	/* SIM */
	int			sim_type; /* selects card on power on */
	char 			emergency_imsi[20]; /* just in case... */

	/* test card simulator settings */
	char 			test_imsi[20]; /* just in case... */
	uint8_t			test_ki_type;
	uint8_t			test_ki[16]; /* 128 bit max */
	uint8_t			test_barr;
	uint8_t			test_rplmn_valid;
	uint16_t		test_rplmn_mcc, test_rplmn_mnc;
	uint8_t			test_always; /* ...search hplmn... */

	/* call related settings */
	uint8_t			cw; /* set if call-waiting is allowed */
	uint8_t			clip, clir;
	uint8_t			half, half_prefer;

	/* changing default behavior */
	uint8_t			alter_tx_power;
	uint8_t			alter_tx_power_value;
	int8_t			alter_delay;
	uint8_t			stick;
	uint16_t		stick_arfcn;
	uint8_t			no_lupd;
};

int gsm_settings_init(struct osmocom_ms *ms);
char *gsm_check_imei(const char *imei, const char *sv);
int gsm_random_imei(struct gsm_settings *set);

#endif /* _settings_h */

