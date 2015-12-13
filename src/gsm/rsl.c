/* GSM Radio Signalling Link messages on the A-bis interface 
 * 3GPP TS 08.58 version 8.6.0 Release 1999 / ETSI TS 100 596 V8.6.0 */

/* (C) 2008-2010 by Harald Welte <laforge@gnumonks.org>
 *
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
#include <stdio.h>
#include <errno.h>

#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/rsl.h>

/*! \addtogroup rsl
 *  @{
 */

/*! \file rsl.c */

/*! \brief Size for RSL \ref msgb_alloc */
#define RSL_ALLOC_SIZE		200
/*! \brief Headroom size for RSL \ref msgb_alloc */
#define RSL_ALLOC_HEADROOM	56

/*! \brief Initialize a RSL RLL header */
void rsl_init_rll_hdr(struct abis_rsl_rll_hdr *dh, uint8_t msg_type)
{
	dh->c.msg_discr = ABIS_RSL_MDISC_RLL;
	dh->c.msg_type = msg_type;
	dh->ie_chan = RSL_IE_CHAN_NR;
	dh->ie_link_id = RSL_IE_LINK_IDENT;
}

/*! \brief Initialize a RSL Common Channel header */
void rsl_init_cchan_hdr(struct abis_rsl_cchan_hdr *ch, uint8_t msg_type)
{
	ch->c.msg_discr = ABIS_RSL_MDISC_COM_CHAN;
	ch->c.msg_type = msg_type;
	ch->ie_chan = RSL_IE_CHAN_NR;
}

