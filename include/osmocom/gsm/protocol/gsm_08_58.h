#ifndef PROTO_GSM_08_58_H
#define PROTO_GSM_08_58_H

/* GSM Radio Signalling Link messages on the A-bis interface 
 * 3GPP TS 08.58 version 8.6.0 Release 1999 / ETSI TS 100 596 V8.6.0 */

/* (C) 2008 by Harald Welte <laforge@gnumonks.org>
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

#include <stdint.h>

/*! \addtogroup rsl
 *  @{
 */

/*! \file gsm_08_58.h */

/*! \brief RSL common header */
struct abis_rsl_common_hdr {
	uint8_t	msg_discr;	/*!< \brief message discriminator (ABIS_RSL_MDISC_*) */
	uint8_t	msg_type;	/*!< \brief message type (\ref abis_rsl_msgtype) */
	uint8_t	data[0];	/*!< \brief actual payload data */
} __attribute__ ((packed));

/* \brief RSL RLL header (Chapter 8.3) */
struct abis_rsl_rll_hdr {
	struct abis_rsl_common_hdr c;
	uint8_t	ie_chan;	/*!< \brief \ref RSL_IE_CHAN_NR (tag) */
	uint8_t	chan_nr;	/*!< \brief RSL channel number (value) */
	uint8_t	ie_link_id;	/*!< \brief \ref RSL_IE_LINK_IDENT (tag) */
	uint8_t	link_id;	/*!< \brief RSL link identifier (value) */
	uint8_t	data[0];	/*!< \brief message payload data */
} __attribute__ ((packed));

/* \brief RSL Dedicated Channel header (Chapter 8.3 and 8.4) */
struct abis_rsl_dchan_hdr {
	struct abis_rsl_common_hdr c;
	uint8_t	ie_chan;	/*!< \brief \ref RSL_IE_CHAN_NR (tag) */
	uint8_t	chan_nr;	/*!< \brief RSL channel number (value) */
	uint8_t	data[0];	/*!< \brief message payload data */
} __attribute__ ((packed));

/* \brief RSL Common Channel header (Chapter 8.5) */
struct abis_rsl_cchan_hdr {
	struct abis_rsl_common_hdr c;
	uint8_t	ie_chan;	/*!< \brief \ref RSL_IE_CHAN_NR (tag) */
	uint8_t	chan_nr;	/*!< \brief RSL channel number (value) */
	uint8_t	data[0];	/*!< \brief message payload data */
} __attribute__ ((packed));


/* Chapter 9.1 */
/* \brief RSL Message Discriminator: RLL */
#define ABIS_RSL_MDISC_RLL		0x02
/* \brief RSL Message Discriminator: Dedicated Channel */
#define ABIS_RSL_MDISC_DED_CHAN		0x08
/* \brief RSL Message Discriminator: Common Channel */
#define ABIS_RSL_MDISC_COM_CHAN		0x0c
/* \brief RSL Message Discriminator: TRX Management */
#define ABIS_RSL_MDISC_TRX		0x10
/* \brief RSL Message Discriminator: Location Service */
#define ABIS_RSL_MDISC_LOC		0x20
/* \brief RSL Message Discriminator: ip.access */
#define ABIS_RSL_MDISC_IPACCESS		0x7e
#define ABIS_RSL_MDISC_TRANSP		0x01

/* \brief Check if given RSL message discriminator is transparent */
#define ABIS_RSL_MDISC_IS_TRANSP(x)	(x & 0x01)

/* \brief RSL Message Tyoe (Chapter 9.1) */
enum abis_rsl_msgtype {
	/* Radio Link Layer Management */
	RSL_MT_DATA_REQ			= 0x01,
	RSL_MT_DATA_IND,
	RSL_MT_ERROR_IND,
	RSL_MT_EST_REQ,
	RSL_MT_EST_CONF,
	RSL_MT_EST_IND,
	RSL_MT_REL_REQ,
	RSL_MT_REL_CONF,
	RSL_MT_REL_IND,
	RSL_MT_UNIT_DATA_REQ,
	RSL_MT_UNIT_DATA_IND,		/* 0x0b */
	RSL_MT_SUSP_REQ,		/* non-standard elements */
	RSL_MT_SUSP_CONF,
	RSL_MT_RES_REQ,
	RSL_MT_RECON_REQ,		/* 0x0f */

