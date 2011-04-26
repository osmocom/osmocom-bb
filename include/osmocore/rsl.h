#ifndef _OSMOCORE_RSL_H
#define _OSMOCORE_RSL_H

#include <stdint.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

void rsl_init_rll_hdr(struct abis_rsl_rll_hdr *dh, uint8_t msg_type);

extern const struct tlv_definition rsl_att_tlvdef;
#define rsl_tlv_parse(dec, buf, len)     \
			tlv_parse(dec, &rsl_att_tlvdef, buf, len, 0, 0)

/* encode channel number as per Section 9.3.1 */
uint8_t rsl_enc_chan_nr(uint8_t type, uint8_t subch, uint8_t timeslot);

const struct value_string rsl_rlm_cause_strs[];

const char *rsl_err_name(uint8_t err);

/* Section 3.3.2.3 TS 05.02. I think this looks like a table */
int rsl_ccch_conf_to_bs_cc_chans(int ccch_conf);

#endif /* _OSMOCORE_RSL_H */
