#ifndef _OSMOCORE_GSM48_H

#include <osmocore/tlv.h>

extern const struct tlv_definition gsm48_att_tlvdef;
extern const char *cc_state_names[];
const char *rr_cause_name(uint8_t cause);

#endif
