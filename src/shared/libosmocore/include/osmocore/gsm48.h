#ifndef _OSMOCORE_GSM48_H

#include <osmocore/tlv.h>
#include <osmocore/protocol/gsm_04_08.h>
#include <osmocore/gsm48_ie.h>

extern const struct tlv_definition gsm48_att_tlvdef;
const char *gsm48_cc_state_name(uint8_t state);
const char *gsm48_cc_msg_name(uint8_t msgtype);
const char *rr_cause_name(uint8_t cause);

void gsm48_generate_lai(struct gsm48_loc_area_id *lai48, uint16_t mcc,
			uint16_t mnc, uint16_t lac);
int gsm48_generate_mid_from_tmsi(uint8_t *buf, uint32_t tmsi);
int gsm48_generate_mid_from_imsi(uint8_t *buf, const char *imsi);

#endif
