#pragma once

/* GSM Network Management messages on the A-bis interface 
 * 3GPP TS 12.21 version 8.0.0 Release 1999 / ETSI TS 100 623 V8.0.0 */

/* (C) 2008-2009 by Harald Welte <laforge@gnumonks.org>
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*! \addtogroup oml
 *  @{
 */

/*! \file gsm_12_21.h */

#include <stdint.h>
#include <osmocom/gsm/tlv.h>

/*! \brief generic header in front of every OML message according to TS 08.59 */
struct abis_om_hdr {
	/*! \brief Message Discriminator \ref abis_oml_mdisc */
	uint8_t	mdisc;
	/*! \brief Placement (like \ref ABIS_OM_PLACEMENT_ONLY) */
	uint8_t	placement;
	/*! \brief Sequence Number (if \ref ABIS_OM_PLACEMENT_MIDDLE) */
	uint8_t	sequence;
	/*! \brief Length in octets */
	uint8_t	length;
	/*! \brief actual payload data */
	uint8_t	data[0];
} __attribute__ ((packed));

/*! \brief Message Discriminator for Formatted Object Messages */
#define ABIS_OM_MDISC_FOM		0x80
/*! \brief Message Discriminator for Man Machine Interface */
#define ABIS_OM_MDISC_MMI		0x40
/*! \brief Message Discriminator for TRAU management */
#define ABIS_OM_MDISC_TRAU		0x20
/*! \brief Message Discriminator for Manufacturer Specific Messages */
#define ABIS_OM_MDISC_MANUF		0x10

/*! \brief Entire OML message is in the L2 frame */
#define ABIS_OM_PLACEMENT_ONLY		0x80
/*! \brief First fragment of OML message is in this L2 frame */
#define ABIS_OM_PLACEMENT_FIRST 	0x40
/*! \brief Middle fragment of OML message is in this L2 frame */
#define ABIS_OM_PLACEMENT_MIDDLE	0x20
/*! \brief Last fragment of OML message is in this L2 frame */
#define ABIS_OM_PLACEMENT_LAST		0x10

/*! \brief OML Object Instance */
struct abis_om_obj_inst {
	uint8_t	bts_nr;	/*!< \brief BTS Number */
	uint8_t	trx_nr;	/*!< \brief TRX Number */
	uint8_t	ts_nr;	/*!< \brief Timeslot Number */
} __attribute__ ((packed));

/*! \brief OML Object Instance */
struct abis_om_fom_hdr {
	uint8_t	msg_type;	/*!< \brief Message Type (\ref abis_nm_msgtype) */
	uint8_t	obj_class;	/*!< \brief Object Class (\ref abis_nm_obj_class) */
	struct abis_om_obj_inst	obj_inst; /*!< \brief Object Instance */
	uint8_t	data[0];	/*!< \brief Data */
} __attribute__ ((packed));

/*! \brief Size of the OML FOM header in octets */
#define ABIS_OM_FOM_HDR_SIZE	(sizeof(struct abis_om_hdr) + sizeof(struct abis_om_fom_hdr))