/* \brief TLV parser definition for RSL */
const struct tlv_definition rsl_att_tlvdef = {
	.def = {
		[RSL_IE_CHAN_NR]		= { TLV_TYPE_TV },
		[RSL_IE_LINK_IDENT]		= { TLV_TYPE_TV },
		[RSL_IE_ACT_TYPE]		= { TLV_TYPE_TV },
		[RSL_IE_BS_POWER]		= { TLV_TYPE_TV },
		[RSL_IE_CHAN_IDENT]		= { TLV_TYPE_TLV },
		[RSL_IE_CHAN_MODE]		= { TLV_TYPE_TLV },
		[RSL_IE_ENCR_INFO]		= { TLV_TYPE_TLV },
		[RSL_IE_FRAME_NUMBER]		= { TLV_TYPE_FIXED, 2 },
		[RSL_IE_HANDO_REF]		= { TLV_TYPE_TV },
		[RSL_IE_L1_INFO]		= { TLV_TYPE_FIXED, 2 },
		[RSL_IE_L3_INFO]		= { TLV_TYPE_TL16V },
		[RSL_IE_MS_IDENTITY]		= { TLV_TYPE_TLV },
		[RSL_IE_MS_POWER]		= { TLV_TYPE_TV },
		[RSL_IE_PAGING_GROUP]		= { TLV_TYPE_TV },
		[RSL_IE_PAGING_LOAD]		= { TLV_TYPE_FIXED, 2 },
		[RSL_IE_PYHS_CONTEXT]		= { TLV_TYPE_TLV },
		[RSL_IE_ACCESS_DELAY]		= { TLV_TYPE_TV },
		[RSL_IE_RACH_LOAD]		= { TLV_TYPE_TLV },
		[RSL_IE_REQ_REFERENCE]		= { TLV_TYPE_FIXED, 3 },
		[RSL_IE_RELEASE_MODE]		= { TLV_TYPE_TV },
		[RSL_IE_RESOURCE_INFO]		= { TLV_TYPE_TLV },
		[RSL_IE_RLM_CAUSE]		= { TLV_TYPE_TLV },
		[RSL_IE_STARTNG_TIME]		= { TLV_TYPE_FIXED, 2 },
		[RSL_IE_TIMING_ADVANCE]		= { TLV_TYPE_TV },
		[RSL_IE_UPLINK_MEAS]		= { TLV_TYPE_TLV },
		[RSL_IE_CAUSE]			= { TLV_TYPE_TLV },
		[RSL_IE_MEAS_RES_NR]		= { TLV_TYPE_TV },
		[RSL_IE_MSG_ID]			= { TLV_TYPE_TV },
		[RSL_IE_SYSINFO_TYPE]		= { TLV_TYPE_TV },
		[RSL_IE_MS_POWER_PARAM]		= { TLV_TYPE_TLV },
		[RSL_IE_BS_POWER_PARAM]		= { TLV_TYPE_TLV },
		[RSL_IE_PREPROC_PARAM]		= { TLV_TYPE_TLV },
		[RSL_IE_PREPROC_MEAS]		= { TLV_TYPE_TLV },
		[RSL_IE_IMM_ASS_INFO]		= { TLV_TYPE_TLV },
		[RSL_IE_SMSCB_INFO]		= { TLV_TYPE_FIXED, 23 },
		[RSL_IE_MS_TIMING_OFFSET]	= { TLV_TYPE_TV },
		[RSL_IE_ERR_MSG]		= { TLV_TYPE_TLV },
		[RSL_IE_FULL_BCCH_INFO]		= { TLV_TYPE_TLV },
		[RSL_IE_CHAN_NEEDED]		= { TLV_TYPE_TV },
		[RSL_IE_CB_CMD_TYPE]		= { TLV_TYPE_TV },
		[RSL_IE_SMSCB_MSG]		= { TLV_TYPE_TLV },
		[RSL_IE_FULL_IMM_ASS_INFO]	= { TLV_TYPE_TLV },
		[RSL_IE_SACCH_INFO]		= { TLV_TYPE_TLV },
		[RSL_IE_CBCH_LOAD_INFO]		= { TLV_TYPE_TV },
		[RSL_IE_SMSCB_CHAN_INDICATOR]	= { TLV_TYPE_TV },
		[RSL_IE_GROUP_CALL_REF]		= { TLV_TYPE_TLV },
		[RSL_IE_CHAN_DESC]		= { TLV_TYPE_TLV },
		[RSL_IE_NCH_DRX_INFO]		= { TLV_TYPE_TLV },
		[RSL_IE_CMD_INDICATOR]		= { TLV_TYPE_TLV },
		[RSL_IE_EMLPP_PRIO]		= { TLV_TYPE_TV },
		[RSL_IE_UIC]			= { TLV_TYPE_TLV },
		[RSL_IE_MAIN_CHAN_REF]		= { TLV_TYPE_TV },
		[RSL_IE_MR_CONFIG]		= { TLV_TYPE_TLV },
		[RSL_IE_MR_CONTROL]		= { TLV_TYPE_TV },
		[RSL_IE_SUP_CODEC_TYPES]	= { TLV_TYPE_TLV },
		[RSL_IE_CODEC_CONFIG]		= { TLV_TYPE_TLV },
		[RSL_IE_RTD]			= { TLV_TYPE_TV },
		[RSL_IE_TFO_STATUS]		= { TLV_TYPE_TV },
		[RSL_IE_LLP_APDU]		= { TLV_TYPE_TLV },
		[RSL_IE_SIEMENS_MRPCI]		= { TLV_TYPE_TV },
		[RSL_IE_IPAC_PROXY_UDP]		= { TLV_TYPE_FIXED, 2 },
		[RSL_IE_IPAC_BSCMPL_TOUT]	= { TLV_TYPE_TV },
		[RSL_IE_IPAC_REMOTE_IP]		= { TLV_TYPE_FIXED, 4 },
		[RSL_IE_IPAC_REMOTE_PORT]	= { TLV_TYPE_FIXED, 2 },
		[RSL_IE_IPAC_RTP_PAYLOAD]	= { TLV_TYPE_TV },
		[RSL_IE_IPAC_LOCAL_PORT]	= { TLV_TYPE_FIXED, 2 },
		[RSL_IE_IPAC_SPEECH_MODE]	= { TLV_TYPE_TV },
		[RSL_IE_IPAC_LOCAL_IP]		= { TLV_TYPE_FIXED, 4 },
		[RSL_IE_IPAC_CONN_ID]		= { TLV_TYPE_FIXED, 2 },
		[RSL_IE_IPAC_RTP_CSD_FMT]	= { TLV_TYPE_TV },
		[RSL_IE_IPAC_RTP_JIT_BUF]	= { TLV_TYPE_FIXED, 2 },
		[RSL_IE_IPAC_RTP_COMPR]		= { TLV_TYPE_TV },
		[RSL_IE_IPAC_RTP_PAYLOAD2]	= { TLV_TYPE_TV },
		[RSL_IE_IPAC_RTP_MPLEX]		= { TLV_TYPE_FIXED, 8 },
		[RSL_IE_IPAC_RTP_MPLEX_ID]	= { TLV_TYPE_TV },
	},
};

