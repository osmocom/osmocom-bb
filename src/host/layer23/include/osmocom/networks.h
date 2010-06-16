#ifndef _NETWORKS_H
#define _NETWORKS_H

struct gsm_networks {
	uint16_t	mcc;
	int16_t		mnc;
	const char 	*name;
};

const char *gsm_get_mcc(uint16_t mcc);
const char *gsm_get_mnc(uint16_t mcc, uint16_t mnc);

#endif /* _NETWORKS_H */

