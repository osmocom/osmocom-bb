#ifndef _OSMOCORE_GSM48_H
#define _OSMOCORE_GSM48_H

#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/gsm48_ie.h>

/* A parsed GPRS routing area */
struct gprs_ra_id {
	uint16_t	mnc;
	uint16_t	mcc;
	uint16_t	lac;
	uint8_t		rac;
};

extern const struct tlv_definition gsm48_att_tlvdef;
extern const struct tlv_definition gsm48_rr_att_tlvdef;
extern const struct tlv_definition gsm48_mm_att_tlvdef;
const char *gsm48_cc_state_name(uint8_t state);
const char *gsm48_cc_msg_name(uint8_t msgtype);
const char *rr_cause_name(uint8_t cause);

int gsm48_decode_lai(struct gsm48_loc_area_id *lai, uint16_t *mcc,
		     uint16_t *mnc, uint16_t *lac);
void gsm48_generate_lai(struct gsm48_loc_area_id *lai48, uint16_t mcc,
			uint16_t mnc, uint16_t lac);
int gsm48_generate_mid_from_tmsi(uint8_t *buf, uint32_t tmsi);
int gsm48_generate_mid_from_imsi(uint8_t *buf, const char *imsi);

/* Convert Mobile Identity (10.5.1.4) to string */
int gsm48_mi_to_string(char *string, const int str_len,
			const uint8_t *mi, const int mi_len);

/* Parse Routeing Area Identifier */
void gsm48_parse_ra(struct gprs_ra_id *raid, const uint8_t *buf);
int gsm48_construct_ra(uint8_t *buf, const struct gprs_ra_id *raid);

int gsm48_number_of_paging_subchannels(struct gsm48_control_channel_descr *chan_desc);

#endif