/*! \brief Encode channel number as per Section 9.3.1 */
uint8_t rsl_enc_chan_nr(uint8_t type, uint8_t subch, uint8_t timeslot)
{
	uint8_t ret;

	ret = (timeslot & 0x07) | type;

	switch (type) {
	case RSL_CHAN_Lm_ACCHs:
		subch &= 0x01;
		break;
	case RSL_CHAN_SDCCH4_ACCH:
		subch &= 0x03;
		break;
	case RSL_CHAN_SDCCH8_ACCH:
		subch &= 0x07;
		break;
	default:
		/* no subchannels allowed */
		subch = 0x00;
		break;
	}
	ret |= (subch << 3);

	return ret;
}

/*! \brief Decode RSL channel number
 *  \param[in] chan_nr Channel Number
 *  \param[out] type Channel Type
 *  \param[out] subch Sub-channel Number
 *  \param[out] timeslot Timeslot
 */
int rsl_dec_chan_nr(uint8_t chan_nr, uint8_t *type, uint8_t *subch, uint8_t *timeslot)
{
	*timeslot = chan_nr & 0x7;

	if ((chan_nr & 0xf8) == RSL_CHAN_Bm_ACCHs) {
		*type = RSL_CHAN_Bm_ACCHs;
		*subch = 0;
	} else if ((chan_nr & 0xf0) == RSL_CHAN_Lm_ACCHs) {
		*type = RSL_CHAN_Lm_ACCHs;
		*subch = (chan_nr >> 3) & 0x1;
	} else if ((chan_nr & 0xe0) == RSL_CHAN_SDCCH4_ACCH) {
		*type = RSL_CHAN_SDCCH4_ACCH;
		*subch = (chan_nr >> 3) & 0x3;
	} else if ((chan_nr & 0xc0) == RSL_CHAN_SDCCH8_ACCH) {
		*type = RSL_CHAN_SDCCH8_ACCH;
		*subch = (chan_nr >> 3) & 0x7;
	} else if ((chan_nr & 0xf8) == RSL_CHAN_BCCH) {
		*type = RSL_CHAN_BCCH;
		*subch = 0;
	} else if ((chan_nr & 0xf8) == RSL_CHAN_RACH) {
		*type = RSL_CHAN_RACH;
		*subch = 0;
	} else if ((chan_nr & 0xf8) == RSL_CHAN_PCH_AGCH) {
		*type = RSL_CHAN_PCH_AGCH;
		*subch = 0;
	} else
		return -EINVAL;

	return 0;
}

/*! \brief Get human-readable string for RSL channel number */
const char *rsl_chan_nr_str(uint8_t chan_nr)
{
	static char str[20];
	int ts = chan_nr & 7;
	uint8_t cbits = chan_nr >> 3;

	if (cbits == 0x01)
		sprintf(str, "TCH/F on TS%d", ts);
	else if ((cbits & 0x1e) == 0x02)
		sprintf(str, "TCH/H(%u) on TS%d", cbits & 0x01, ts);
	else if ((cbits & 0x1c) == 0x04)
		sprintf(str, "SDCCH/4(%u) on TS%d", cbits & 0x03, ts);
	else if ((cbits & 0x18) == 0x08)
		sprintf(str, "SDCCH/8(%u) on TS%d", cbits & 0x07, ts);
	else if (cbits == 0x10)
		sprintf(str, "BCCH on TS%d", ts);
	else if (cbits == 0x11)
		sprintf(str, "RACH on TS%d", ts);
	else if (cbits == 0x12)
		sprintf(str, "PCH/AGCH on TS%d", ts);
	else
		sprintf(str, "UNKNOWN on TS%d", ts);

        return str;
}