	/* Common Channel Management / TRX Management */
	RSL_MT_BCCH_INFO			= 0x11,
	RSL_MT_CCCH_LOAD_IND,
	RSL_MT_CHAN_RQD,
	RSL_MT_DELETE_IND,
	RSL_MT_PAGING_CMD,
	RSL_MT_IMMEDIATE_ASSIGN_CMD,
	RSL_MT_SMS_BC_REQ,
	RSL_MT_CHAN_CONF,		/* non-standard element */
	/* empty */
	RSL_MT_RF_RES_IND			= 0x19,
	RSL_MT_SACCH_FILL,
	RSL_MT_OVERLOAD,
	RSL_MT_ERROR_REPORT,
	RSL_MT_SMS_BC_CMD,
	RSL_MT_CBCH_LOAD_IND,
	RSL_MT_NOT_CMD,			/* 0x1f */

	/* Dedicate Channel Management */
	RSL_MT_CHAN_ACTIV			= 0x21,
	RSL_MT_CHAN_ACTIV_ACK,
	RSL_MT_CHAN_ACTIV_NACK,
	RSL_MT_CONN_FAIL,
	RSL_MT_DEACTIVATE_SACCH,
	RSL_MT_ENCR_CMD,
	RSL_MT_HANDO_DET,
	RSL_MT_MEAS_RES,
	RSL_MT_MODE_MODIFY_REQ,
	RSL_MT_MODE_MODIFY_ACK,
	RSL_MT_MODE_MODIFY_NACK,
	RSL_MT_PHY_CONTEXT_REQ,
	RSL_MT_PHY_CONTEXT_CONF,
	RSL_MT_RF_CHAN_REL,
	RSL_MT_MS_POWER_CONTROL,
	RSL_MT_BS_POWER_CONTROL,		/* 0x30 */
	RSL_MT_PREPROC_CONFIG,
	RSL_MT_PREPROC_MEAS_RES,
	RSL_MT_RF_CHAN_REL_ACK,
	RSL_MT_SACCH_INFO_MODIFY,
	RSL_MT_TALKER_DET,
	RSL_MT_LISTENER_DET,
	RSL_MT_REMOTE_CODEC_CONF_REP,
	RSL_MT_RTD_REP,
	RSL_MT_PRE_HANDO_NOTIF,
	RSL_MT_MR_CODEC_MOD_REQ,
	RSL_MT_MR_CODEC_MOD_ACK,
	RSL_MT_MR_CODEC_MOD_NACK,
	RSL_MT_MR_CODEC_MOD_PER,
	RSL_MT_TFO_REP,
	RSL_MT_TFO_MOD_REQ,		/* 0x3f */
	RSL_MT_LOCATION_INFO		= 0x41,

	/* ip.access specific RSL message types */
	RSL_MT_IPAC_DIR_RETR_ENQ	= 0x40,
	RSL_MT_IPAC_PDCH_ACT		= 0x48,
	RSL_MT_IPAC_PDCH_ACT_ACK,
	RSL_MT_IPAC_PDCH_ACT_NACK,
	RSL_MT_IPAC_PDCH_DEACT		= 0x4b,
	RSL_MT_IPAC_PDCH_DEACT_ACK,
	RSL_MT_IPAC_PDCH_DEACT_NACK,
	RSL_MT_IPAC_CONNECT_MUX		= 0x50,
	RSL_MT_IPAC_CONNECT_MUX_ACK,
	RSL_MT_IPAC_CONNECT_MUX_NACK,
	RSL_MT_IPAC_BIND_MUX		= 0x53,
	RSL_MT_IPAC_BIND_MUX_ACK,
	RSL_MT_IPAC_BIND_MUX_NACK,
	RSL_MT_IPAC_DISC_MUX		= 0x56,
	RSL_MT_IPAC_DISC_MUX_ACK,
	RSL_MT_IPAC_DISC_MUX_NACK,
	RSL_MT_IPAC_CRCX		= 0x70,		/* Bind to local BTS RTP port */
	RSL_MT_IPAC_CRCX_ACK,
	RSL_MT_IPAC_CRCX_NACK,
	RSL_MT_IPAC_MDCX		= 0x73,
	RSL_MT_IPAC_MDCX_ACK,
	RSL_MT_IPAC_MDCX_NACK,
	RSL_MT_IPAC_DLCX_IND		= 0x76,
	RSL_MT_IPAC_DLCX		= 0x77,
	RSL_MT_IPAC_DLCX_ACK,
	RSL_MT_IPAC_DLCX_NACK,
};

