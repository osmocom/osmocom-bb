/* GSM Network Management (OML) messages on the A-bis interface
 * 3GPP TS 12.21 version 8.0.0 Release 1999 / ETSI TS 100 623 V8.0.0 */

/* (C) 2008-2011 by Harald Welte <laforge@gnumonks.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*! \addtogroup oml
 *  @{
 */

/*! \file abis_nm.c */

#include <stdint.h>
#include <errno.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/logging.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_12_21.h>
#include <osmocom/gsm/abis_nm.h>

/*! \brief unidirectional messages from BTS to BSC */
const enum abis_nm_msgtype abis_nm_reports[4] = {
	NM_MT_SW_ACTIVATED_REP,
	NM_MT_TEST_REP,
	NM_MT_STATECHG_EVENT_REP,
	NM_MT_FAILURE_EVENT_REP,
};

/*! \brief messages without ACK/NACK */
const enum abis_nm_msgtype abis_nm_no_ack_nack[3] = {
	NM_MT_MEAS_RES_REQ,
	NM_MT_STOP_MEAS,
	NM_MT_START_MEAS,
};

/*! \brief messages related to software load */
const enum abis_nm_msgtype abis_nm_sw_load_msgs[9] = {
	NM_MT_LOAD_INIT_ACK,
	NM_MT_LOAD_INIT_NACK,
	NM_MT_LOAD_SEG_ACK,
	NM_MT_LOAD_ABORT,
	NM_MT_LOAD_END_ACK,
	NM_MT_LOAD_END_NACK,
	//NM_MT_SW_ACT_REQ,
	NM_MT_ACTIVATE_SW_ACK,
	NM_MT_ACTIVATE_SW_NACK,
	NM_MT_SW_ACTIVATED_REP,
};

/*! \brief All NACKs (negative acknowledgements */
const enum abis_nm_msgtype abis_nm_nacks[33] = {
	NM_MT_LOAD_INIT_NACK,
	NM_MT_LOAD_END_NACK,
	NM_MT_SW_ACT_REQ_NACK,
	NM_MT_ACTIVATE_SW_NACK,
	NM_MT_ESTABLISH_TEI_NACK,
	NM_MT_CONN_TERR_SIGN_NACK,
	NM_MT_DISC_TERR_SIGN_NACK,
	NM_MT_CONN_TERR_TRAF_NACK,
	NM_MT_DISC_TERR_TRAF_NACK,
	NM_MT_CONN_MDROP_LINK_NACK,
	NM_MT_DISC_MDROP_LINK_NACK,
	NM_MT_SET_BTS_ATTR_NACK,
	NM_MT_SET_RADIO_ATTR_NACK,
	NM_MT_SET_CHAN_ATTR_NACK,
	NM_MT_PERF_TEST_NACK,
	NM_MT_SEND_TEST_REP_NACK,
	NM_MT_STOP_TEST_NACK,
	NM_MT_STOP_EVENT_REP_NACK,
	NM_MT_REST_EVENT_REP_NACK,
	NM_MT_CHG_ADM_STATE_NACK,
	NM_MT_CHG_ADM_STATE_REQ_NACK,
	NM_MT_REP_OUTST_ALARMS_NACK,
	NM_MT_CHANGEOVER_NACK,
	NM_MT_OPSTART_NACK,
	NM_MT_REINIT_NACK,
	NM_MT_SET_SITE_OUT_NACK,
	NM_MT_CHG_HW_CONF_NACK,
	NM_MT_GET_ATTR_NACK,
	NM_MT_SET_ALARM_THRES_NACK,
	NM_MT_BS11_BEGIN_DB_TX_NACK,
	NM_MT_BS11_END_DB_TX_NACK,
	NM_MT_BS11_CREATE_OBJ_NACK,
	NM_MT_BS11_DELETE_OBJ_NACK,
};

