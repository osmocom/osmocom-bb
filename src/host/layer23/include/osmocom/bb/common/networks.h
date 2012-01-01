#ifndef _NETWORKS_H
#define _NETWORKS_H

#define GSM_INPUT_INVALID	0xffff

struct gsm_networks {
	uint16_t	mcc;
	int16_t		mnc;
	const char 	*name;
};

int gsm_match_mcc(uint16_t mcc, char *imsi);
int gsm_match_mnc(uint16_t mcc, uint16_t mnc, char *imsi);
const char *gsm_print_mcc(uint16_t mcc);
const char *gsm_print_mnc(uint16_t mcc);
const char *gsm_get_mcc(uint16_t mcc);
const char *gsm_get_mnc(uint16_t mcc, uint16_t mnc);
const char *gsm_imsi_mcc(char *imsi);
const char *gsm_imsi_mnc(char *imsi);
const uint16_t gsm_input_mcc(char *string);
const uint16_t gsm_input_mnc(char *string);

#endif /* _NETWORKS_H */