static const struct value_string rsl_err_vals[] = {
	{ RSL_ERR_RADIO_IF_FAIL,	"Radio Interface Failure" },
	{ RSL_ERR_RADIO_LINK_FAIL,	"Radio Link Failure" },
	{ RSL_ERR_HANDOVER_ACC_FAIL,	"Handover Access Failure" },
	{ RSL_ERR_TALKER_ACC_FAIL,	"Talker Access Failure" },
	{ RSL_ERR_OM_INTERVENTION,	"O&M Intervention" },
	{ RSL_ERR_NORMAL_UNSPEC,	"Normal event, unspecified" },
	{ RSL_ERR_T_MSRFPCI_EXP,	"Siemens: T_MSRFPCI Expired" },
	{ RSL_ERR_EQUIPMENT_FAIL,	"Equipment Failure" },
	{ RSL_ERR_RR_UNAVAIL,		"Radio Resource not available" },
	{ RSL_ERR_TERR_CH_FAIL,		"Terrestrial Channel Failure" },
	{ RSL_ERR_CCCH_OVERLOAD,	"CCCH Overload" },
	{ RSL_ERR_ACCH_OVERLOAD,	"ACCH Overload" },
	{ RSL_ERR_PROCESSOR_OVERLOAD,	"Processor Overload" },
	{ RSL_ERR_RES_UNAVAIL,		"Resource not available, unspecified" },
	{ RSL_ERR_TRANSC_UNAVAIL,	"Transcoding not available" },
	{ RSL_ERR_SERV_OPT_UNAVAIL,	"Service or Option not available" },
	{ RSL_ERR_ENCR_UNIMPL,		"Encryption algorithm not implemented" },
	{ RSL_ERR_SERV_OPT_UNIMPL,	"Service or Option not implemented" },
	{ RSL_ERR_RCH_ALR_ACTV_ALLOC,	"Radio channel already activated" },
	{ RSL_ERR_INVALID_MESSAGE,	"Invalid Message, unspecified" },
	{ RSL_ERR_MSG_DISCR,		"Message Discriminator Error" },
	{ RSL_ERR_MSG_TYPE,		"Message Type Error" },
	{ RSL_ERR_MSG_SEQ,		"Message Sequence Error" },
	{ RSL_ERR_IE_ERROR,		"General IE error" },
	{ RSL_ERR_MAND_IE_ERROR,	"Mandatory IE error" },
	{ RSL_ERR_OPT_IE_ERROR,		"Optional IE error" },
	{ RSL_ERR_IE_NONEXIST,		"IE non-existent" },
	{ RSL_ERR_IE_LENGTH,		"IE length error" },
	{ RSL_ERR_IE_CONTENT,		"IE content error" },
	{ RSL_ERR_PROTO,		"Protocol error, unspecified" },
	{ RSL_ERR_INTERWORKING,		"Interworking error, unspecified" },
	{ 0,				NULL }
};

/*! \brief Get human-readable name for RSL Error */
const char *rsl_err_name(uint8_t err)
{
	return get_value_string(rsl_err_vals, err);
}