static const struct value_string nack_names[] = {
	{ NM_MT_LOAD_INIT_NACK,		"SOFTWARE LOAD INIT" },
	{ NM_MT_LOAD_END_NACK,		"SOFTWARE LOAD END" },
	{ NM_MT_SW_ACT_REQ_NACK,	"SOFTWARE ACTIVATE REQUEST" },
	{ NM_MT_ACTIVATE_SW_NACK,	"ACTIVATE SOFTWARE" },
	{ NM_MT_ESTABLISH_TEI_NACK,	"ESTABLISH TEI" },
	{ NM_MT_CONN_TERR_SIGN_NACK,	"CONNECT TERRESTRIAL SIGNALLING" },
	{ NM_MT_DISC_TERR_SIGN_NACK,	"DISCONNECT TERRESTRIAL SIGNALLING" },
	{ NM_MT_CONN_TERR_TRAF_NACK,	"CONNECT TERRESTRIAL TRAFFIC" },
	{ NM_MT_DISC_TERR_TRAF_NACK,	"DISCONNECT TERRESTRIAL TRAFFIC" },
	{ NM_MT_CONN_MDROP_LINK_NACK,	"CONNECT MULTI-DROP LINK" },
	{ NM_MT_DISC_MDROP_LINK_NACK,	"DISCONNECT MULTI-DROP LINK" },
	{ NM_MT_SET_BTS_ATTR_NACK,	"SET BTS ATTRIBUTE" },
	{ NM_MT_SET_RADIO_ATTR_NACK,	"SET RADIO ATTRIBUTE" },
	{ NM_MT_SET_CHAN_ATTR_NACK,	"SET CHANNEL ATTRIBUTE" },
	{ NM_MT_PERF_TEST_NACK,		"PERFORM TEST" },
	{ NM_MT_SEND_TEST_REP_NACK,	"SEND TEST REPORT" },
	{ NM_MT_STOP_TEST_NACK,		"STOP TEST" },
	{ NM_MT_STOP_EVENT_REP_NACK,	"STOP EVENT REPORT" },
	{ NM_MT_REST_EVENT_REP_NACK,	"RESET EVENT REPORT" },
	{ NM_MT_CHG_ADM_STATE_NACK,	"CHANGE ADMINISTRATIVE STATE" },
	{ NM_MT_CHG_ADM_STATE_REQ_NACK,
				"CHANGE ADMINISTRATIVE STATE REQUEST" },
	{ NM_MT_REP_OUTST_ALARMS_NACK,	"REPORT OUTSTANDING ALARMS" },
	{ NM_MT_CHANGEOVER_NACK,	"CHANGEOVER" },
	{ NM_MT_OPSTART_NACK,		"OPSTART" },
	{ NM_MT_REINIT_NACK,		"REINIT" },
	{ NM_MT_SET_SITE_OUT_NACK,	"SET SITE OUTPUT" },
	{ NM_MT_CHG_HW_CONF_NACK,	"CHANGE HARDWARE CONFIGURATION" },
	{ NM_MT_GET_ATTR_NACK,		"GET ATTRIBUTE" },
	{ NM_MT_SET_ALARM_THRES_NACK,	"SET ALARM THRESHOLD" },
	{ NM_MT_BS11_BEGIN_DB_TX_NACK,	"BS11 BEGIN DATABASE TRANSMISSION" },
	{ NM_MT_BS11_END_DB_TX_NACK,	"BS11 END DATABASE TRANSMISSION" },
	{ NM_MT_BS11_CREATE_OBJ_NACK,	"BS11 CREATE OBJECT" },
	{ NM_MT_BS11_DELETE_OBJ_NACK,	"BS11 DELETE OBJECT" },
	{ 0,				NULL }
};

/*! \brief Get human-readable string for OML NACK message type */
const char *abis_nm_nack_name(uint8_t nack)
{
	return get_value_string(nack_names, nack);
}

