#ifndef _OSMOCOM_L1SAP_H
#define _OSMOCOM_L1SAP_H

#include <osmocom/core/prim.h>

/*! \brief LAPDm related primitives (L1<->L2 SAP) */
enum osmo_ph_prim {
	PRIM_PH_DATA,		/*!< \brief PH-DATA */
	PRIM_PH_RACH,		/*!< \brief PH-RANDOM_ACCESS */
	PRIM_PH_CONN,		/*!< \brief PH-CONNECT */
	PRIM_PH_EMPTY_FRAME,	/*!< \brief PH-EMPTY_FRAME */
	PRIM_PH_RTS,		/*!< \brief PH-RTS */
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
	uint8_t ra;		/*!< \brief Random Access */
	uint8_t acc_delay;	/*!< \brief Delay in bit periods */
	uint32_t fn;		/*!< \brief GSM Frame Number at time of RA */
};

/*! \brief for PH-[UNIT]DATA.{req,ind} */
struct ph_data_param {
	uint8_t link_id;	/*!< \brief Link Identifier (Like RSL) */
	uint8_t chan_nr;	/*!< \brief Channel Number (Like RSL) */
};

/*! \brief for PH-CONN.ind */
struct ph_conn_ind_param {
	uint32_t fn;		/*!< \brief GSM Frame Number */
};

/*! \brief primitive header for LAPDm PH-SAP primitives */
struct osmo_phsap_prim {
	struct osmo_prim_hdr oph; /*!< \brief generic primitive header */
	union {
		struct ph_data_param data;
		struct ph_rach_req_param rach_req;
		struct ph_rach_ind_param rach_ind;
		struct ph_conn_ind_param conn_ind;
	} u;			/*!< \brief request-specific data */
};

#endif /* _OSMOCOM_L1SAP_H */