/*! \brief OML Message Type (Section 9.1) */
enum abis_nm_msgtype {
	/* SW Download Management Messages */
	NM_MT_LOAD_INIT			= 0x01,
	NM_MT_LOAD_INIT_ACK,
	NM_MT_LOAD_INIT_NACK,
	NM_MT_LOAD_SEG,
	NM_MT_LOAD_SEG_ACK,
	NM_MT_LOAD_ABORT,
	NM_MT_LOAD_END,
	NM_MT_LOAD_END_ACK,
	NM_MT_LOAD_END_NACK,
	NM_MT_SW_ACT_REQ,		/* BTS->BSC */
	NM_MT_SW_ACT_REQ_ACK,
	NM_MT_SW_ACT_REQ_NACK,
	NM_MT_ACTIVATE_SW,		/* BSC->BTS */
	NM_MT_ACTIVATE_SW_ACK,
	NM_MT_ACTIVATE_SW_NACK,
	NM_MT_SW_ACTIVATED_REP,		/* 0x10 */
	/* A-bis Interface Management Messages */
	NM_MT_ESTABLISH_TEI		= 0x21,
	NM_MT_ESTABLISH_TEI_ACK,
	NM_MT_ESTABLISH_TEI_NACK,
	NM_MT_CONN_TERR_SIGN,
	NM_MT_CONN_TERR_SIGN_ACK,
	NM_MT_CONN_TERR_SIGN_NACK,
	NM_MT_DISC_TERR_SIGN,
	NM_MT_DISC_TERR_SIGN_ACK,
	NM_MT_DISC_TERR_SIGN_NACK,
	NM_MT_CONN_TERR_TRAF,
	NM_MT_CONN_TERR_TRAF_ACK,
	NM_MT_CONN_TERR_TRAF_NACK,
	NM_MT_DISC_TERR_TRAF,
	NM_MT_DISC_TERR_TRAF_ACK,
	NM_MT_DISC_TERR_TRAF_NACK,
	/* Transmission Management Messages */
	NM_MT_CONN_MDROP_LINK		= 0x31,
	NM_MT_CONN_MDROP_LINK_ACK,
	NM_MT_CONN_MDROP_LINK_NACK,
	NM_MT_DISC_MDROP_LINK,
	NM_MT_DISC_MDROP_LINK_ACK,
	NM_MT_DISC_MDROP_LINK_NACK,
	/* Air Interface Management Messages */
	NM_MT_SET_BTS_ATTR		= 0x41,
	NM_MT_SET_BTS_ATTR_ACK,
	NM_MT_SET_BTS_ATTR_NACK,
	NM_MT_SET_RADIO_ATTR,
	NM_MT_SET_RADIO_ATTR_ACK,
	NM_MT_SET_RADIO_ATTR_NACK,
	NM_MT_SET_CHAN_ATTR,
	NM_MT_SET_CHAN_ATTR_ACK,
	NM_MT_SET_CHAN_ATTR_NACK,
	/* Test Management Messages */
	NM_MT_PERF_TEST			= 0x51,
	NM_MT_PERF_TEST_ACK,
	NM_MT_PERF_TEST_NACK,
	NM_MT_TEST_REP,
	NM_MT_SEND_TEST_REP,
	NM_MT_SEND_TEST_REP_ACK,
	NM_MT_SEND_TEST_REP_NACK,
	NM_MT_STOP_TEST,
	NM_MT_STOP_TEST_ACK,
	NM_MT_STOP_TEST_NACK,
	/* State Management and Event Report Messages */
	NM_MT_STATECHG_EVENT_REP	= 0x61,
	NM_MT_FAILURE_EVENT_REP,
	NM_MT_STOP_EVENT_REP,
	NM_MT_STOP_EVENT_REP_ACK,
	NM_MT_STOP_EVENT_REP_NACK,
	NM_MT_REST_EVENT_REP,
	NM_MT_REST_EVENT_REP_ACK,
	NM_MT_REST_EVENT_REP_NACK,
	NM_MT_CHG_ADM_STATE,
	NM_MT_CHG_ADM_STATE_ACK,
	NM_MT_CHG_ADM_STATE_NACK,
	NM_MT_CHG_ADM_STATE_REQ,
	NM_MT_CHG_ADM_STATE_REQ_ACK,
	NM_MT_CHG_ADM_STATE_REQ_NACK,
	NM_MT_REP_OUTST_ALARMS		= 0x93,
	NM_MT_REP_OUTST_ALARMS_ACK,
	NM_MT_REP_OUTST_ALARMS_NACK,
	/* Equipment Management Messages */
	NM_MT_CHANGEOVER		= 0x71,
	NM_MT_CHANGEOVER_ACK,
	NM_MT_CHANGEOVER_NACK,
	NM_MT_OPSTART,
	NM_MT_OPSTART_ACK,
	NM_MT_OPSTART_NACK,
	NM_MT_REINIT,
	NM_MT_REINIT_ACK,
	NM_MT_REINIT_NACK,
	NM_MT_SET_SITE_OUT,		/* BS11: get alarm ?!? */
	NM_MT_SET_SITE_OUT_ACK,
	NM_MT_SET_SITE_OUT_NACK,
	NM_MT_CHG_HW_CONF		= 0x90,
	NM_MT_CHG_HW_CONF_ACK,
	NM_MT_CHG_HW_CONF_NACK,
	/* Measurement Management Messages */
	NM_MT_MEAS_RES_REQ		= 0x8a,
	NM_MT_MEAS_RES_RESP,
	NM_MT_STOP_MEAS,
	NM_MT_START_MEAS,
	/* Other Messages */
	NM_MT_GET_ATTR			= 0x81,
	NM_MT_GET_ATTR_RESP,
	NM_MT_GET_ATTR_NACK,
	NM_MT_SET_ALARM_THRES,
	NM_MT_SET_ALARM_THRES_ACK,
	NM_MT_SET_ALARM_THRES_NACK,
};

/*! \brief Siemens specific OML Message Types */
enum abis_nm_msgtype_bs11 {
	NM_MT_BS11_RESET_RESOURCE	= 0x74,

	NM_MT_BS11_BEGIN_DB_TX		= 0xa3,
	NM_MT_BS11_BEGIN_DB_TX_ACK,
	NM_MT_BS11_BEGIN_DB_TX_NACK,
	NM_MT_BS11_END_DB_TX		= 0xa6,
	NM_MT_BS11_END_DB_TX_ACK,
	NM_MT_BS11_END_DB_TX_NACK,
	NM_MT_BS11_CREATE_OBJ		= 0xa9,
	NM_MT_BS11_CREATE_OBJ_ACK,
	NM_MT_BS11_CREATE_OBJ_NACK,
	NM_MT_BS11_DELETE_OBJ		= 0xac,
	NM_MT_BS11_DELETE_OBJ_ACK,
	NM_MT_BS11_DELETE_OBJ_NACK,

	NM_MT_BS11_SET_ATTR		= 0xd0,
	NM_MT_BS11_SET_ATTR_ACK,
	NM_MT_BS11_SET_ATTR_NACK,
	NM_MT_BS11_LMT_SESSION		= 0xdc,