/* Names for Radio Link Layer Management */
static const struct value_string rsl_msgt_names[] = {
	{ RSL_MT_DATA_REQ,		"DATA_REQ" },
	{ RSL_MT_DATA_IND,		"DATA_IND" },
	{ RSL_MT_ERROR_IND,		"ERROR_IND" },
	{ RSL_MT_EST_REQ,		"EST_REQ" },
	{ RSL_MT_EST_CONF,		"EST_CONF" },
	{ RSL_MT_EST_IND,		"EST_IND" },
	{ RSL_MT_REL_REQ,		"REL_REQ" },
	{ RSL_MT_REL_CONF,		"REL_CONF" },
	{ RSL_MT_REL_IND,		"REL_IND" },
	{ RSL_MT_UNIT_DATA_REQ,		"UNIT_DATA_REQ" },
	{ RSL_MT_UNIT_DATA_IND,		"UNIT_DATA_IND" },
	{ RSL_MT_SUSP_REQ,		"SUSP_REQ" },
	{ RSL_MT_SUSP_CONF,		"SUSP_CONF" },
	{ RSL_MT_RES_REQ,		"RES_REQ" },
	{ RSL_MT_RECON_REQ,		"RECON_REQ" },

	{ RSL_MT_BCCH_INFO,		"BCCH_INFO" },
	{ RSL_MT_CCCH_LOAD_IND,		"CCCH_LOAD_IND" },
	{ RSL_MT_CHAN_RQD,		"CHAN_RQD" },
	{ RSL_MT_DELETE_IND,		"DELETE_IND" },
	{ RSL_MT_PAGING_CMD,		"PAGING_CMD" },
	{ RSL_MT_IMMEDIATE_ASSIGN_CMD,	"IMM_ASS_CMD" },
	{ RSL_MT_SMS_BC_REQ,		"SMS_BC_REQ" },
	{ RSL_MT_CHAN_CONF,		"CHAN_CONF" },

	{ RSL_MT_RF_RES_IND,		"RF_RES_IND" },
	{ RSL_MT_SACCH_FILL,		"SACCH_FILL" },
	{ RSL_MT_OVERLOAD,		"OVERLOAD" },
	{ RSL_MT_ERROR_REPORT,		"ERROR_REPORT" },
	{ RSL_MT_SMS_BC_CMD,		"SMS_BC_CMD" },
	{ RSL_MT_CBCH_LOAD_IND,		"CBCH_LOAD_IND" },
	{ RSL_MT_NOT_CMD,		"NOTIFY_CMD" },

	{ RSL_MT_CHAN_ACTIV,		"CHAN_ACTIV" },
	{ RSL_MT_CHAN_ACTIV_ACK,	"CHAN_ACTIV_ACK" },
	{ RSL_MT_CHAN_ACTIV_NACK,	"CHAN_ACTIV_NACK" },
	{ RSL_MT_CONN_FAIL,		"CONN_FAIL" },
	{ RSL_MT_DEACTIVATE_SACCH,	"DEACTIVATE_SACCH" },
	{ RSL_MT_ENCR_CMD,		"ENCR_CMD" },
	{ RSL_MT_HANDO_DET,		"HANDOVER_DETECT" },
	{ RSL_MT_MEAS_RES,		"MEAS_RES" },
	{ RSL_MT_MODE_MODIFY_REQ,	"MODE_MODIFY_REQ" },
	{ RSL_MT_MODE_MODIFY_ACK,	"MODE_MODIFY_ACK" },
	{ RSL_MT_MODE_MODIFY_NACK,	"MODE_MODIFY_NACK" },
	{ RSL_MT_PHY_CONTEXT_REQ,	"PHY_CONTEXT_REQ" },
	{ RSL_MT_PHY_CONTEXT_CONF,	"PHY_CONTEXT_CONF" },
	{ RSL_MT_RF_CHAN_REL,		"RF_CHAN_REL" },
	{ RSL_MT_MS_POWER_CONTROL,	"MS_POWER_CONTROL" },
	{ RSL_MT_BS_POWER_CONTROL,	"BS_POWER_CONTROL" },
	{ RSL_MT_PREPROC_CONFIG,	"PREPROC_CONFIG" },
	{ RSL_MT_PREPROC_MEAS_RES,	"PREPROC_MEAS_RES" },
	{ RSL_MT_RF_CHAN_REL_ACK,	"RF_CHAN_REL_ACK" },
	{ RSL_MT_SACCH_INFO_MODIFY,	"SACCH_INFO_MODIFY" },
	{ RSL_MT_TALKER_DET,		"TALKER_DETECT" },
	{ RSL_MT_LISTENER_DET,		"LISTENER_DETECT" },
	{ RSL_MT_REMOTE_CODEC_CONF_REP,	"REM_CODEC_CONF_REP" },
	{ RSL_MT_RTD_REP,		"RTD_REQ" },
	{ RSL_MT_PRE_HANDO_NOTIF,	"HANDO_NOTIF" },
	{ RSL_MT_MR_CODEC_MOD_REQ,	"CODEC_MOD_REQ" },
	{ RSL_MT_MR_CODEC_MOD_ACK,	"CODEC_MOD_ACK" },
	{ RSL_MT_MR_CODEC_MOD_NACK,	"CODEC_MOD_NACK" },
	{ RSL_MT_MR_CODEC_MOD_PER,	"CODEC_MODE_PER" },
	{ RSL_MT_TFO_REP,		"TFO_REP" },
	{ RSL_MT_TFO_MOD_REQ,		"TFO_MOD_REQ" },
	{ RSL_MT_LOCATION_INFO,		"LOCATION_INFO" },
	{ 0,				NULL }
};


