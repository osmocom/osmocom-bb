#ifndef _OSMOCORE_RSL_H
#define _OSMOCORE_RSL_H

#include <stdint.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/msgb.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

/*! \defgroup rsl RSL
 *  @{
 */

/*! \file rsl.h */

void rsl_init_rll_hdr(struct abis_rsl_rll_hdr *dh, uint8_t msg_type);

void rsl_init_cchan_hdr(struct abis_rsl_cchan_hdr *ch, uint8_t msg_type);

extern const struct tlv_definition rsl_att_tlvdef;

/*! \brief Parse RSL TLV structure using \ref tlv_parse */
#define rsl_tlv_parse(dec, buf, len)     \
			tlv_parse(dec, &rsl_att_tlvdef, buf, len, 0, 0)

/* encode channel number as per Section 9.3.1 */
uint8_t rsl_enc_chan_nr(uint8_t type, uint8_t subch, uint8_t timeslot);
/* decode channel number as per Section 9.3.1 */
int rsl_dec_chan_nr(uint8_t chan_nr, uint8_t *type, uint8_t *subch, uint8_t *timeslot);
/* Turns channel number into a string */
const char *rsl_chan_nr_str(uint8_t chan_nr);


const char *rsl_err_name(uint8_t err);
const char *rsl_rlm_cause_name(uint8_t err);
const char *rsl_msg_name(uint8_t err);
const char *rsl_ipac_msg_name(uint8_t msg_type);

/* Section 3.3.2.3 TS 05.02. I think this looks like a table */
int rsl_ccch_conf_to_bs_cc_chans(int ccch_conf);

/* Push a RSL RLL header */
void rsl_rll_push_hdr(struct msgb *msg, uint8_t msg_type, uint8_t chan_nr,
		      uint8_t link_id, int transparent);

/* Push a RSL RLL header with L3_INFO IE */
void rsl_rll_push_l3(struct msgb *msg, uint8_t msg_type, uint8_t chan_nr,
		     uint8_t link_id, int transparent);

/* Allocate msgb and fill with simple RSL RLL header */
struct msgb *rsl_rll_simple(uint8_t msg_type, uint8_t chan_nr,
			    uint8_t link_id, int transparent);

/*! @} */

#endif /* _OSMOCORE_RSL_H */