	NM_MT_BS11_GET_STATE		= 0xe3,
	NM_MT_BS11_GET_STATE_ACK,
	NM_MT_BS11_LMT_LOGON		= 0xe5,
	NM_MT_BS11_LMT_LOGON_ACK,
	NM_MT_BS11_RESTART		= 0xe7,
	NM_MT_BS11_RESTART_ACK,
	NM_MT_BS11_DISCONNECT		= 0xe9,
	NM_MT_BS11_DISCONNECT_ACK,
	NM_MT_BS11_LMT_LOGOFF		= 0xec,
	NM_MT_BS11_LMT_LOGOFF_ACK,
	NM_MT_BS11_RECONNECT		= 0xf1,
	NM_MT_BS11_RECONNECT_ACK,
};

/*! \brief ip.access specific OML Message Types */
enum abis_nm_msgtype_ipacc {
	NM_MT_IPACC_RESTART		= 0x87,
	NM_MT_IPACC_RESTART_ACK,
	NM_MT_IPACC_RESTART_NACK,
	NM_MT_IPACC_RSL_CONNECT		= 0xe0,
	NM_MT_IPACC_RSL_CONNECT_ACK,
	NM_MT_IPACC_RSL_CONNECT_NACK,
	NM_MT_IPACC_RSL_DISCONNECT	= 0xe3,
	NM_MT_IPACC_RSL_DISCONNECT_ACK,
	NM_MT_IPACC_RSL_DISCONNECT_NACK,
	NM_MT_IPACC_CONN_TRAF		= 0xe6,
	NM_MT_IPACC_CONN_TRAF_ACK,
	NM_MT_IPACC_CONN_TRAF_NACK,
	NM_MT_IPACC_DEF_BOOT_SW		= 0xec,
	NM_MT_IPACC_DEF_BOOT_SW_ACK,
	MN_MT_IPACC_DEF_BOOT_SW_NACK,
	NM_MT_IPACC_SET_NVATTR		= 0xef,
	NM_MT_IPACC_SET_NVATTR_ACK,
	NM_MT_IPACC_SET_NVATTR_NACK,
	NM_MT_IPACC_GET_NVATTR		= 0xf2,
	NM_MT_IPACC_GET_NVATTR_ACK,
	NM_MT_IPACC_GET_NVATTR_NACK,
	NM_MT_IPACC_SET_ATTR		= 0xf5,
	NM_MT_IPACC_SET_ATTR_ACK,
	NM_MT_IPACC_SET_ATTR_NACK,
};

enum abis_nm_bs11_cell_alloc {
	NM_BS11_CANR_GSM	= 0x00,
	NM_BS11_CANR_DCS1800	= 0x01,
};

/*! \brief OML Object Class (Section 9.2) */
enum abis_nm_obj_class {
	NM_OC_SITE_MANAGER		= 0x00,
	NM_OC_BTS,
	NM_OC_RADIO_CARRIER,
	NM_OC_CHANNEL,
	NM_OC_BASEB_TRANSC,
	/* RFU: 05-FE */

	NM_OC_IPAC_E1_TRUNK		= 0x0e,
	NM_OC_IPAC_E1_PORT		= 0x0f,
	NM_OC_IPAC_E1_CHAN		= 0x10,
	NM_OC_IPAC_CLK_MODULE		= 0x22,

	NM_OC_BS11_ADJC			= 0xa0,
	NM_OC_BS11_HANDOVER		= 0xa1,
	NM_OC_BS11_PWR_CTRL		= 0xa2,
	NM_OC_BS11_BTSE			= 0xa3,		/* LMT? */
	NM_OC_BS11_RACK			= 0xa4,
	NM_OC_BS11			= 0xa5,		/* 01: ALCO */
	NM_OC_BS11_TEST			= 0xa6,
	NM_OC_BS11_ENVABTSE		= 0xa8,
	NM_OC_BS11_BPORT		= 0xa9,

	NM_OC_GPRS_NSE			= 0xf0,
	NM_OC_GPRS_CELL			= 0xf1,
	NM_OC_GPRS_NSVC			= 0xf2,

	NM_OC_NULL			= 0xff,
};

