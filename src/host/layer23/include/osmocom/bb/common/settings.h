#ifndef _settings_h
#define _settings_h

#include <stdbool.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/gsm/protocol/gsm_23_003.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/gsm23003.h>
#include <osmocom/gsm/gsm48.h>

#include <osmocom/bb/common/sim.h>

struct osmocom_ms;
struct osmobb_apn;

#define MOB_C7_DEFLT_ANY_TIMEOUT	30

/* CC (Call Control) message handling entity */
enum mncc_handler_t {
	/* Built-in mobile's MNCC */
	MNCC_HANDLER_INTERNAL,
	/* External MNCC application via UNIX-socket */
	MNCC_HANDLER_EXTERNAL,
	/* No call support */
	MNCC_HANDLER_DUMMY,
};

/* TCH I/O handler for voice calls */
enum tch_voice_io_handler {
	/* No handler, drop frames */
	TCH_VOICE_IOH_NONE = 0,
	/* libosmo-gapk based handler */
	TCH_VOICE_IOH_GAPK,
	/* L1 PHY (e.g. Calypso DSP) */
	TCH_VOICE_IOH_L1PHY,
	/* External MNCC app (via MNCC socket) */
	TCH_VOICE_IOH_MNCC_SOCK,
	/* Return to sender */
	TCH_VOICE_IOH_LOOPBACK,
};

extern const struct value_string tch_voice_io_handler_names[];
static inline const char *tch_voice_io_handler_name(enum tch_voice_io_handler val)
{ return get_value_string(tch_voice_io_handler_names, val); }

/* TCH I/O handler for data calls */
enum tch_data_io_handler {
	/* No handler, drop frames */
	TCH_DATA_IOH_NONE = 0,
	/* UNIX socket */
	TCH_DATA_IOH_UNIX_SOCK,
	/* Return to sender */
	TCH_DATA_IOH_LOOPBACK,
};

extern const struct value_string tch_data_io_handler_names[];
static inline const char *tch_data_io_handler_name(enum tch_data_io_handler val)
{ return get_value_string(tch_data_io_handler_names, val); }

/* TCH I/O format for voice calls */
enum tch_voice_io_format {
	/* RFC3551 for FR/EFR, RFC5993 for HR, RFC4867 for AMR */
	TCH_VOICE_IOF_RTP,
	/* Texas Instruments format, used by Calypso based phones (e.g. Motorola C1xx) */
	TCH_VOICE_IOF_TI,
};

extern const struct value_string tch_voice_io_format_names[];
static inline const char *tch_voice_io_format_name(enum tch_voice_io_format val)
{ return get_value_string(tch_voice_io_format_names, val); }

/* TCH I/O format for data calls */
enum tch_data_io_format {
	/* Osmocom format, used by trxcon and virtphy */
	TCH_DATA_IOF_OSMO,
	/* Texas Instruments format, used by Calypso based phones (e.g. Motorola C1xx) */
	TCH_DATA_IOF_TI,
};

extern const struct value_string tch_data_io_format_names[];
static inline const char *tch_data_io_format_name(enum tch_data_io_format val)
{ return get_value_string(tch_data_io_format_names, val); }

struct tch_voice_settings {
	enum tch_voice_io_handler io_handler;
	enum tch_voice_io_format io_format;
	char alsa_output_dev[128];
	char alsa_input_dev[128];
};

struct tch_data_settings {
	enum tch_data_io_handler io_handler;
	enum tch_data_io_format io_format;
	char unix_socket_path[128];
};

struct test_sim_settings {
	char			imsi[OSMO_IMSI_BUF_SIZE];
	uint32_t		tmsi;
	uint8_t			ki_type;
	uint8_t			ki[16]; /* 128 bit max */
	bool			barr;
	bool			rplmn_valid;
	struct osmo_plmn_id	rplmn;
	uint16_t		lac;
	bool			imsi_attached;
	bool			always_search_hplmn;
	struct {
		bool			valid;
		uint32_t		ptmsi; /* invalid tmsi: GSM_RESERVED_TMSI */
		uint32_t		ptmsi_sig; /* P-TMSI signature, 3 bytes */
		struct gprs_ra_id	rai;
		enum gsm1111_ef_locigprs_rau_status gu_state; /* GU, TS 24.008 */
		bool			imsi_attached;
	} locigprs;
};