/*! \brief Get human-readable string for RSL Message Type */
const char *rsl_msg_name(uint8_t msg_type)
{
	return get_value_string(rsl_msgt_names, msg_type);
}

/*! \brief ip.access specific */
static const struct value_string rsl_ipac_msgt_names[] = {
	{ RSL_MT_IPAC_PDCH_ACT,		"IPAC_PDCH_ACT" },
	{ RSL_MT_IPAC_PDCH_ACT_ACK,	"IPAC_PDCH_ACT_ACK" },
	{ RSL_MT_IPAC_PDCH_ACT_NACK,	"IPAC_PDCH_ACT_NACK" },
	{ RSL_MT_IPAC_PDCH_DEACT,	"IPAC_PDCH_DEACT" },
	{ RSL_MT_IPAC_PDCH_DEACT_ACK,	"IPAC_PDCH_DEACT_ACK" },
	{ RSL_MT_IPAC_PDCH_DEACT_NACK,	"IPAC_PDCH_DEACT_NACK" },
	{ RSL_MT_IPAC_CONNECT_MUX,	"IPAC_CONNECT_MUX" },
	{ RSL_MT_IPAC_CONNECT_MUX_ACK,	"IPAC_CONNECT_MUX_ACK" },
	{ RSL_MT_IPAC_CONNECT_MUX_NACK,	"IPAC_CONNECT_MUX_NACK" },
	{ RSL_MT_IPAC_BIND_MUX,		"IPAC_BIND_MUX" },
	{ RSL_MT_IPAC_BIND_MUX_ACK,	"IPAC_BIND_MUX_ACK" },
	{ RSL_MT_IPAC_BIND_MUX_NACK,	"IPAC_BIND_MUX_NACK" },
	{ RSL_MT_IPAC_DISC_MUX,		"IPAC_DISC_MUX" },
	{ RSL_MT_IPAC_DISC_MUX_ACK,	"IPAC_DISC_MUX_ACK" },
	{ RSL_MT_IPAC_DISC_MUX_NACK,	"IPAC_DISC_MUX_NACK" },
	{ RSL_MT_IPAC_CRCX,		"IPAC_CRCX" },
	{ RSL_MT_IPAC_CRCX_ACK,		"IPAC_CRCX_ACK" },
	{ RSL_MT_IPAC_CRCX_NACK,	"IPAC_CRCX_NACK" },
	{ RSL_MT_IPAC_MDCX,		"IPAC_MDCX" },
	{ RSL_MT_IPAC_MDCX_ACK,		"IPAC_MDCX_ACK" },
	{ RSL_MT_IPAC_MDCX_NACK,	"IPAC_MDCX_NACK" },
	{ RSL_MT_IPAC_DLCX_IND,		"IPAC_DLCX_IND" },
	{ RSL_MT_IPAC_DLCX,		"IPAC_DLCX" },
	{ RSL_MT_IPAC_DLCX_ACK,		"IPAC_DLCX_ACK" },
	{ RSL_MT_IPAC_DLCX_NACK,	"IPAC_DLCX_NACK" },
	{ 0, NULL }
};