/* Chapter 9.4.36 */
static const struct value_string nack_cause_names[] = {
	/* General Nack Causes */
	{ NM_NACK_INCORR_STRUCT,	"Incorrect message structure" },
	{ NM_NACK_MSGTYPE_INVAL,	"Invalid message type value" },
	{ NM_NACK_OBJCLASS_INVAL,	"Invalid Object class value" },
	{ NM_NACK_OBJCLASS_NOTSUPP,	"Object class not supported" },
	{ NM_NACK_BTSNR_UNKN,		"BTS no. unknown" },
	{ NM_NACK_TRXNR_UNKN,		"Baseband Transceiver no. unknown" },
	{ NM_NACK_OBJINST_UNKN,		"Object Instance unknown" },
	{ NM_NACK_ATTRID_INVAL,		"Invalid attribute identifier value" },
	{ NM_NACK_ATTRID_NOTSUPP,	"Attribute identifier not supported" },
	{ NM_NACK_PARAM_RANGE,		"Parameter value outside permitted range" },
	{ NM_NACK_ATTRLIST_INCONSISTENT,"Inconsistency in attribute list" },
	{ NM_NACK_SPEC_IMPL_NOTSUPP,	"Specified implementation not supported" },
	{ NM_NACK_CANT_PERFORM,		"Message cannot be performed" },
	/* Specific Nack Causes */
	{ NM_NACK_RES_NOTIMPL,		"Resource not implemented" },
	{ NM_NACK_RES_NOTAVAIL,		"Resource not available" },
	{ NM_NACK_FREQ_NOTAVAIL,	"Frequency not available" },
	{ NM_NACK_TEST_NOTSUPP,		"Test not supported" },
	{ NM_NACK_CAPACITY_RESTR,	"Capacity restrictions" },
	{ NM_NACK_PHYSCFG_NOTPERFORM,	"Physical configuration cannot be performed" },
	{ NM_NACK_TEST_NOTINIT,		"Test not initiated" },
	{ NM_NACK_PHYSCFG_NOTRESTORE,	"Physical configuration cannot be restored" },
	{ NM_NACK_TEST_NOSUCH,		"No such test" },
	{ NM_NACK_TEST_NOSTOP,		"Test cannot be stopped" },
	{ NM_NACK_MSGINCONSIST_PHYSCFG,	"Message inconsistent with physical configuration" },
	{ NM_NACK_FILE_INCOMPLETE,	"Complete file notreceived" },
	{ NM_NACK_FILE_NOTAVAIL,	"File not available at destination" },
	{ NM_NACK_FILE_NOTACTIVATE,	"File cannot be activate" },
	{ NM_NACK_REQ_NOT_GRANT,	"Request not granted" },
	{ NM_NACK_WAIT,			"Wait" },
	{ NM_NACK_NOTH_REPORT_EXIST,	"Nothing reportable existing" },
	{ NM_NACK_MEAS_NOTSUPP,		"Measurement not supported" },
	{ NM_NACK_MEAS_NOTSTART,	"Measurement not started" },
	{ 0,				NULL }
};

/*! \brief Get human-readable string for NACK cause */
const char *abis_nm_nack_cause_name(uint8_t cause)
{
	return get_value_string(nack_cause_names, cause);
}

/* Chapter 9.4.16: Event Type */
static const struct value_string event_type_names[] = {
	{ NM_EVT_COMM_FAIL,		"communication failure" },
	{ NM_EVT_QOS_FAIL,		"quality of service failure" },
	{ NM_EVT_PROC_FAIL,		"processing failure" },
	{ NM_EVT_EQUIP_FAIL,		"equipment failure" },
	{ NM_EVT_ENV_FAIL,		"environment failure" },
	{ 0,				NULL }
};

/*! \brief Get human-readable string for OML event type */
const char *abis_nm_event_type_name(uint8_t cause)
{
	return get_value_string(event_type_names, cause);
}

/* Chapter 9.4.63: Perceived Severity */
static const struct value_string severity_names[] = {
	{ NM_SEVER_CEASED,		"failure ceased" },
	{ NM_SEVER_CRITICAL,		"critical failure" },
	{ NM_SEVER_MAJOR,		"major failure" },
	{ NM_SEVER_MINOR,		"minor failure" },
	{ NM_SEVER_WARNING,		"warning level failure" },
	{ NM_SEVER_INDETERMINATE,	"indeterminate failure" },
	{ 0,				NULL }
};

/*! \brief Get human-readable string for perceived OML severity */
const char *abis_nm_severity_name(uint8_t cause)
{
	return get_value_string(severity_names, cause);
}