/*! \brief OML Attributes / IEs (Section 9.4) */
enum abis_nm_attr {
	NM_ATT_ABIS_CHANNEL	= 0x01,
	NM_ATT_ADD_INFO,
	NM_ATT_ADD_TEXT,
	NM_ATT_ADM_STATE,
	NM_ATT_ARFCN_LIST,
	NM_ATT_AUTON_REPORT,
	NM_ATT_AVAIL_STATUS,
	NM_ATT_BCCH_ARFCN,
	NM_ATT_BSIC,
	NM_ATT_BTS_AIR_TIMER,
	NM_ATT_CCCH_L_I_P,
	NM_ATT_CCCH_L_T,
	NM_ATT_CHAN_COMB,
	NM_ATT_CONN_FAIL_CRIT,
	NM_ATT_DEST,
	/* res */
	NM_ATT_EVENT_TYPE	= 0x11, /* BS11: file data ?!? */
	NM_ATT_FILE_ID,
	NM_ATT_FILE_VERSION,
	NM_ATT_GSM_TIME,
	NM_ATT_HSN,
	NM_ATT_HW_CONFIG,
	NM_ATT_HW_DESC,
	NM_ATT_INTAVE_PARAM,
	NM_ATT_INTERF_BOUND,
	NM_ATT_LIST_REQ_ATTR,
	NM_ATT_MAIO,
	NM_ATT_MANUF_STATE,
	NM_ATT_MANUF_THRESH,
	NM_ATT_MANUF_ID,
	NM_ATT_MAX_TA,
	NM_ATT_MDROP_LINK,	/* 0x20 */
	NM_ATT_MDROP_NEXT,
	NM_ATT_NACK_CAUSES,
	NM_ATT_NY1,
	NM_ATT_OPER_STATE,
	NM_ATT_OVERL_PERIOD,
	NM_ATT_PHYS_CONF,
	NM_ATT_POWER_CLASS,
	NM_ATT_POWER_THRESH,
	NM_ATT_PROB_CAUSE,
	NM_ATT_RACH_B_THRESH,
	NM_ATT_LDAVG_SLOTS,
	NM_ATT_RAD_SUBC,
	NM_ATT_RF_MAXPOWR_R,
	NM_ATT_SITE_INPUTS,
	NM_ATT_SITE_OUTPUTS,
	NM_ATT_SOURCE,		/* 0x30 */
	NM_ATT_SPEC_PROB,
	NM_ATT_START_TIME,
	NM_ATT_T200,
	NM_ATT_TEI,
	NM_ATT_TEST_DUR,
	NM_ATT_TEST_NO,
	NM_ATT_TEST_REPORT,
	NM_ATT_VSWR_THRESH,
	NM_ATT_WINDOW_SIZE,
	/* Res  */
	NM_ATT_BS11_RSSI_OFFS	= 0x3d,
	NM_ATT_BS11_TXPWR	= 0x3e,
	NM_ATT_BS11_DIVERSITY	= 0x3f,
	/* Res  */
	NM_ATT_TSC		= 0x40,
	NM_ATT_SW_CONFIG,
	NM_ATT_SW_DESCR,
	NM_ATT_SEVERITY,
	NM_ATT_GET_ARI,
	NM_ATT_HW_CONF_CHG,
	NM_ATT_OUTST_ALARM,
	NM_ATT_FILE_DATA,
	NM_ATT_MEAS_RES,
	NM_ATT_MEAS_TYPE,

	NM_ATT_BS11_ESN_FW_CODE_NO	= 0x4c,
	NM_ATT_BS11_ESN_HW_CODE_NO	= 0x4f,

	NM_ATT_BS11_ESN_PCB_SERIAL	= 0x55,
	NM_ATT_BS11_EXCESSIVE_DISTANCE	= 0x58,

	NM_ATT_BS11_ALL_TEST_CATG	= 0x60,
	NM_ATT_BS11_BTSLS_HOPPING,
	NM_ATT_BS11_CELL_ALLOC_NR,
	NM_ATT_BS11_CELL_GLOBAL_ID,
	NM_ATT_BS11_ENA_INTERF_CLASS	= 0x66,
	NM_ATT_BS11_ENA_INT_INTEC_HANDO	= 0x67,
	NM_ATT_BS11_ENA_INT_INTRC_HANDO	= 0x68,
	NM_ATT_BS11_ENA_MS_PWR_CTRL	= 0x69,
	NM_ATT_BS11_ENA_PWR_BDGT_HO	= 0x6a,
	NM_ATT_BS11_ENA_PWR_CTRL_RLFW	= 0x6b,
	NM_ATT_BS11_ENA_RXLEV_HO	= 0x6c,
	NM_ATT_BS11_ENA_RXQUAL_HO	= 0x6d,
	NM_ATT_BS11_FACCH_QUAL		= 0x6e,