/*! \brief Get human-readable name of ip.access RSL msg type */
const char *rsl_ipac_msg_name(uint8_t msg_type)
{
	return get_value_string(rsl_ipac_msgt_names, msg_type);
}

static const struct value_string rsl_rlm_cause_strs[] = {
	{ RLL_CAUSE_T200_EXPIRED,	"Timer T200 expired (N200+1) times" },
	{ RLL_CAUSE_REEST_REQ,		"Re-establishment request" },
	{ RLL_CAUSE_UNSOL_UA_RESP,	"Unsolicited UA response" },
	{ RLL_CAUSE_UNSOL_DM_RESP,	"Unsolicited DM response" },
	{ RLL_CAUSE_UNSOL_DM_RESP_MF,	"Unsolicited DM response, multiple frame" },
	{ RLL_CAUSE_UNSOL_SPRV_RESP,	"Unsolicited supervisory response" },
	{ RLL_CAUSE_SEQ_ERR,		"Sequence Error" },
	{ RLL_CAUSE_UFRM_INC_PARAM,	"U-Frame with incorrect parameters" },
	{ RLL_CAUSE_SFRM_INC_PARAM,	"S-Frame with incorrect parameters" },
	{ RLL_CAUSE_IFRM_INC_MBITS,	"I-Frame with incorrect use of M bit" },
	{ RLL_CAUSE_IFRM_INC_LEN,	"I-Frame with incorrect length" },
	{ RLL_CAUSE_FRM_UNIMPL,		"Frame not implemented" },
	{ RLL_CAUSE_SABM_MF,		"SABM command, multiple frame established state" },
	{ RLL_CAUSE_SABM_INFO_NOTALL,	"SABM frame with information not allowed in this state" },
	{ 0,				NULL },
};

/*! \brief Get human-readable string for RLM cause */
const char *rsl_rlm_cause_name(uint8_t err)
{
	return get_value_string(rsl_rlm_cause_strs, err);
}

/* Section 3.3.2.3 TS 05.02. I think this looks like a table */
int rsl_ccch_conf_to_bs_cc_chans(int ccch_conf)
{
	switch (ccch_conf) {
	case RSL_BCCH_CCCH_CONF_1_NC:
		return 1;
	case RSL_BCCH_CCCH_CONF_1_C:
		return 1;
	case RSL_BCCH_CCCH_CONF_2_NC:
		return 2;
	case RSL_BCCH_CCCH_CONF_3_NC:
		return 3;
	case RSL_BCCH_CCCH_CONF_4_NC:
		return 4;
	default:
		return -1;
	}
}

/* Section 3.3.2.3 TS 05.02 */
int rsl_ccch_conf_to_bs_ccch_sdcch_comb(int ccch_conf)
{
	switch (ccch_conf) {
	case RSL_BCCH_CCCH_CONF_1_NC:
		return 0;
	case RSL_BCCH_CCCH_CONF_1_C:
		return 1;
	case RSL_BCCH_CCCH_CONF_2_NC:
		return 0;
	case RSL_BCCH_CCCH_CONF_3_NC:
		return 0;
	case RSL_BCCH_CCCH_CONF_4_NC:
		return 0;
	default:
		return -1;
	}
}

/*! \brief Push a RSL RLL header onto an existing msgb */
void rsl_rll_push_hdr(struct msgb *msg, uint8_t msg_type, uint8_t chan_nr,
		      uint8_t link_id, int transparent)
{
	struct abis_rsl_rll_hdr *rh;

	rh = (struct abis_rsl_rll_hdr *) msgb_push(msg, sizeof(*rh));
	rsl_init_rll_hdr(rh, msg_type);
	if (transparent)
		rh->c.msg_discr |= ABIS_RSL_MDISC_TRANSP;
	rh->chan_nr = chan_nr;
	rh->link_id = link_id;

	/* set the l2 header pointer */
	msg->l2h = (uint8_t *)rh;
}