/*! \brief Attributes that the BSC can set, not only get, according to Section 9.4 */
const enum abis_nm_attr abis_nm_att_settable[] = {
	NM_ATT_ADD_INFO,
	NM_ATT_ADD_TEXT,
	NM_ATT_DEST,
	NM_ATT_EVENT_TYPE,
	NM_ATT_FILE_DATA,
	NM_ATT_GET_ARI,
	NM_ATT_HW_CONF_CHG,
	NM_ATT_LIST_REQ_ATTR,
	NM_ATT_MDROP_LINK,
	NM_ATT_MDROP_NEXT,
	NM_ATT_NACK_CAUSES,
	NM_ATT_OUTST_ALARM,
	NM_ATT_PHYS_CONF,
	NM_ATT_PROB_CAUSE,
	NM_ATT_RAD_SUBC,
	NM_ATT_SOURCE,
	NM_ATT_SPEC_PROB,
	NM_ATT_START_TIME,
	NM_ATT_TEST_DUR,
	NM_ATT_TEST_NO,
	NM_ATT_TEST_REPORT,
	NM_ATT_WINDOW_SIZE,
	NM_ATT_SEVERITY,
	NM_ATT_MEAS_RES,
	NM_ATT_MEAS_TYPE,
};

/*! \brief GSM A-bis OML TLV parser definition */
const struct tlv_definition abis_nm_att_tlvdef = {
	.def = {
		[NM_ATT_ABIS_CHANNEL] =		{ TLV_TYPE_FIXED, 3 },
		[NM_ATT_ADD_INFO] =		{ TLV_TYPE_TL16V },
		[NM_ATT_ADD_TEXT] =		{ TLV_TYPE_TL16V },
		[NM_ATT_ADM_STATE] =		{ TLV_TYPE_TV },
		[NM_ATT_ARFCN_LIST]=		{ TLV_TYPE_TL16V },
		[NM_ATT_AUTON_REPORT] =		{ TLV_TYPE_TV },
		[NM_ATT_AVAIL_STATUS] =		{ TLV_TYPE_TL16V },
		[NM_ATT_BCCH_ARFCN] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_BSIC] =			{ TLV_TYPE_TV },
		[NM_ATT_BTS_AIR_TIMER] =	{ TLV_TYPE_TV },
		[NM_ATT_CCCH_L_I_P] =		{ TLV_TYPE_TV },
		[NM_ATT_CCCH_L_T] =		{ TLV_TYPE_TV },
		[NM_ATT_CHAN_COMB] =		{ TLV_TYPE_TV },
		[NM_ATT_CONN_FAIL_CRIT] =	{ TLV_TYPE_TL16V },
		[NM_ATT_DEST] =			{ TLV_TYPE_TL16V },
		[NM_ATT_EVENT_TYPE] =		{ TLV_TYPE_TV },
		[NM_ATT_FILE_DATA] =		{ TLV_TYPE_TL16V },
		[NM_ATT_FILE_ID] =		{ TLV_TYPE_TL16V },
		[NM_ATT_FILE_VERSION] =		{ TLV_TYPE_TL16V },
		[NM_ATT_GSM_TIME] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_HSN] =			{ TLV_TYPE_TV },
		[NM_ATT_HW_CONFIG] =		{ TLV_TYPE_TL16V },
		[NM_ATT_HW_DESC] =		{ TLV_TYPE_TL16V },
		[NM_ATT_INTAVE_PARAM] =		{ TLV_TYPE_TV },
		[NM_ATT_INTERF_BOUND] =		{ TLV_TYPE_FIXED, 6 },
		[NM_ATT_LIST_REQ_ATTR] =	{ TLV_TYPE_TL16V },
		[NM_ATT_MAIO] =			{ TLV_TYPE_TV },
		[NM_ATT_MANUF_STATE] =		{ TLV_TYPE_TV },
		[NM_ATT_MANUF_THRESH] =		{ TLV_TYPE_TL16V },
		[NM_ATT_MANUF_ID] =		{ TLV_TYPE_TL16V },
		[NM_ATT_MAX_TA] =		{ TLV_TYPE_TV },
		[NM_ATT_MDROP_LINK] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_MDROP_NEXT] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_NACK_CAUSES] =		{ TLV_TYPE_TV },
		[NM_ATT_NY1] =			{ TLV_TYPE_TV },
		[NM_ATT_OPER_STATE] =		{ TLV_TYPE_TV },
		[NM_ATT_OVERL_PERIOD] =		{ TLV_TYPE_TL16V },
		[NM_ATT_PHYS_CONF] =		{ TLV_TYPE_TL16V },
		[NM_ATT_POWER_CLASS] =		{ TLV_TYPE_TV },
		[NM_ATT_POWER_THRESH] =		{ TLV_TYPE_FIXED, 3 },
		[NM_ATT_PROB_CAUSE] =		{ TLV_TYPE_FIXED, 3 },
		[NM_ATT_RACH_B_THRESH] =	{ TLV_TYPE_TV },
		[NM_ATT_LDAVG_SLOTS] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_RAD_SUBC] =		{ TLV_TYPE_TV },
		[NM_ATT_RF_MAXPOWR_R] =		{ TLV_TYPE_TV },
		[NM_ATT_SITE_INPUTS] =		{ TLV_TYPE_TL16V },
		[NM_ATT_SITE_OUTPUTS] =		{ TLV_TYPE_TL16V },
		[NM_ATT_SOURCE] =		{ TLV_TYPE_TL16V },
		[NM_ATT_SPEC_PROB] =		{ TLV_TYPE_TV },
		[NM_ATT_START_TIME] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_T200] =			{ TLV_TYPE_FIXED, 7 },
		[NM_ATT_TEI] =			{ TLV_TYPE_TV },
		[NM_ATT_TEST_DUR] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_TEST_NO] =		{ TLV_TYPE_TV },
		[NM_ATT_TEST_REPORT] =		{ TLV_TYPE_TL16V },
		[NM_ATT_VSWR_THRESH] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_WINDOW_SIZE] = 		{ TLV_TYPE_TV },
		[NM_ATT_TSC] =			{ TLV_TYPE_TV },
		[NM_ATT_SW_CONFIG] =		{ TLV_TYPE_TL16V },
		[NM_ATT_SEVERITY] = 		{ TLV_TYPE_TV },
		[NM_ATT_GET_ARI] =		{ TLV_TYPE_TL16V },
		[NM_ATT_HW_CONF_CHG] = 		{ TLV_TYPE_TL16V },
		[NM_ATT_OUTST_ALARM] =		{ TLV_TYPE_TV },
		[NM_ATT_MEAS_RES] =		{ TLV_TYPE_TL16V },
	},
};

