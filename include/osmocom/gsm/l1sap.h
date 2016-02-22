#pragma once

#include <osmocom/core/prim.h>

/*! \brief PH-SAP related primitives (L1<->L2 SAP) */
enum osmo_ph_prim {
	PRIM_PH_DATA,		/*!< \brief PH-DATA */
	PRIM_PH_RACH,		/*!< \brief PH-RANDOM_ACCESS */
	PRIM_PH_CONN,		/*!< \brief PH-CONNECT */
	PRIM_PH_EMPTY_FRAME,	/*!< \brief PH-EMPTY_FRAME */
	PRIM_PH_RTS,		/*!< \brief PH-RTS */
	PRIM_MPH_INFO,		/*!< \brief MPH-INFO */
	PRIM_TCH,		/*!< \brief TCH */
	PRIM_TCH_RTS,		/*!< \brief TCH */
};

/*! \brief PH-SAP related primitives (L1<->L2 SAP) */
enum osmo_mph_info_type {
	PRIM_INFO_TIME,		/*!< \brief Current GSM time */
	PRIM_INFO_MEAS,		/*!< \brief Measurement indication */
	PRIM_INFO_ACTIVATE,	/*!< \brief Activation of channel */
	PRIM_INFO_DEACTIVATE,	/*!< \brief Deactivation of channel */
	PRIM_INFO_MODIFY,	/*!< \brief Mode Modify of channel */
	PRIM_INFO_ACT_CIPH,	/*!< \brief Activation of ciphering */
	PRIM_INFO_DEACT_CIPH,	/*!< \brief Deactivation of ciphering */
};

/*! \brief PH-DATA presence information */
enum osmo_ph_pres_info_type {
	PRES_INFO_INVALID = 0,	/*!< \brief Data is invalid */
	PRES_INFO_HEADER  = 1,	/*!< \brief Only header is present and valid */
	PRES_INFO_FIRST   = 3,	/*!< \brief First half of data + header are valid (2nd half may be present but invalid) */
	PRES_INFO_SECOND  = 5,	/*!< \brief Second half of data + header are valid (1st halfmay be present but invalid) */
	PRES_INFO_BOTH    = 7,	/*!< \brief Both parts + header are present and valid */
	PRES_INFO_UNKNOWN
};

/*! \brief for PH-RANDOM_ACCESS.req */
struct ph_rach_req_param {
	uint8_t ra;		/*!< \brief Random Access */
	uint8_t ta;		/*!< \brief Timing Advance */
	uint8_t tx_power;	/*!< \brief Transmit Power */
	uint8_t is_combined_ccch;/*!< \brief Are we using a combined CCCH? */
	uint16_t offset;	/*!< \brief Timing Offset */
};

/*! \brief for PH-RANDOM_ACCESS.ind */
struct ph_rach_ind_param {
	uint8_t chan_nr;	/*!< \brief Channel Number (Like RSL) */
	uint8_t ra;		/*!< \brief Random Access */
	uint8_t acc_delay;	/*!< \brief Delay in bit periods */
	uint32_t fn;		/*!< \brief GSM Frame Number at time of RA */
};

/*! \brief for PH-[UNIT]DATA.{req,ind} | PH-RTS.ind */
struct ph_data_param {
	uint8_t link_id;	/*!< \brief Link Identifier (Like RSL) */
	uint8_t chan_nr;	/*!< \brief Channel Number (Like RSL) */
	uint32_t fn;		/*!< \brief GSM Frame Number */
	int8_t rssi;		/*!< \brief RSSI of receivedindication */
	enum osmo_ph_pres_info_type pdch_presence_info; /*!< \brief Info regarding presence/validity of header and data parts */
};

/*! \brief for TCH.{req,ind} | TCH-RTS.ind */
struct ph_tch_param {
	uint8_t chan_nr;	/*!< \brief Channel Number (Like RSL) */
	uint32_t fn;		/*!< \brief GSM Frame Number */
	int8_t rssi;		/*!< \brief RSSI of received indication */
};

/*! \brief for PH-CONN.ind */
struct ph_conn_ind_param {
	uint32_t fn;		/*!< \brief GSM Frame Number */
};

/*! \brief for TIME MPH-INFO.ind */
struct info_time_ind_param {
	uint32_t fn;		/*!< \brief GSM Frame Number */
};

/*! \brief for MEAS MPH-INFO.ind */
struct info_meas_ind_param {
	uint8_t chan_nr;	/*!< \brief Channel Number (Like RSL) */
	uint16_t ber10k;	/*!< \brief BER in units of 0.01% */
	int16_t ta_offs_qbits;	/*!< \brief timing advance offset (in qbits) */
	int16_t c_i_cb;		/*!< \brief C/I ratio in 0.1 dB */
	uint8_t is_sub:1;	/*!< \brief flags */
	uint8_t inv_rssi;	/*!< \brief RSSI in dBm * -1 */
};

/*! \brief for {ACTIVATE,DEACTIVATE,MODIFY} MPH-INFO.req */
struct info_act_req_param {
	uint8_t chan_nr;	/*!< \brief Channel Number (Like RSL) */
	uint8_t sacch_only;	/*!< \breif Only deactivate SACCH */
};

/*! \brief for {ACTIVATE,DEACTIVATE} MPH-INFO.cnf */
struct info_act_cnf_param {
	uint8_t chan_nr;	/*!< \brief Channel Number (Like RSL) */
	uint8_t cause;		/*!< \brief RSL cause in case of nack */
};

/*! \brief for {ACTIVATE,DEACTIVATE} MPH-INFO.{req,cnf} */
struct info_ciph_req_param {
	uint8_t chan_nr;	/*!< \brief Channel Number (Like RSL) */
	uint8_t downlink;	/*!< \brief Apply to downlink */
	uint8_t uplink;		/*!< \brief Apply to uplink */
};

/*! \brief for MPH-INFO.ind */
struct mph_info_param {
	enum osmo_mph_info_type type; /*!< \brief Info message type */
	union {
		struct info_time_ind_param time_ind;
		struct info_meas_ind_param meas_ind;
		struct info_act_req_param act_req;
		struct info_act_cnf_param act_cnf;
		struct info_ciph_req_param ciph_req;
	} u;
};

/*! \brief primitive header for PH-SAP primitives */
struct osmo_phsap_prim {
	struct osmo_prim_hdr oph; /*!< \brief generic primitive header */
	union {
		struct ph_data_param data;
		struct ph_tch_param tch;
		struct ph_rach_req_param rach_req;
		struct ph_rach_ind_param rach_ind;
		struct ph_conn_ind_param conn_ind;
		struct mph_info_param info;
	} u;			/*!< \brief request-specific data */
};