/* Data (CSD) call type and rate, values like in the '<speed>' part of 'AT+CBST'.
 * See 3GPP TS 27.007, section 6.7 "Select bearer service type +CBST". */
enum data_call_type_rate {
	DATA_CALL_TR_AUTO		= 0,
	DATA_CALL_TR_V21_300		= 1,
	DATA_CALL_TR_V22_1200		= 2,
	DATA_CALL_TR_V23_1200_75	= 3,
	DATA_CALL_TR_V22bis_2400	= 4,
	DATA_CALL_TR_V26ter_2400	= 5,
	DATA_CALL_TR_V32_4800		= 6,
	DATA_CALL_TR_V32_9600		= 7,
	DATA_CALL_TR_V34_9600		= 12,
	DATA_CALL_TR_V34_14400		= 14,
	DATA_CALL_TR_V34_19200		= 15,
	DATA_CALL_TR_V34_28800		= 16,
	DATA_CALL_TR_V34_33600		= 17,
	DATA_CALL_TR_V120_1200		= 34,
	DATA_CALL_TR_V120_2400		= 36,
	DATA_CALL_TR_V120_4800		= 38,
	DATA_CALL_TR_V120_9600		= 39,
	DATA_CALL_TR_V120_14400		= 43,
	DATA_CALL_TR_V120_19200		= 47,
	DATA_CALL_TR_V120_28800		= 48,
	DATA_CALL_TR_V120_38400		= 49,
	DATA_CALL_TR_V120_48000		= 50,
	DATA_CALL_TR_V120_56000		= 51,
	DATA_CALL_TR_V110_300		= 65,
	DATA_CALL_TR_V110_1200		= 66,
	DATA_CALL_TR_V110_2400		= 68,
	DATA_CALL_TR_V110_4800		= 70,
	DATA_CALL_TR_V110_9600		= 71,
	DATA_CALL_TR_V110_14400		= 75,
	DATA_CALL_TR_V110_19200		= 79,
	DATA_CALL_TR_V110_28800		= 80,
	DATA_CALL_TR_V110_38400		= 81,
	DATA_CALL_TR_V110_48000		= 82,
	DATA_CALL_TR_V110_56000		= 83,
	DATA_CALL_TR_V110_64000		= 84,
	DATA_CALL_TR_BTR_56000		= 115,
	DATA_CALL_TR_BTR_64000		= 116,
	DATA_CALL_TR_PIAFS32k_32000	= 120,
	DATA_CALL_TR_PIAFS64k_64000	= 121,
	DATA_CALL_TR_MMEDIA_28800	= 130,
	DATA_CALL_TR_MMEDIA_32000	= 131,
	DATA_CALL_TR_MMEDIA_33600	= 132,
	DATA_CALL_TR_MMEDIA_56000	= 133,
	DATA_CALL_TR_MMEDIA_64000	= 134,
};

/* Data (CSD) call parameters */
struct data_call_params {
	enum data_call_type_rate	type_rate;
	enum gsm48_bcap_transp		transp;

	/* async call parameters */
	bool				is_async;
	unsigned int			nr_stop_bits;
	unsigned int			nr_data_bits;
	enum gsm48_bcap_parity		parity;
};

struct gsm_settings {
	char			layer2_socket_path[128];
	char			sap_socket_path[128];
	char			mncc_socket_path[128];

	/* MNCC handler */
	enum mncc_handler_t	mncc_handler;

	/* TCH settings */
	struct tch_voice_settings tch_voice;
	struct tch_data_settings tch_data;

	/* IMEI */
	char			imei[GSM23003_IMEI_NUM_DIGITS + 1];
	char			imeisv[GSM23003_IMEISV_NUM_DIGITS + 1];
	char			imei_random;

	/* network search */
	int			plmn_mode; /* PLMN_MODE_* */