	NM_ATT_IPACC_DST_IP		= 0x80,
	NM_ATT_IPACC_DST_IP_PORT	= 0x81,
	NM_ATT_IPACC_SSRC		= 0x82,
	NM_ATT_IPACC_RTP_PAYLD_TYPE	= 0x83,
	NM_ATT_IPACC_BASEB_ID		= 0x84,
	NM_ATT_IPACC_STREAM_ID		= 0x85,
	NM_ATT_IPACC_NV_FLAGS		= 0x86,
	NM_ATT_IPACC_FREQ_CTRL		= 0x87,
	NM_ATT_IPACC_PRIM_OML_CFG	= 0x88,
	NM_ATT_IPACC_SEC_OML_CFG	= 0x89,
	NM_ATT_IPACC_IP_IF_CFG		= 0x8a,		/* IP interface */
	NM_ATT_IPACC_IP_GW_CFG		= 0x8b,		/* IP gateway */
	NM_ATT_IPACC_IN_SERV_TIME	= 0x8c,
	NM_ATT_IPACC_TRX_BTS_ASS	= 0x8d,
	NM_ATT_IPACC_LOCATION		= 0x8e,		/* string describing location */
	NM_ATT_IPACC_PAGING_CFG		= 0x8f,
	NM_ATT_IPACC_FILE_DATA		= 0x90,
	NM_ATT_IPACC_UNIT_ID		= 0x91,		/* Site/BTS/TRX */
	NM_ATT_IPACC_PARENT_UNIT_ID	= 0x92,
	NM_ATT_IPACC_UNIT_NAME		= 0x93,		/* default: nbts-<mac-as-string> */
	NM_ATT_IPACC_SNMP_CFG		= 0x94,
	NM_ATT_IPACC_PRIM_OML_CFG_LIST	= 0x95,
	NM_ATT_IPACC_PRIM_OML_FB_TOUT	= 0x96,
	NM_ATT_IPACC_CUR_SW_CFG		= 0x97,
	NM_ATT_IPACC_TIMING_BUS		= 0x98,
	NM_ATT_IPACC_CGI		= 0x99,
	NM_ATT_IPACC_RAC		= 0x9a,
	NM_ATT_IPACC_OBJ_VERSION	= 0x9b,
	NM_ATT_IPACC_GPRS_PAGING_CFG	= 0x9c,
	NM_ATT_IPACC_NSEI		= 0x9d,
	NM_ATT_IPACC_BVCI		= 0x9e,
	NM_ATT_IPACC_NSVCI		= 0x9f,
	NM_ATT_IPACC_NS_CFG		= 0xa0,
	NM_ATT_IPACC_BSSGP_CFG		= 0xa1,
	NM_ATT_IPACC_NS_LINK_CFG	= 0xa2,
	NM_ATT_IPACC_RLC_CFG		= 0xa3,	
	NM_ATT_IPACC_ALM_THRESH_LIST	= 0xa4,
	NM_ATT_IPACC_MONIT_VAL_LIST	= 0xa5,
	NM_ATT_IPACC_TIB_CONTROL	= 0xa6,
	NM_ATT_IPACC_SUPP_FEATURES	= 0xa7,
	NM_ATT_IPACC_CODING_SCHEMES	= 0xa8,
	NM_ATT_IPACC_RLC_CFG_2		= 0xa9,
	NM_ATT_IPACC_HEARTB_TOUT	= 0xaa,
	NM_ATT_IPACC_UPTIME		= 0xab,
	NM_ATT_IPACC_RLC_CFG_3		= 0xac,
	NM_ATT_IPACC_SSL_CFG		= 0xad,
	NM_ATT_IPACC_SEC_POSSIBLE	= 0xae,
	NM_ATT_IPACC_IML_SSL_STATE	= 0xaf,
	NM_ATT_IPACC_REVOC_DATE		= 0xb0,


	NM_ATT_BS11_RF_RES_IND_PER	= 0x8f,
	
	NM_ATT_BS11_RX_LEV_MIN_CELL	= 0x90,
	NM_ATT_BS11_ABIS_EXT_TIME	= 0x91,
	NM_ATT_BS11_TIMER_HO_REQUEST	= 0x92,
	NM_ATT_BS11_TIMER_NCELL		= 0x93,
	NM_ATT_BS11_TSYNC		= 0x94,
	NM_ATT_BS11_TTRAU		= 0x95,
	NM_ATT_BS11_EMRG_CFG_MEMBER	= 0x9b,
	NM_ATT_BS11_TRX_AREA		= 0x9f,

	NM_ATT_BS11_BCCH_RECONF		= 0xd7,
	NM_ATT_BS11_BIT_ERR_THESH	= 0xa0,
	NM_ATT_BS11_BOOT_SW_VERS	= 0xa1,
	NM_ATT_BS11_CCLK_ACCURACY	= 0xa3,
	NM_ATT_BS11_CCLK_TYPE		= 0xa4,
	NM_ATT_BS11_INP_IMPEDANCE	= 0xaa,
	NM_ATT_BS11_L1_PROT_TYPE	= 0xab,
	NM_ATT_BS11_LINE_CFG		= 0xac,
	NM_ATT_BS11_LI_PORT_1		= 0xad,
	NM_ATT_BS11_LI_PORT_2		= 0xae,

	NM_ATT_BS11_L1_REM_ALM_TYPE	= 0xb0,
	NM_ATT_BS11_SW_LOAD_INTENDED	= 0xbb,
	NM_ATT_BS11_SW_LOAD_SAFETY	= 0xbc,
	NM_ATT_BS11_SW_LOAD_STORED	= 0xbd,

	NM_ATT_BS11_VENDOR_NAME		= 0xc1,
	NM_ATT_BS11_HOPPING_MODE	= 0xc5,
	NM_ATT_BS11_LMT_LOGON_SESSION	= 0xc6,
	NM_ATT_BS11_LMT_LOGIN_TIME	= 0xc7,
	NM_ATT_BS11_LMT_USER_ACC_LEV	= 0xc8,
	NM_ATT_BS11_LMT_USER_NAME	= 0xc9,

	NM_ATT_BS11_L1_CONTROL_TS	= 0xd8,
	NM_ATT_BS11_RADIO_MEAS_GRAN	= 0xdc,	/* in SACCH multiframes */
	NM_ATT_BS11_RADIO_MEAS_REP	= 0xdd,

	NM_ATT_BS11_SH_LAPD_INT_TIMER	= 0xe8,