/*! \brief Siemens vendor-specific RSL message types */
enum abis_rsl_msgtype_siemens {
	RSL_MT_SIEMENS_MRPCI		= 0x41,
	RSL_MT_SIEMENS_INTRAC_HO_COND_IND = 0x42,
	RSL_MT_SIEMENS_INTERC_HO_COND_IND = 0x43,
	RSL_MT_SIEMENS_FORCED_HO_REQ	= 0x44,
	RSL_MT_SIEMENS_PREF_AREA_REQ	= 0x45,
	RSL_MT_SIEMENS_PREF_AREA	= 0x46,
	RSL_MT_SIEMENS_START_TRACE	= 0x47,
	RSL_MT_SIEMENS_START_TRACE_ACK	= 0x48,
	RSL_MT_SIEMENS_STOP_TRACE	= 0x49,
	RSL_MT_SIEMENS_TRMR		= 0x4a,
	RSL_MT_SIEMENS_HO_FAIL_IND	= 0x4b,
	RSL_MT_SIEMENS_STOP_TRACE_ACK	= 0x4c,
	RSL_MT_SIEMENS_UPLF		= 0x4d,
	RSL_MT_SIEMENS_UPLB		= 0x4e,
	RSL_MT_SIEMENS_SET_SYS_INFO_10	= 0x4f,
	RSL_MT_SIEMENS_MODIF_COND_IND	= 0x50,
};

/*! \brief RSL Information Element Identifiers (Chapter 9.3) */
enum abis_rsl_ie {
	RSL_IE_CHAN_NR			= 0x01,
	RSL_IE_LINK_IDENT,
	RSL_IE_ACT_TYPE,
	RSL_IE_BS_POWER,
	RSL_IE_CHAN_IDENT,
	RSL_IE_CHAN_MODE,
	RSL_IE_ENCR_INFO,
	RSL_IE_FRAME_NUMBER,
	RSL_IE_HANDO_REF,
	RSL_IE_L1_INFO,
	RSL_IE_L3_INFO,
	RSL_IE_MS_IDENTITY,
	RSL_IE_MS_POWER,
	RSL_IE_PAGING_GROUP,
	RSL_IE_PAGING_LOAD,
	RSL_IE_PYHS_CONTEXT		= 0x10,
	RSL_IE_ACCESS_DELAY,
	RSL_IE_RACH_LOAD,
	RSL_IE_REQ_REFERENCE,
	RSL_IE_RELEASE_MODE,
	RSL_IE_RESOURCE_INFO,
	RSL_IE_RLM_CAUSE,
	RSL_IE_STARTNG_TIME,
	RSL_IE_TIMING_ADVANCE,
	RSL_IE_UPLINK_MEAS,
	RSL_IE_CAUSE,
	RSL_IE_MEAS_RES_NR,
	RSL_IE_MSG_ID,
	/* reserved */
	RSL_IE_SYSINFO_TYPE		= 0x1e,
	RSL_IE_MS_POWER_PARAM,
	RSL_IE_BS_POWER_PARAM,
	RSL_IE_PREPROC_PARAM,
	RSL_IE_PREPROC_MEAS,
	RSL_IE_IMM_ASS_INFO,		/* Phase 1 (3.6.0), later Full below */
	RSL_IE_SMSCB_INFO		= 0x24,
	RSL_IE_MS_TIMING_OFFSET,
	RSL_IE_ERR_MSG,
	RSL_IE_FULL_BCCH_INFO,
	RSL_IE_CHAN_NEEDED,
	RSL_IE_CB_CMD_TYPE,
	RSL_IE_SMSCB_MSG,
	RSL_IE_FULL_IMM_ASS_INFO,
	RSL_IE_SACCH_INFO,
	RSL_IE_CBCH_LOAD_INFO,
	RSL_IE_SMSCB_CHAN_INDICATOR,
	RSL_IE_GROUP_CALL_REF,
	RSL_IE_CHAN_DESC		= 0x30,
	RSL_IE_NCH_DRX_INFO,
	RSL_IE_CMD_INDICATOR,
	RSL_IE_EMLPP_PRIO,
	RSL_IE_UIC,
	RSL_IE_MAIN_CHAN_REF,
	RSL_IE_MR_CONFIG,
	RSL_IE_MR_CONTROL,
	RSL_IE_SUP_CODEC_TYPES,
	RSL_IE_CODEC_CONFIG,
	RSL_IE_RTD,
	RSL_IE_TFO_STATUS,
	RSL_IE_LLP_APDU,
	/* Siemens vendor-specific */
	RSL_IE_SIEMENS_MRPCI		= 0x40,
	RSL_IE_SIEMENS_PREF_AREA_TYPE	= 0x43,
	RSL_IE_SIEMENS_ININ_CELL_HO_PAR	= 0x45,
	RSL_IE_SIEMENS_TRACE_REF_NR	= 0x46,
	RSL_IE_SIEMENS_INT_TRACE_IDX	= 0x47,
	RSL_IE_SIEMENS_L2_HDR_INFO	= 0x48,
	RSL_IE_SIEMENS_HIGHEST_RATE	= 0x4e,
	RSL_IE_SIEMENS_SUGGESTED_RATE	= 0x4f,

