#pragma once

/*! \defgroup oml A-bis OML
 *  @{
 */

#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_12_21.h>

/*! \file abis_nm.h */

extern const char abis_nm_ipa_magic[13];
extern const char abis_nm_osmo_magic[12];

enum abis_nm_msgtype;
enum gsm_phys_chan_config;

extern const enum abis_nm_msgtype abis_nm_reports[4];
extern const enum abis_nm_msgtype abis_nm_no_ack_nack[3];
extern const enum abis_nm_msgtype abis_nm_sw_load_msgs[9];
extern const enum abis_nm_msgtype abis_nm_nacks[33];

extern const struct value_string abis_nm_msg_disc_names[];
extern const struct value_string abis_nm_obj_class_names[];
extern const struct value_string abis_nm_adm_state_names[];

const char *abis_nm_nack_cause_name(uint8_t cause);
const char *abis_nm_nack_name(uint8_t nack);
const char *abis_nm_event_type_name(uint8_t cause);
const char *abis_nm_severity_name(uint8_t cause);
extern const struct tlv_definition abis_nm_att_tlvdef;
const char *abis_nm_opstate_name(uint8_t os);
const char *abis_nm_avail_name(uint8_t avail);
const char *abis_nm_test_name(uint8_t test);
extern const struct tlv_definition abis_nm_osmo_att_tlvdef;

/*! \brief write a human-readable OML header to the debug log
 *  \param[in] ss Logging sub-system
 *  \param[in] foh A-bis OML FOM header
 */
#define abis_nm_debugp_foh(ss, foh)					    \
	DEBUGP(ss, "OC=%s(%02x) INST=(%02x,%02x,%02x) ",		    \
		get_value_string(abis_nm_obj_class_names, (foh)->obj_class),  \
		(foh)->obj_class, (foh)->obj_inst.bts_nr, (foh)->obj_inst.trx_nr, \
		(foh)->obj_inst.ts_nr)


int abis_nm_chcomb4pchan(enum gsm_phys_chan_config pchan);
enum gsm_phys_chan_config abis_nm_pchan4chcomb(uint8_t chcomb);

/*! @} */