	NM_ATT_BS11_BTS_STATE		= 0xf0,
	NM_ATT_BS11_E1_STATE		= 0xf1,
	NM_ATT_BS11_PLL			= 0xf2,
	NM_ATT_BS11_RX_OFFSET		= 0xf3,
	NM_ATT_BS11_ANT_TYPE		= 0xf4,
	NM_ATT_BS11_PLL_MODE		= 0xfc,
	NM_ATT_BS11_PASSWORD		= 0xfd,

	/* osmocom (osmo-bts) specific attributes, used in combination
	 * with the "org.osmocom" manufacturer identification */
	NM_ATT_OSMO_REDUCEPOWER		= 0xfe,	/* TLV_TYPE_TV */
};
#define NM_ATT_BS11_FILE_DATA	NM_ATT_EVENT_TYPE

/*! \brief OML Administrative State (Section 9.4.4) */
enum abis_nm_adm_state {
	NM_STATE_LOCKED		= 0x01,
	NM_STATE_UNLOCKED	= 0x02,
	NM_STATE_SHUTDOWN	= 0x03,
	NM_STATE_NULL		= 0xff,
};

/*! \brief OML Availability State (Section 9.4.7) */
enum abis_nm_avail_state {
	NM_AVSTATE_IN_TEST	= 1,
	NM_AVSTATE_POWER_OFF	= 2,
	NM_AVSTATE_OFF_LINE	= 3,
	NM_AVSTATE_DEPENDENCY	= 5,
	NM_AVSTATE_DEGRADED	= 6,
	NM_AVSTATE_NOT_INSTALLED= 7,
	NM_AVSTATE_OK		= 0xff,
};

/*! \brief OML Operational State */
enum abis_nm_op_state {
	NM_OPSTATE_DISABLED	= 1,
	NM_OPSTATE_ENABLED	= 2,
	NM_OPSTATE_NULL		= 0xff,
};

/* \brief Channel Combination (Section 9.4.13) */
enum abis_nm_chan_comb {
	NM_CHANC_TCHFull	= 0x00,	/* TCH/F + TCH/H + SACCH/TF */
	NM_CHANC_TCHHalf	= 0x01, /* TCH/H(0,1) + FACCH/H(0,1) +
					   SACCH/TH(0,1) */
	NM_CHANC_TCHHalf2	= 0x02, /* TCH/H(0) + FACCH/H(0) + SACCH/TH(0) +
					   TCH/H(1) */
	NM_CHANC_SDCCH		= 0x03,	/* SDCCH/8 + SACCH/8 */
	NM_CHANC_mainBCCH	= 0x04,	/* FCCH + SCH + BCCH + CCCH */
	NM_CHANC_BCCHComb	= 0x05,	/* FCCH + SCH + BCCH + CCCH + SDCCH/4 +
					   SACCH/C4 */
	NM_CHANC_BCCH		= 0x06,	/* BCCH + CCCH */
	NM_CHANC_BCCH_CBCH	= 0x07,	/* CHANC_BCCHComb + CBCH */
	NM_CHANC_SDCCH_CBCH	= 0x08,	/* CHANC_SDCCH8 + CBCH */
	/* ip.access */
	NM_CHANC_IPAC_bPDCH	= 0x0b,	/* PBCCH + PCCCH + PDTCH/F + PACCH/F +
					   PTCCH/F */
	NM_CHANC_IPAC_cPDCH	= 0x0c, /* PBCCH + PDTCH/F + PACCH/F + PTCCH/F */
	NM_CHANC_IPAC_PDCH	= 0x0d,	/* PDTCH/F + PACCH/F + PTCCH/F */
	NM_CHANC_IPAC_TCHFull_PDCH = 0x80,
	NM_CHANC_IPAC_TCHFull_TCHHalf = 0x81,
};

/*! \brief Event Type (Section 9.4.16) */
enum abis_nm_event_type {
	NM_EVT_COMM_FAIL	= 0x00,
	NM_EVT_QOS_FAIL		= 0x01,
	NM_EVT_PROC_FAIL	= 0x02,
	NM_EVT_EQUIP_FAIL	= 0x03,
	NM_EVT_ENV_FAIL		= 0x04,
};

/*! \brief Perceived Severity (Section: 9.4.63) */
enum abis_nm_severity {
	NM_SEVER_CEASED		= 0x00,
	NM_SEVER_CRITICAL	= 0x01,
	NM_SEVER_MAJOR		= 0x02,
	NM_SEVER_MINOR		= 0x03,
	NM_SEVER_WARNING	= 0x04,
	NM_SEVER_INDETERMINATE	= 0x05,
};

/*! \brief Probable Cause Type (Section 9.4.43) */
enum abis_nm_pcause_type {
	NM_PCAUSE_T_X721	= 0x01,
	NM_PCAUSE_T_GSM		= 0x02,
	NM_PCAUSE_T_MANUF	= 0x03,
};

