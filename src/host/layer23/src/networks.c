#include <stdint.h>
#include <stdio.h>

#include <osmocom/networks.h>

/* list of networks */

struct gsm_networks gsm_networks[] = {
	{ 262, 0,	"Germany" },
	{ 262, 1,		"T-Mobile" },
	{ 262, 2,		"Vodafone" },
	{ 262, 3,		"E-Plus" },
	{ 262, 7,		"O2" },
	{ 0, 0, NULL }
};

const char *gsm_get_mcc(uint16_t mcc)
{
	int i;

	for (i = 0; gsm_networks[i].name; i++)
		if (!gsm_networks[i].mnc && gsm_networks[i].mcc == mcc)
			return gsm_networks[i].name;

	return "unknown";
}

const char *gsm_get_mnc(uint16_t mcc, uint16_t mnc)
{
	int i;

	for (i = 0; gsm_networks[i].name; i++)
		if (gsm_networks[i].mcc == mcc && gsm_networks[i].mnc == mnc)
			return gsm_networks[i].name;

	return "unknown";
}


