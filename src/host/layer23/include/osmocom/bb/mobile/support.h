#ifndef _SUPPORT_H
#define _SUPPORT_H

#define GSM_CIPHER_A5_1		0
#define GSM_CIPHER_A5_2		1
#define GSM_CIPHER_A5_3		2
#define GSM_CIPHER_A5_4		3
#define GSM_CIPHER_A5_5		4
#define GSM_CIPHER_A5_6		5
#define GSM_CIPHER_A5_7		6
#define GSM_CIPHER_RESERVED	7

#define GSM_CAP_SDCCH		0
#define GSM_CAP_SDCCH_TCHF	1
#define GSM_CAP_SDCCH_TCHF_TCHH	2

struct gsm_support {
	struct osmocom_ms *ms;

	/* controlled early classmark sending */
	uint8_t es_ind;
	/* revision level */
	uint8_t rev_lev;
	/* support of VGCS */
	uint8_t vgcs;
	/* support of VBS */
	uint8_t vbs;
	/* support of SMS */
	uint8_t sms_ptp;
	/* screening indicator */
	uint8_t ss_ind;
	/* pseudo synchronised capability */
	uint8_t ps_cap;
	/* CM service prompt */
	uint8_t cmsp;
	/* solsa support */
	uint8_t solsa;
	/* location service support */
	uint8_t lcsva;
	/* codec support */
	uint8_t a5_1;
	uint8_t a5_2;
	uint8_t a5_3;
	uint8_t a5_4;
	uint8_t a5_5;
	uint8_t a5_6;
	uint8_t a5_7;
	/* radio support */
	uint8_t p_gsm;
	uint8_t e_gsm;
	uint8_t r_gsm;
	uint8_t dcs;
	uint8_t gsm_850;
	uint8_t pcs;
	uint8_t gsm_480;
	uint8_t gsm_450;
	uint8_t class_900;
	uint8_t class_dcs;
	uint8_t class_850;
	uint8_t class_pcs;
	uint8_t class_400;
	/* multi slot support */
	uint8_t ms_sup;
	/* ucs2 treatment */
	uint8_t ucs2_treat;
	/* support extended measurements */
	uint8_t ext_meas;
	/* support switched measurement capability */
	uint8_t meas_cap;
	uint8_t sms_val;
	uint8_t sm_val;
	/* positioning method capability */
	uint8_t loc_serv;
	uint8_t e_otd_ass;
	uint8_t e_otd_based;
	uint8_t gps_ass;
	uint8_t gps_based;
	uint8_t gps_conv;

	/* radio */
	uint8_t ch_cap; /* channel capability */
	int8_t min_rxlev_dbm;
	uint8_t scan_to;
	uint8_t sync_to;
	uint16_t dsc_max; /* maximum dl signal failure counter */

	/* codecs */
	uint8_t full_v1;
	uint8_t full_v2;
	uint8_t full_v3;
	uint8_t half_v1;
	uint8_t half_v3;

	/* EDGE / UMTS / CDMA */
	uint8_t edge_ms_sup;
	uint8_t edge_psk_sup;
	uint8_t edge_psk_uplink;
	uint8_t class_900_edge;
	uint8_t class_dcs_pcs_edge;
	uint8_t umts_fdd;
	uint8_t umts_tdd;
	uint8_t cdma_2000;
	uint8_t dtm;
	uint8_t class_dtm;
	uint8_t dtm_mac;
	uint8_t dtm_egprs;
};

struct gsm_support_scan_max {
	uint16_t	start;
	uint16_t	end;
	uint16_t	max;
	uint16_t	temp;	
};
extern struct gsm_support_scan_max gsm_sup_smax[];

void gsm_support_init(struct osmocom_ms *ms);
void gsm_support_dump(struct osmocom_ms *ms,
			void (*print)(void *, const char *, ...), void *priv);

#endif /* _SUPPORT_H */