	/* ip.access */
	RSL_IE_IPAC_SRTP_CONFIG	= 0xe0,
	RSL_IE_IPAC_PROXY_UDP	= 0xe1,
	RSL_IE_IPAC_BSCMPL_TOUT	= 0xe2,
	RSL_IE_IPAC_REMOTE_IP	= 0xf0,
	RSL_IE_IPAC_REMOTE_PORT	= 0xf1,
	RSL_IE_IPAC_RTP_PAYLOAD	= 0xf2,
	RSL_IE_IPAC_LOCAL_PORT	= 0xf3,
	RSL_IE_IPAC_SPEECH_MODE	= 0xf4,
	RSL_IE_IPAC_LOCAL_IP	= 0xf5,
	RSL_IE_IPAC_CONN_STAT	= 0xf6,
	RSL_IE_IPAC_HO_C_PARMS	= 0xf7,
	RSL_IE_IPAC_CONN_ID	= 0xf8,
	RSL_IE_IPAC_RTP_CSD_FMT	= 0xf9,
	RSL_IE_IPAC_RTP_JIT_BUF	= 0xfa,
	RSL_IE_IPAC_RTP_COMPR	= 0xfb,
	RSL_IE_IPAC_RTP_PAYLOAD2= 0xfc,
	RSL_IE_IPAC_RTP_MPLEX	= 0xfd,
	RSL_IE_IPAC_RTP_MPLEX_ID= 0xfe,
};

/* Chapter 9.3.1 */
#define RSL_CHAN_NR_MASK	0xf8
#define RSL_CHAN_Bm_ACCHs	0x08
#define RSL_CHAN_Lm_ACCHs	0x10	/* .. 0x18 */
#define RSL_CHAN_SDCCH4_ACCH	0x20	/* .. 0x38 */
#define RSL_CHAN_SDCCH8_ACCH	0x40	/* ...0x78 */
#define RSL_CHAN_BCCH		0x80
#define RSL_CHAN_RACH		0x88
#define RSL_CHAN_PCH_AGCH	0x90

/* Chapter 9.3.3 */
#define RSL_ACT_TYPE_INITIAL	0x00
#define RSL_ACT_TYPE_REACT	0x80
#define RSL_ACT_INTRA_IMM_ASS	0x00
#define RSL_ACT_INTRA_NORM_ASS	0x01
#define RSL_ACT_INTER_ASYNC	0x02
#define RSL_ACT_INTER_SYNC	0x03
#define RSL_ACT_SECOND_ADD	0x04
#define RSL_ACT_SECOND_MULTI	0x05