	/* SIM */
	int			sim_type; /* enum gsm_subscriber_sim_type,
					   * selects card on power on */
	char			emergency_imsi[OSMO_IMSI_BUF_SIZE];

	/* SMS */
	char			sms_sca[22];
	bool			store_sms;

	/* test card simulator settings */
	struct test_sim_settings test_sim;

	/* call related settings */
	uint8_t			cw; /* set if call-waiting is allowed */
	uint8_t			auto_answer;
	uint8_t			clip, clir;
	uint8_t			half, half_prefer;

	/* changing default behavior */
	uint8_t			alter_tx_power;
	uint8_t			alter_tx_power_value;
	int8_t			alter_delay;
	uint8_t			stick;
	uint16_t		stick_arfcn;
	uint8_t			skip_max_per_band;
	uint8_t			no_lupd;
	uint8_t			no_neighbour;

	/* supported by configuration */
	uint8_t			cc_dtmf;
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
	uint8_t			gsm_850;
	uint8_t			pcs;
	uint8_t			gsm_480;
	uint8_t			gsm_450;
	uint8_t			class_900;
	uint8_t			class_dcs;
	uint8_t			class_850;
	uint8_t			class_pcs;
	uint8_t			class_400;
	uint8_t			freq_map[128+38];
	uint8_t			full_v1;
	uint8_t			full_v2;
	uint8_t			full_v3;
	uint8_t			half_v1;
	uint8_t			half_v3;
	uint8_t			ch_cap; /* channel capability */
	int8_t			min_rxlev_dbm; /* min dBm to access */

	/* CSD modes */
	bool			csd_tch_f144;
	bool			csd_tch_f96;
	bool			csd_tch_f48;
	bool			csd_tch_h48;
	bool			csd_tch_f24;
	bool			csd_tch_h24;

	/* support for ASCI */
	bool			vgcs; /* support of VGCS */
	bool			vbs; /* support of VBS */

	/* radio */
	uint16_t		dsc_max;
	uint8_t			force_rekey;

	/* dialing */
	struct llist_head	abbrev;

	/* EDGE / UMTS / CDMA */
	uint8_t			edge_ms_sup;
	uint8_t			edge_psk_sup;
	uint8_t			edge_psk_uplink;
	uint8_t			class_900_edge;
	uint8_t			class_dcs_pcs_edge;
	uint8_t			umts_fdd;
	uint8_t			umts_tdd;
	uint8_t			cdma_2000;
	uint8_t			dtm;
	uint8_t			class_dtm;
	uint8_t			dtm_mac;
	uint8_t			dtm_egprs;

	/* Timeout for GSM 03.22 C7 state */
	uint8_t			any_timeout;

	/* ASCI settings */
	bool			uplink_release_local;
	bool			asci_allow_any;

	/* call parameters */
	struct {
		struct data_call_params data;
	} call_params;
};

struct gsm_settings_abbrev {
	struct llist_head	list;
	char			abbrev[4];
	char			number[32];
	char			name[32];
};

int gsm_settings_arfcn(struct osmocom_ms *ms);
int gsm_settings_init(struct osmocom_ms *ms);
int gsm_settings_exit(struct osmocom_ms *ms);
char *gsm_check_imei(const char *imei, const char *sv);
int gsm_random_imei(struct gsm_settings *set);

struct gprs_settings {
	struct llist_head apn_list;

	/* RFC1144 TCP/IP header compression */
	struct {
		int active;
		int passive;
		int s01;
	} pcomp_rfc1144;

	/* V.42vis data compression */
	struct {
		int active;
		int passive;
		int p0;
		int p1;
		int p2;
	} dcomp_v42bis;
};

int gprs_settings_init(struct osmocom_ms *ms);
int gprs_settings_fi(struct osmocom_ms *ms);
struct osmobb_apn *ms_find_apn_by_name(struct osmocom_ms *ms, const char *apn_name);
int ms_dispatch_all_apn(struct osmocom_ms *ms, uint32_t event, void *data);

extern char *layer2_socket_path;

#endif /* _settings_h */

