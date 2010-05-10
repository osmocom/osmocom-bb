#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <osmocom/networks.h>

/* list of networks */

struct gsm_networks gsm_networks[] = {
	{ 1, 0,		"Test" },
	{ 1, 1,			"Test" },
	{ 262, 0,	"Germany" },
	{ 262, 1,		"T-Mobile" },
	{ 262, 2,		"Vodafone" },
	{ 262, 3,		"E-Plus" },
	{ 262, 7,		"O2" },
	{ 262, 10,		"DB Systel GSM-R" },
	{ 262, 42,		"OpenBSC" },
	{ 238, 0,	"Denmark" },
	{ 238, 1,		"TDC Mobil" },
	{ 238, 2,		"Sonofon" },
	{ 238, 3,		"MIGway A/S" },
	{ 238, 6,		"HiG3" },
	{ 238, 7,		"Barablue Mobile Ltd." },
	{ 238, 10,		"TDC Mobil" },
	{ 238, 12,		"Lycamobile Denmark" },
	{ 238, 20,		"Telia" },
	{ 238, 30,		"Telia Mobile" },
	{ 238, 77,		"Tele2" },
	{ 0, 0, NULL }
};

const char *gsm_get_mcc(uint16_t mcc)
{
	int i;
	static char unknown[4] = "000";

	for (i = 0; gsm_networks[i].name; i++)
		if (!gsm_networks[i].mnc && gsm_networks[i].mcc == mcc)
			return gsm_networks[i].name;

	snprintf(unknown, 3, "%03d", mcc);
	return unknown;
}

const char *gsm_get_mnc(uint16_t mcc, uint16_t mnc)
{
	int i;
	static char unknown[4] = "000";

	for (i = 0; gsm_networks[i].name; i++)
		if (gsm_networks[i].mcc == mcc && gsm_networks[i].mnc == mnc)
			return gsm_networks[i].name;

	snprintf(unknown, 3, "%02d", mnc);
	return unknown;
}


