#ifndef _OSMO_GSM_ABIS_NM_H
#define _OSMO_GSM_ABIS_NM_H

/*! \defgroup oml A-bis OML
 *  @{
 */

#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_12_21.h>

/*! \file abis_nm.h */

enum abis_nm_msgtype;
enum gsm_phys_chan_config;

extern const enum abis_nm_msgtype abis_nm_reports[4];
extern const enum abis_nm_msgtype abis_nm_no_ack_nack[3];
extern const enum abis_nm_msgtype abis_nm_sw_load_msgs[9];
extern const enum abis_nm_msgtype abis_nm_nacks[33];

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
void abis_nm_debugp_foh(int ss, struct abis_om_fom_hdr *foh);

int abis_nm_chcomb4pchan(enum gsm_phys_chan_config pchan);
enum abis_nm_chan_comb abis_nm_pchan4chcomb(uint8_t chcomb);

/*! @} */

#endif /* _OSMO_GSM_ABIS_NM_H */