/*! \brief RSL Channel Mode IF (Chapter 9.3.6) */
struct rsl_ie_chan_mode {
	uint8_t dtx_dtu;
	uint8_t spd_ind;
	uint8_t chan_rt;
	uint8_t chan_rate;
} __attribute__ ((packed));
#define RSL_CMOD_DTXu		0x01	/* uplink */
#define RSL_CMOD_DTXd		0x02	/* downlink */
enum rsl_cmod_spd {
	RSL_CMOD_SPD_SPEECH	= 0x01,
	RSL_CMOD_SPD_DATA	= 0x02,
	RSL_CMOD_SPD_SIGN	= 0x03,
};
#define RSL_CMOD_CRT_SDCCH	0x01
#define RSL_CMOD_CRT_TCH_Bm	0x08	/* full-rate */
#define RSL_CMOD_CRT_TCH_Lm	0x09	/* half-rate */
/* FIXME: More CRT types */
/* Speech */
#define RSL_CMOD_SP_GSM1	0x01
#define RSL_CMOD_SP_GSM2	0x11
#define RSL_CMOD_SP_GSM3	0x21
/* non-transparent data */
#define RSL_CMOD_CSD_NT_43k5	0x74
#define RSL_CMOD_CSD_NT_28k8	0x71
#define RSL_CMOD_CSD_NT_14k5	0x58
#define RSL_CMOD_CSD_NT_12k0	0x50
#define RSL_CMOD_CSD_NT_6k0	0x51
/* legacy #defines with wrong name */
#define RSL_CMOD_SP_NT_14k5	RSL_CMOD_CSD_NT_14k5
#define RSL_CMOD_SP_NT_12k0	RSL_CMOD_CSD_NT_12k0
#define RSL_CMOD_SP_NT_6k0	RSL_CMOD_CSD_NT_6k0
/* transparent data */
#define RSL_CMOD_CSD_T_32000	0x38
#define RSL_CMOD_CSD_T_29000	0x39
#define RSL_CMOD_CSD_T_14400	0x18
#define RSL_CMOD_CSD_T_9600	0x10
#define RSL_CMOD_CSD_T_4800	0x11
#define RSL_CMOD_CSD_T_2400	0x12
#define RSL_CMOD_CSD_T_1200	0x13
#define RSL_CMOD_CSD_T_600	0x14
#define RSL_CMOD_CSD_T_1200_75	0x15

/*! \brief RSL Channel Identification IE (Chapter 9.3.5) */
struct rsl_ie_chan_ident {
	/* GSM 04.08 10.5.2.5 */
	struct {
		uint8_t iei;
		uint8_t chan_nr;	/* enc_chan_nr */
		uint8_t oct3;
		uint8_t oct4;
	} chan_desc;
#if 0	/* spec says we need this but Abissim doesn't use it */
	struct {
		uint8_t tag;
		uint8_t len;
	} mobile_alloc;
#endif
} __attribute__ ((packed));

/* Chapter 9.3.22 */
#define RLL_CAUSE_T200_EXPIRED		0x01
#define RLL_CAUSE_REEST_REQ		0x02
#define RLL_CAUSE_UNSOL_UA_RESP		0x03
#define RLL_CAUSE_UNSOL_DM_RESP		0x04
#define RLL_CAUSE_UNSOL_DM_RESP_MF	0x05
#define RLL_CAUSE_UNSOL_SPRV_RESP	0x06
#define RLL_CAUSE_SEQ_ERR		0x07
#define RLL_CAUSE_UFRM_INC_PARAM	0x08
#define RLL_CAUSE_SFRM_INC_PARAM	0x09
#define RLL_CAUSE_IFRM_INC_MBITS	0x0a
#define RLL_CAUSE_IFRM_INC_LEN		0x0b
#define RLL_CAUSE_FRM_UNIMPL		0x0c
#define RLL_CAUSE_SABM_MF		0x0d
#define RLL_CAUSE_SABM_INFO_NOTALL	0x0e

/* Chapter 9.3.26 */
#define RSL_ERRCLS_NORMAL		0x00
#define RSL_ERRCLS_RESOURCE_UNAVAIL	0x20
#define RSL_ERRCLS_SERVICE_UNAVAIL	0x30
#define RSL_ERRCLS_SERVICE_UNIMPL	0x40
#define RSL_ERRCLS_INVAL_MSG		0x50
#define RSL_ERRCLS_PROTO_ERROR		0x60
#define RSL_ERRCLS_INTERWORKING		0x70