/*! \brief Push a RSL RLL header with L3_INFO IE */
void rsl_rll_push_l3(struct msgb *msg, uint8_t msg_type, uint8_t chan_nr,
		     uint8_t link_id, int transparent)
{
	uint8_t l3_len = msg->tail - (uint8_t *)msgb_l3(msg);

	/* construct a RSLms RLL message (DATA INDICATION, UNIT DATA
	 * INDICATION) and send it off via RSLms */

	/* Push the L3 IE tag and lengh */
	msgb_tv16_push(msg, RSL_IE_L3_INFO, l3_len);

	/* Then push the RSL header */
	rsl_rll_push_hdr(msg, msg_type, chan_nr, link_id, transparent);
}

/*! \brief Create msgb with RSL RLL header */
struct msgb *rsl_rll_simple(uint8_t msg_type, uint8_t chan_nr,
			    uint8_t link_id, int transparent)
{
	struct abis_rsl_rll_hdr *rh;
	struct msgb *msg;

	msg = msgb_alloc_headroom(RSL_ALLOC_SIZE+RSL_ALLOC_HEADROOM,
				  RSL_ALLOC_HEADROOM, "rsl_rll_simple");

	if (!msg)
		return NULL;

	/* put the RSL header */
	rh = (struct abis_rsl_rll_hdr *) msgb_put(msg, sizeof(*rh));
	rsl_init_rll_hdr(rh, msg_type);
	if (transparent)
		rh->c.msg_discr |= ABIS_RSL_MDISC_TRANSP;
	rh->chan_nr = chan_nr;
	rh->link_id = link_id;

	/* set the l2 header pointer */
	msg->l2h = (uint8_t *)rh;

	return msg;
}

const struct tlv_definition rsl_ipac_eie_tlvdef = {
	.def = {
		[RSL_IPAC_EIE_RXLEV]		= { TLV_TYPE_TV },
		[RSL_IPAC_EIE_RXQUAL]		= { TLV_TYPE_TV },
		[RSL_IPAC_EIE_FREQ_ERR]		= { TLV_TYPE_FIXED, 2 },
		[RSL_IPAC_EIE_TIMING_ERR]	= { TLV_TYPE_TV },
		[RSL_IPAC_EIE_MEAS_AVG_CFG]	= { TLV_TYPE_TLV },
		[RSL_IPAC_EIE_BS_PWR_CTL]	= { TLV_TYPE_FIXED, 3 },
		[RSL_IPAC_EIE_MS_PWR_CTL]	= { TLV_TYPE_FIXED, 3 },
		[RSL_IPAC_EIE_HANDO_THRESH]	= { TLV_TYPE_FIXED, 6 },
		[RSL_IPAC_EIE_NCELL_DEFAULTS]	= { TLV_TYPE_FIXED, 3 },
		[RSL_IPAC_EIE_NCELL_LIST]	= { TLV_TYPE_TLV },
		[RSL_IPAC_EIE_PC_THRESH_COMP]	= { TLV_TYPE_FIXED, 10 },
		[RSL_IPAC_EIE_HO_THRESH_COMP]	= { TLV_TYPE_FIXED, 10 },
		[RSL_IPAC_EIE_HO_CAUSE]		= { TLV_TYPE_TLV },
		[RSL_IPAC_EIE_HO_CANDIDATES]	= { TLV_TYPE_TLV },
		[RSL_IPAC_EIE_NCELL_BA_CHG_LIST]= { TLV_TYPE_TLV },
		[RSL_IPAC_EIE_NUM_OF_MS]	= { TLV_TYPE_TV },
		[RSL_IPAC_EIE_HO_CAND_EXT]	= { TLV_TYPE_TLV },
		[RSL_IPAC_EIE_NCELL_DEF_EXT]	= { TLV_TYPE_TLV },
		[RSL_IPAC_EIE_NCELL_LIST_EXT]	= { TLV_TYPE_TLV },
		[RSL_IPAC_EIE_MASTER_KEY]	= { TLV_TYPE_TLV },
		[RSL_IPAC_EIE_MASTER_SALT]	= { TLV_TYPE_TLV },
	},
};

/*! @} */