/*! \brief NACK causes (Section 9.4.36) */
enum abis_nm_nack_cause {
	/* General Nack Causes */
	NM_NACK_INCORR_STRUCT		= 0x01,
	NM_NACK_MSGTYPE_INVAL		= 0x02,
	NM_NACK_OBJCLASS_INVAL		= 0x05,
	NM_NACK_OBJCLASS_NOTSUPP	= 0x06,
	NM_NACK_BTSNR_UNKN		= 0x07,
	NM_NACK_TRXNR_UNKN		= 0x08,
	NM_NACK_OBJINST_UNKN		= 0x09,
	NM_NACK_ATTRID_INVAL		= 0x0c,
	NM_NACK_ATTRID_NOTSUPP		= 0x0d,
	NM_NACK_PARAM_RANGE		= 0x0e,
	NM_NACK_ATTRLIST_INCONSISTENT	= 0x0f,
	NM_NACK_SPEC_IMPL_NOTSUPP	= 0x10,
	NM_NACK_CANT_PERFORM		= 0x11,
	/* Specific Nack Causes */
	NM_NACK_RES_NOTIMPL		= 0x19,
	NM_NACK_RES_NOTAVAIL		= 0x1a,
	NM_NACK_FREQ_NOTAVAIL		= 0x1b,
	NM_NACK_TEST_NOTSUPP		= 0x1c,
	NM_NACK_CAPACITY_RESTR		= 0x1d,
	NM_NACK_PHYSCFG_NOTPERFORM	= 0x1e,
	NM_NACK_TEST_NOTINIT		= 0x1f,
	NM_NACK_PHYSCFG_NOTRESTORE	= 0x20,
	NM_NACK_TEST_NOSUCH		= 0x21,
	NM_NACK_TEST_NOSTOP		= 0x22,
	NM_NACK_MSGINCONSIST_PHYSCFG	= 0x23,
	NM_NACK_FILE_INCOMPLETE		= 0x25,
	NM_NACK_FILE_NOTAVAIL		= 0x26,
	NM_NACK_FILE_NOTACTIVATE	= 0x27,
	NM_NACK_REQ_NOT_GRANT		= 0x28,
	NM_NACK_WAIT			= 0x29,
	NM_NACK_NOTH_REPORT_EXIST	= 0x2a,
	NM_NACK_MEAS_NOTSUPP		= 0x2b,
	NM_NACK_MEAS_NOTSTART		= 0x2c,
};

/*! \brief Abis OML Channel (Section 9.4.1) */
struct abis_nm_channel {
	uint8_t	attrib;
	uint8_t	bts_port;	/*!< \brief BTS port number */
	uint8_t	timeslot;	/*!< \brief E1 timeslot */
	uint8_t	subslot;	/*!< \brief E1 sub-slot */
} __attribute__ ((packed));

/*! \brief Siemens BS-11 specific objects in the SienemsHW (0xA5) object class */
enum abis_bs11_objtype {
	BS11_OBJ_ALCO		= 0x01,
	BS11_OBJ_BBSIG		= 0x02,	/* obj_class: 0,1 */
	BS11_OBJ_TRX1		= 0x03,	/* only DEACTIVATE TRX1 */
	BS11_OBJ_CCLK		= 0x04,
	BS11_OBJ_GPSU		= 0x06,
	BS11_OBJ_LI		= 0x07,
	BS11_OBJ_PA		= 0x09,	/* obj_class: 0, 1*/
};

/*! \brief Siemens BS11 TRX power */
enum abis_bs11_trx_power {
	BS11_TRX_POWER_GSM_2W	= 0x06,
	BS11_TRX_POWER_GSM_250mW= 0x07,
	BS11_TRX_POWER_GSM_80mW	= 0x08,
	BS11_TRX_POWER_GSM_30mW	= 0x09,
	BS11_TRX_POWER_DCS_3W	= 0x0a,
	BS11_TRX_POWER_DCS_1W6	= 0x0b,
	BS11_TRX_POWER_DCS_500mW= 0x0c,
	BS11_TRX_POWER_DCS_160mW= 0x0d,
};

/*! \brief Siemens BS11 PLL mode */
enum abis_bs11_li_pll_mode {
	BS11_LI_PLL_LOCKED	= 2,
	BS11_LI_PLL_STANDALONE	= 3,
};

/*! \brief Siemens BS11 E1 line configuration */
enum abis_bs11_line_cfg {
	BS11_LINE_CFG_STAR	= 0x00,
	BS11_LINE_CFG_MULTIDROP	= 0x01,
	BS11_LINE_CFG_LOOP	= 0x02,
};

/*! \brief Siemens BS11 boot phase */
enum abis_bs11_phase {
	BS11_STATE_SOFTWARE_RQD		= 0x01,
	BS11_STATE_LOAD_SMU_INTENDED	= 0x11,
	BS11_STATE_LOAD_SMU_SAFETY	= 0x21,
	BS11_STATE_LOAD_FAILED		= 0x31,
	BS11_STATE_LOAD_DIAGNOSTIC	= 0x41,
	BS11_STATE_WARM_UP		= 0x51,
	BS11_STATE_WARM_UP_2		= 0x52,
	BS11_STATE_WAIT_MIN_CFG		= 0x62,
	BS11_STATE_MAINTENANCE		= 0x72,
	BS11_STATE_LOAD_MBCCU		= 0x92,
	BS11_STATE_WAIT_MIN_CFG_2	= 0xA2,
	BS11_STATE_NORMAL		= 0x03,
	BS11_STATE_ABIS_LOAD		= 0x13,
};

