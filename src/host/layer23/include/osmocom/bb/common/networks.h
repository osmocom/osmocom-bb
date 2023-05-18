#ifndef _NETWORKS_H
#define _NETWORKS_H
#include <osmocom/gsm/gsm23003.h>

/* Instead of handling numerical MCC and MNC, they stored and handled
 * hexadecimal. This makes it possible
 * to correctly handle 2 and 3 digits MNC. Internally 2 digit MNCs are stored
 * as 0xXXf, and 3 digits MNC are stored as 0xXXX, where X is the digit 0..9.
*/
struct gsm_networks {
	uint16_t	mcc_hex;
	int16_t		mnc_hex;
	const char 	*name;
};

int gsm_match_mcc(uint16_t mcc, char *imsi);
int gsm_match_mnc(uint16_t mcc, uint16_t mnc, bool mnc_3_digits, char *imsi);
const char *gsm_get_mcc(uint16_t mcc);
const char *gsm_get_mnc(const struct osmo_plmn_id *plmn);
const char *gsm_imsi_mcc(char *imsi);
const char *gsm_imsi_mnc(char *imsi);

uint16_t gsm_mcc_to_hex(uint16_t mcc);
uint16_t gsm_mnc_to_hex(uint16_t mnc, bool mnc_3_digits);

#endif /* _NETWORKS_H */