/*! \brief Human-readable strings for A-bis OML Object Class */
const struct value_string abis_nm_obj_class_names[] = {
	{ NM_OC_SITE_MANAGER,	"SITE-MANAGER" },
	{ NM_OC_BTS,		"BTS" },
	{ NM_OC_RADIO_CARRIER,	"RADIO-CARRIER" },
	{ NM_OC_BASEB_TRANSC,	"BASEBAND-TRANSCEIVER" },
	{ NM_OC_CHANNEL,	"CHANNEL" },
	{ NM_OC_BS11_ADJC,	"ADJC" },
	{ NM_OC_BS11_HANDOVER,	"HANDOVER" },
	{ NM_OC_BS11_PWR_CTRL,	"POWER-CONTROL" },
	{ NM_OC_BS11_BTSE,	"BTSE" },
	{ NM_OC_BS11_RACK,	"RACK" },
	{ NM_OC_BS11_TEST,	"TEST" },
	{ NM_OC_BS11_ENVABTSE,	"ENVABTSE" },
	{ NM_OC_BS11_BPORT,	"BPORT" },
	{ NM_OC_GPRS_NSE,	"GPRS-NSE" },
	{ NM_OC_GPRS_CELL,	"GPRS-CELL" },
	{ NM_OC_GPRS_NSVC,	"GPRS-NSVC" },
	{ NM_OC_BS11,		"SIEMENSHW" },
	{ 0,			NULL }
};

/*! \brief Get human-readable string for OML Operational State */
const char *abis_nm_opstate_name(uint8_t os)
{
	switch (os) {
	case NM_OPSTATE_DISABLED:
		return "Disabled";
	case NM_OPSTATE_ENABLED:
		return "Enabled";
	case NM_OPSTATE_NULL:
		return "NULL";
	default:
		return "RFU";
	}
}