/*! \brief ip.access test number */
enum abis_nm_ipacc_test_no {
	NM_IPACC_TESTNO_RLOOP_ANT	= 0x01,
	NM_IPACC_TESTNO_RLOOP_XCVR	= 0x02,
	NM_IPACC_TESTNO_FUNC_OBJ	= 0x03,
	NM_IPACC_TESTNO_CHAN_USAGE	= 0x40,
	NM_IPACC_TESTNO_BCCH_CHAN_USAGE	= 0x41,
	NM_IPACC_TESTNO_FREQ_SYNC	= 0x42,
	NM_IPACC_TESTNO_BCCH_INFO	= 0x43,
	NM_IPACC_TESTNO_TX_BEACON	= 0x44,
	NM_IPACC_TESTNO_SYSINFO_MONITOR	= 0x45,
	NM_IPACC_TESTNO_BCCCH_MONITOR	= 0x46,
};

/*! \brief first byte after length inside NM_ATT_TEST_REPORT */
enum abis_nm_ipacc_test_res {
	NM_IPACC_TESTRES_SUCCESS	= 0,
	NM_IPACC_TESTRES_TIMEOUT	= 1,
	NM_IPACC_TESTRES_NO_CHANS	= 2,
	NM_IPACC_TESTRES_PARTIAL	= 3,
	NM_IPACC_TESTRES_STOPPED	= 4,
};

/*! \brief internal IE inside NM_ATT_TEST_REPORT */
enum abis_nm_ipacc_testres_ie {
	NM_IPACC_TR_IE_FREQ_ERR_LIST	= 3,
	NM_IPACC_TR_IE_CHAN_USAGE	= 4,
	NM_IPACC_TR_IE_BCCH_INFO	= 6,
	NM_IPACC_TR_IE_RESULT_DETAILS	= 8,
	NM_IPACC_TR_IE_FREQ_ERR		= 18,
};

/*! \brief ip.access IEI */
enum ipac_eie {
	NM_IPAC_EIE_ARFCN_WHITE		= 0x01,
	NM_IPAC_EIE_ARFCH_BLACK		= 0x02,
	NM_IPAC_EIE_FREQ_ERR_LIST	= 0x03,
	NM_IPAC_EIE_CHAN_USE_LIST	= 0x04,
	NM_IPAC_EIE_BCCH_INFO_TYPE	= 0x05,
	NM_IPAC_EIE_BCCH_INFO		= 0x06,
	NM_IPAC_EIE_CONFIG		= 0x07,
	NM_IPAC_EIE_RES_DETAILS		= 0x08,
	NM_IPAC_EIE_RXLEV_THRESH	= 0x09,
	NM_IPAC_EIE_FREQ_SYNC_OPTS	= 0x0a,
	NM_IPAC_EIE_MAC_ADDR		= 0x0b,
	NM_IPAC_EIE_HW_SW_COMPAT_NR	= 0x0c,
	NM_IPAC_EIE_MANUF_SER_NR	= 0x0d,
	NM_IPAC_EIE_OEM_ID		= 0x0e,
	NM_IPAC_EIE_DATE_TIME_MANUF	= 0x0f,
	NM_IPAC_EIE_DATE_TIME_CALIB	= 0x10,
	NM_IPAC_EIE_BEACON_INFO		= 0x11,
	NM_IPAC_EIE_FREQ_ERR		= 0x12,
	/* FIXME */
	NM_IPAC_EIE_FREQ_BANDS		= 0x1c,
	NM_IPAC_EIE_MAX_TA		= 0x1d,
	NM_IPAC_EIE_CIPH_ALGOS		= 0x1e,
	NM_IPAC_EIE_CHAN_TYPES		= 0x1f,
	NM_IPAC_EIE_CHAN_MODES		= 0x20,
	NM_IPAC_EIE_GPRS_CODING		= 0x21,
	NM_IPAC_EIE_RTP_FEATURES	= 0x22,
	NM_IPAC_EIE_RSL_FEATURES	= 0x23,
	NM_IPAC_EIE_BTS_HW_CLASS	= 0x24,
	NM_IPAC_EIE_BTS_ID		= 0x25,
};

/*! \brief ip.access NWL BCCH information type */
enum ipac_bcch_info_type {
	IPAC_BINF_RXLEV			= (1 << 8),
	IPAC_BINF_RXQUAL		= (1 << 9),
	IPAC_BINF_FREQ_ERR_QUAL		= (1 << 10),
	IPAC_BINF_FRAME_OFFSET		= (1 << 11),
	IPAC_BINF_FRAME_NR_OFFSET	= (1 << 12),
	IPAC_BINF_BSIC			= (1 << 13),
	IPAC_BINF_CGI			= (1 << 14),
	IPAC_BINF_NEIGH_BA_SI2		= (1 << 15),
	IPAC_BINF_NEIGH_BA_SI2bis	= (1 << 0),
	IPAC_BINF_NEIGH_BA_SI2ter	= (1 << 1),
	IPAC_BINF_CELL_ALLOC		= (1 << 2),
};

/*! @} */
