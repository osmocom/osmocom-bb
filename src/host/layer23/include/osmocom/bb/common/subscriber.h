#ifndef _SUBSCRIBER_H
#define _SUBSCRIBER_H

#include <stdbool.h>

#include <osmocom/core/utils.h>
#include <osmocom/gsm/protocol/gsm_23_003.h>
#include <osmocom/gsm/gsm23003.h>

/* GSM 04.08 4.1.2.2 SIM update status */
enum gsm_sub_sim_ustate {
	GSM_SIM_U0_NULL,
	GSM_SIM_U1_UPDATED,
	GSM_SIM_U2_NOT_UPDATED,
	GSM_SIM_U3_ROAMING_NA,
};
extern const struct value_string gsm_sub_sim_ustate_names[];
static inline const char *gsm_sub_sim_ustate_name(enum gsm_sub_sim_ustate val)
{
	return get_value_string(gsm_sub_sim_ustate_names, val);
}


struct gsm_sub_plmn_list {
	struct llist_head	entry;
	struct osmo_plmn_id	plmn;
};

struct gsm_sub_plmn_na {
	struct llist_head	entry;
	struct osmo_plmn_id	plmn;
	uint8_t			cause;
};

#define GSM_SIM_IS_READER(type) \
	(type == GSM_SIM_TYPE_L1PHY || type == GSM_SIM_TYPE_SAP)

enum gsm_subscriber_sim_type {
	GSM_SIM_TYPE_NONE = 0,
	GSM_SIM_TYPE_L1PHY,
	GSM_SIM_TYPE_TEST,
	GSM_SIM_TYPE_SAP
};

struct gsm_subscriber {
	struct osmocom_ms	*ms;

	/* status */
	enum gsm_subscriber_sim_type sim_type; /* type of sim */
	bool			sim_valid; /* sim inserted and valid */
	enum gsm_sub_sim_ustate	ustate; /* update status */
	uint8_t			imsi_attached; /* attached state */

	/* IMSI & co */
	char			imsi[OSMO_IMSI_BUF_SIZE];
	char 			msisdn[31]; /* may include access codes */
	char 			iccid[21]; /* 20 + termination */

	/* TMSI / LAI */
	uint32_t		tmsi; /* invalid tmsi: GSM_RESERVED_TMSI */
	uint32_t		ptmsi; /* invalid tmsi: GSM_RESERVED_TMSI */
	struct osmo_location_area_id lai; /* invalid lac: 0x0000 */


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
	struct osmo_plmn_id	plmn;

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
int gsm_subscr_insert(struct osmocom_ms *ms);
int gsm_subscr_remove(struct osmocom_ms *ms);

int gsm_subscr_sap_rsp_cb(struct osmocom_ms *ms, int res_code,
	uint8_t res_type, uint16_t param_len, const uint8_t *param_val);
int gsm_subscr_sim_pin(struct osmocom_ms *ms, const char *pin1, const char *pin2,
		       int8_t mode);
int gsm_subscr_write_loci(struct osmocom_ms *ms);
int gsm_subscr_generate_kc(struct osmocom_ms *ms, uint8_t key_seq, const uint8_t *rand,
			   bool no_sim);
void new_sim_ustate(struct gsm_subscriber *subscr, int state);
int gsm_subscr_del_forbidden_plmn(struct gsm_subscriber *subscr, const struct osmo_plmn_id *plmn);
int gsm_subscr_add_forbidden_plmn(struct gsm_subscriber *subscr, const struct osmo_plmn_id *plmn, uint8_t cause);
int gsm_subscr_is_forbidden_plmn(struct gsm_subscriber *subscr, const struct osmo_plmn_id *plmn);
int gsm_subscr_dump_forbidden_plmn(struct osmocom_ms *ms,
			void (*print)(void *, const char *, ...), void *priv);
void gsm_subscr_dump(struct gsm_subscriber *subscr,
			void (*print)(void *, const char *, ...), void *priv);
int gsm_subscr_get_key_seq(struct osmocom_ms *ms, struct gsm_subscriber *subscr);

#endif /* _SUBSCRIBER_H */