/* Chapter 9.4.7 */
static const struct value_string avail_names[] = {
	{ 0, 	"In test" },
	{ 1,	"Failed" },
	{ 2,	"Power off" },
	{ 3,	"Off line" },
	/* Not used */
	{ 5,	"Dependency" },
	{ 6,	"Degraded" },
	{ 7,	"Not installed" },
	{ 0xff, "OK" },
	{ 0,	NULL }
};

/*! \brief Get human-readable string for OML Availability State */
const char *abis_nm_avail_name(uint8_t avail)
{
	return get_value_string(avail_names, avail);
}

static struct value_string test_names[] = {
	/* FIXME: standard test names */
	{ NM_IPACC_TESTNO_CHAN_USAGE, "Channel Usage" },
	{ NM_IPACC_TESTNO_BCCH_CHAN_USAGE, "BCCH Channel Usage" },
	{ NM_IPACC_TESTNO_FREQ_SYNC, "Frequency Synchronization" },
	{ NM_IPACC_TESTNO_BCCH_INFO, "BCCH Info" },
	{ NM_IPACC_TESTNO_TX_BEACON, "Transmit Beacon" },
	{ NM_IPACC_TESTNO_SYSINFO_MONITOR, "System Info Monitor" },
	{ NM_IPACC_TESTNO_BCCCH_MONITOR, "BCCH Monitor" },
	{ 0, NULL }
};

/*! \brief Get human-readable string for OML test */
const char *abis_nm_test_name(uint8_t test)
{
	return get_value_string(test_names, test);
}

/*! \brief Human-readable names for OML administrative state */
const struct value_string abis_nm_adm_state_names[] = {
	{ NM_STATE_LOCKED,	"Locked" },
	{ NM_STATE_UNLOCKED,	"Unlocked" },
	{ NM_STATE_SHUTDOWN,	"Shutdown" },
	{ NM_STATE_NULL,	"NULL" },
	{ 0, NULL }
};

/*! \brief write a human-readable OML header to the debug log
 *  \param[in] ss Logging sub-system
 *  \param[in] foh A-bis OML FOM header
 */
void abis_nm_debugp_foh(int ss, struct abis_om_fom_hdr *foh)
{
	DEBUGP(ss, "OC=%s(%02x) INST=(%02x,%02x,%02x) ",
		get_value_string(abis_nm_obj_class_names, foh->obj_class),
		foh->obj_class, foh->obj_inst.bts_nr, foh->obj_inst.trx_nr,
		foh->obj_inst.ts_nr);
}

static const enum abis_nm_chan_comb chcomb4pchan[] = {
	[GSM_PCHAN_NONE]	= 0xff,
	[GSM_PCHAN_CCCH]	= NM_CHANC_mainBCCH,
	[GSM_PCHAN_CCCH_SDCCH4]	= NM_CHANC_BCCHComb,
	[GSM_PCHAN_TCH_F]	= NM_CHANC_TCHFull,
	[GSM_PCHAN_TCH_H]	= NM_CHANC_TCHHalf,
	[GSM_PCHAN_SDCCH8_SACCH8C] = NM_CHANC_SDCCH,
	[GSM_PCHAN_PDCH]	= NM_CHANC_IPAC_PDCH,
	[GSM_PCHAN_TCH_F_PDCH]	= NM_CHANC_IPAC_TCHFull_PDCH,
	[GSM_PCHAN_UNKNOWN]	= 0xff,
	/* FIXME: bounds check */
};

/*! \brief Obtain OML Channel Combination for phnsical channel config */
int abis_nm_chcomb4pchan(enum gsm_phys_chan_config pchan)
{
	if (pchan < ARRAY_SIZE(chcomb4pchan))
		return chcomb4pchan[pchan];

	return -EINVAL;
}

/*! \brief Obtain physical channel config for OML Channel Combination */
enum abis_nm_chan_comb abis_nm_pchan4chcomb(uint8_t chcomb)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(chcomb4pchan); i++) {
		if (chcomb4pchan[i] == chcomb)
			return i;
	}
	return GSM_PCHAN_NONE;
}

/*! @} */
