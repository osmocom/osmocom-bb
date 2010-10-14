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

	/* supported by configuration */
	uint8_t			sms_ptp;
	uint8_t			a5_1;
	uint8_t			a5_2;
	uint8_t			a5_3;
	uint8_t			a5_4;
	uint8_t			a5_5;
	uint8_t			a5_6;
	uint8_t			a5_7;
	uint8_t			p_gsm;
	uint8_t			e_gsm;
	uint8_t			r_gsm;
	uint8_t			dcs;
	uint8_t			class_900;
	uint8_t			class_dcs;
	uint8_t			full_v1;
	uint8_t			full_v2;
	uint8_t			full_v3;
	uint8_t			half_v1;
	uint8_t			half_v3;
	uint8_t			ch_cap; /* channel capability */
	int8_t			min_rxlev_db; /* min DB to access */

	/* radio */
	uint16_t		dsc_max;

	/* dialing */
	struct llist_head	abbrev;
};

struct gsm_settings_abbrev {
	struct llist_head	list;
	char			abbrev[4];
	char			number[32];
	char			name[32];
};

int gsm_settings_init(struct osmocom_ms *ms);
int gsm_settings_exit(struct osmocom_ms *ms);
char *gsm_check_imei(const char *imei, const char *sv);
int gsm_random_imei(struct gsm_settings *set);

#endif /* _settings_h */