/* normal event */
#define RSL_ERR_RADIO_IF_FAIL		0x00
#define RSL_ERR_RADIO_LINK_FAIL		0x01
#define RSL_ERR_HANDOVER_ACC_FAIL	0x02
#define RSL_ERR_TALKER_ACC_FAIL		0x03
#define RSL_ERR_OM_INTERVENTION		0x07
#define RSL_ERR_NORMAL_UNSPEC		0x0f
#define RSL_ERR_T_MSRFPCI_EXP		0x18
/* resource unavailable */
#define RSL_ERR_EQUIPMENT_FAIL		0x20
#define RSL_ERR_RR_UNAVAIL		0x21
#define RSL_ERR_TERR_CH_FAIL		0x22
#define RSL_ERR_CCCH_OVERLOAD		0x23
#define RSL_ERR_ACCH_OVERLOAD		0x24
#define RSL_ERR_PROCESSOR_OVERLOAD	0x25
#define RSL_ERR_RES_UNAVAIL		0x2f
/* service or option not available */
#define RSL_ERR_TRANSC_UNAVAIL		0x30
#define RSL_ERR_SERV_OPT_UNAVAIL	0x3f
/* service or option not implemented */
#define RSL_ERR_ENCR_UNIMPL		0x40
#define RSL_ERR_SERV_OPT_UNIMPL		0x4f
/* invalid message */
#define RSL_ERR_RCH_ALR_ACTV_ALLOC	0x50
#define RSL_ERR_INVALID_MESSAGE		0x5f
/* protocol error */
#define RSL_ERR_MSG_DISCR		0x60
#define RSL_ERR_MSG_TYPE		0x61
#define RSL_ERR_MSG_SEQ			0x62
#define RSL_ERR_IE_ERROR		0x63
#define RSL_ERR_MAND_IE_ERROR		0x64
#define RSL_ERR_OPT_IE_ERROR		0x65
#define RSL_ERR_IE_NONEXIST		0x66
#define RSL_ERR_IE_LENGTH		0x67
#define RSL_ERR_IE_CONTENT		0x68
#define RSL_ERR_PROTO			0x6f
/* interworking */
#define RSL_ERR_INTERWORKING		0x7f

/* Chapter 9.3.30 */
#define RSL_SYSTEM_INFO_8	0x00
#define RSL_SYSTEM_INFO_1	0x01
#define RSL_SYSTEM_INFO_2	0x02
#define RSL_SYSTEM_INFO_3	0x03
#define RSL_SYSTEM_INFO_4	0x04
#define RSL_SYSTEM_INFO_5	0x05
#define RSL_SYSTEM_INFO_6	0x06
#define RSL_SYSTEM_INFO_7	0x07
#define RSL_SYSTEM_INFO_16	0x08
#define RSL_SYSTEM_INFO_17	0x09
#define RSL_SYSTEM_INFO_2bis	0x0a
#define RSL_SYSTEM_INFO_2ter	0x0b
#define RSL_SYSTEM_INFO_5bis	0x0d
#define RSL_SYSTEM_INFO_5ter	0x0e
#define RSL_SYSTEM_INFO_10	0x0f
#define RSL_EXT_MEAS_ORDER	0x47
#define RSL_MEAS_INFO		0x48
#define RSL_SYSTEM_INFO_13	0x28
#define RSL_SYSTEM_INFO_2quater	0x29
#define RSL_SYSTEM_INFO_9	0x2a
#define RSL_SYSTEM_INFO_18	0x2b
#define RSL_SYSTEM_INFO_19	0x2c
#define RSL_SYSTEM_INFO_20	0x2d

/* Chapter 9.3.40 */
#define RSL_CHANNEED_ANY	0x00
#define RSL_CHANNEED_SDCCH	0x01
#define RSL_CHANNEED_TCH_F	0x02
#define RSL_CHANNEED_TCH_ForH	0x03

/*! \brief RSL Cell Broadcast Command (Chapter 9.3.45) */
struct rsl_ie_cb_cmd_type {
	uint8_t last_block:2;
	uint8_t spare:1;
	uint8_t def_bcast:1;
	uint8_t command:4;
} __attribute__ ((packed));
/* ->command */
#define RSL_CB_CMD_TYPE_NORMAL		0x00
#define RSL_CB_CMD_TYPE_SCHEDULE	0x08
#define RSL_CB_CMD_TYPE_DEFAULT		0x0e
#define RSL_CB_CMD_TYPE_NULL		0x0f
/* ->def_bcast */
#define RSL_CB_CMD_DEFBCAST_NORMAL	0
#define RSL_CB_CMD_DEFBCAST_NULL	1
/* ->last_block */
#define RSL_CB_CMD_LASTBLOCK_4		0
#define RSL_CB_CMD_LASTBLOCK_1		1
#define RSL_CB_CMD_LASTBLOCK_2		2
#define RSL_CB_CMD_LASTBLOCK_3		3

