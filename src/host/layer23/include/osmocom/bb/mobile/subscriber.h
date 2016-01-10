#ifndef _SUBSCRIBER_H
#define _SUBSCRIBER_H

/* GSM 04.08 4.1.2.2 SIM update status */
#define GSM_SIM_U0_NULL		0
#define GSM_SIM_U1_UPDATED	1
#define GSM_SIM_U2_NOT_UPDATED	2
#define GSM_SIM_U3_ROAMING_NA	3

struct gsm_sub_plmn_list {
	struct llist_head	entry;
	uint16_t		mcc, mnc;
};

struct gsm_sub_plmn_na {
	struct llist_head	entry;
	uint16_t		mcc, mnc;
	uint8_t			cause;
};

#define GSM_IMSI_LENGTH		16

enum {
	GSM_SIM_TYPE_NONE = 0,
	GSM_SIM_TYPE_READER,
	GSM_SIM_TYPE_TEST,
	GSM_SIM_TYPE_SAP
};

struct gsm_subscriber {
	struct osmocom_ms	*ms;

	/* status */
	uint8_t			sim_type; /* type of sim */
	uint8_t			sim_valid; /* sim inserted and valid */
	uint8_t			ustate; /* update status */
	uint8_t			imsi_attached; /* attached state */

	/* IMSI & co */
	char 			imsi[GSM_IMSI_LENGTH];
	char 			msisdn[31]; /* may include access codes */
	char 			iccid[21]; /* 20 + termination */

	/* TMSI / LAI */
	uint32_t		tmsi; /* invalid tmsi: 0xffffffff */
	uint16_t		mcc, mnc, lac; /* invalid lac: 0x0000 */


	/* key */
	uint8_t			key_seq; /* ciphering key sequence number */
	uint8_t			key[8]; /* 64 bit */

	/* other */
	struct llist_head	plmn_list; /* PLMN Selector field */
	struct llist_head	plmn_na; /* not allowed PLMNs */
	uint8_t			t6m_hplmn; /* timer for hplmn search */

	/* special things */
	uint8_t			always_search_hplmn;
		/* search hplmn in other countries also (for test cards) */
	uint8_t			any_timeout;
		/* timer to restart 'any cell selection' */
	char			sim_name[31]; /* name to load/save sim */
	char			sim_spn[17]; /* name of service privider */

	/* PLMN last registered */
	uint8_t			plmn_valid;
	uint16_t		plmn_mcc, plmn_mnc;

	/* our access */
	uint8_t			acc_barr; /* if we may access, if cell barred */
	uint16_t		acc_class; /* bitmask of what we may access */

	/* talk to SIM */
	uint8_t			sim_state;
	uint8_t			sim_pin_required; /* state: wait for PIN */
	uint8_t			sim_file_index;
	uint32_t		sim_handle_query;
	uint32_t		sim_handle_update;
	uint32_t		sim_handle_key;

	/* SMS */
	char			sms_sca[22];
};

int gsm_subscr_init(struct osmocom_ms *ms);
int gsm_subscr_exit(struct osmocom_ms *ms);
int gsm_subscr_testcard(struct osmocom_ms *ms, uint16_t mcc, uint16_t mnc,
	uint16_t lac, uint32_t tmsi, uint8_t imsi_attached);
int gsm_subscr_sapcard(struct osmocom_ms *ms);
int gsm_subscr_remove_sapcard(struct osmocom_ms *ms);
int gsm_subscr_simcard(struct osmocom_ms *ms);
void gsm_subscr_sim_pin(struct osmocom_ms *ms, char *pin1, char *pin2,
	int8_t mode);
int gsm_subscr_write_loci(struct osmocom_ms *ms);
int gsm_subscr_generate_kc(struct osmocom_ms *ms, uint8_t key_seq,
	uint8_t *rand, uint8_t no_sim);
int gsm_subscr_remove(struct osmocom_ms *ms);
void new_sim_ustate(struct gsm_subscriber *subscr, int state);
int gsm_subscr_del_forbidden_plmn(struct gsm_subscriber *subscr, uint16_t mcc,
	uint16_t mnc);
int gsm_subscr_add_forbidden_plmn(struct gsm_subscriber *subscr, uint16_t mcc,
					uint16_t mnc, uint8_t cause);
int gsm_subscr_is_forbidden_plmn(struct gsm_subscriber *subscr, uint16_t mcc,
					uint16_t mnc);
int gsm_subscr_dump_forbidden_plmn(struct osmocom_ms *ms,
			void (*print)(void *, const char *, ...), void *priv);
void gsm_subscr_dump(struct gsm_subscriber *subscr,
			void (*print)(void *, const char *, ...), void *priv);
char *gsm_check_imsi(const char *imsi);
int gsm_subscr_get_key_seq(struct osmocom_ms *ms, struct gsm_subscriber *subscr);

#endif /* _SUBSCRIBER_H */