/* Chapter 3.3.2.3 Brocast control channel */
/* CCCH-CONF, NC is not combined */
#define RSL_BCCH_CCCH_CONF_1_NC	0x00
#define RSL_BCCH_CCCH_CONF_1_C	0x01
#define RSL_BCCH_CCCH_CONF_2_NC	0x02
#define RSL_BCCH_CCCH_CONF_3_NC	0x04
#define RSL_BCCH_CCCH_CONF_4_NC	0x06

/* BS-PA-MFRMS */
#define RSL_BS_PA_MFRMS_2	0x00
#define RSL_BS_PA_MFRMS_3	0x01
#define RSL_BS_PA_MFRMS_4	0x02
#define RSL_BS_PA_MFRMS_5	0x03
#define RSL_BS_PA_MFRMS_6	0x04
#define RSL_BS_PA_MFRMS_7	0x05
#define RSL_BS_PA_MFRMS_8	0x06
#define RSL_BS_PA_MFRMS_9	0x07

/* RSL_IE_IPAC_RTP_PAYLOAD[2] */
enum rsl_ipac_rtp_payload {
	RSL_IPAC_RTP_GSM	= 1,
	RSL_IPAC_RTP_EFR,
	RSL_IPAC_RTP_AMR,
	RSL_IPAC_RTP_CSD,
	RSL_IPAC_RTP_MUX,
};

/* RSL_IE_IPAC_SPEECH_MODE, lower four bits */
enum rsl_ipac_speech_mode_s {
	RSL_IPAC_SPEECH_GSM_FR = 0,	/* GSM FR (Type 1, FS) */
	RSL_IPAC_SPEECH_GSM_EFR = 1,	/* GSM EFR (Type 2, FS) */
	RSL_IPAC_SPEECH_GSM_AMR_FR = 2,	/* GSM AMR/FR (Type 3, FS) */
	RSL_IPAC_SPEECH_GSM_HR = 3,	/* GSM HR (Type 1, HS) */
	RSL_IPAC_SPEECH_GSM_AMR_HR = 5,	/* GSM AMR/hr (Type 3, HS) */
	RSL_IPAC_SPEECH_AS_RTP = 0xf,	/* As specified by RTP Payload IE */
};
/* RSL_IE_IPAC_SPEECH_MODE, upper four bits */
enum rsl_ipac_speech_mode_m {
	RSL_IPAC_SPEECH_M_RXTX = 0,	/* Send and Receive */
	RSL_IPAC_SPEECH_M_RX = 1,	/* Receive only */
	RSL_IPAC_SPEECH_M_TX = 2,	/* Send only */
};

/* RSL_IE_IPAC_RTP_CSD_FMT, lower four bits */
enum rsl_ipac_rtp_csd_format_d {
	RSL_IPAC_RTP_CSD_EXT_TRAU = 0,
	RSL_IPAC_RTP_CSD_NON_TRAU = 1,
	RSL_IPAC_RTP_CSD_TRAU_BTS = 2,
	RSL_IPAC_RTP_CSD_IWF_FREE = 3,
};
/* RSL_IE_IPAC_RTP_CSD_FMT, upper four bits */
enum rsl_ipac_rtp_csd_format_ir {
	RSL_IPAC_RTP_CSD_IR_8k = 0,
	RSL_IPAC_RTP_CSD_IR_16k = 1,
	RSL_IPAC_RTP_CSD_IR_32k = 2,
	RSL_IPAC_RTP_CSD_IR_64k = 3,
};

/* Siemens vendor-specific RSL extensions */
struct rsl_mrpci {
	uint8_t power_class:3,
		 vgcs_capable:1,
		 vbs_capable:1,
		 gsm_phase:2;
} __attribute__ ((packed));

enum rsl_mrpci_pwrclass {
	RSL_MRPCI_PWRC_1	= 0,
	RSL_MRPCI_PWRC_2	= 1,
	RSL_MRPCI_PWRC_3	= 2,
	RSL_MRPCI_PWRC_4	= 3,
	RSL_MRPCI_PWRC_5	= 4,
};
enum rsl_mrpci_phase {
	RSL_MRPCI_PHASE_1	= 0,
	/* reserved */
	RSL_MRPCI_PHASE_2	= 2,
	RSL_MRPCI_PHASE_2PLUS	= 3,
};

/*! @} */

#endif /* PROTO_GSM_08_58_H */
