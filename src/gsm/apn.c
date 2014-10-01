#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <osmocom/gsm/apn.h>

#define APN_OI_GPRS_FMT	"mnc%03u.mcc%03u.gprs"
#define APN_GPRS_FMT	"%s.mnc%03u.mcc%03u.gprs"

static char apn_strbuf[APN_MAXLEN+1];

char *osmo_apn_qualify(unsigned int mcc, unsigned int mnc, const char *ni)
{
	snprintf(apn_strbuf, sizeof(apn_strbuf)-1, APN_GPRS_FMT,
		ni, mnc, mcc);
	apn_strbuf[sizeof(apn_strbuf)-1] = '\0';

	return apn_strbuf;
}

char *osmo_apn_qualify_from_imsi(const char *imsi,
				 const char *ni, int have_3dig_mnc)
{
	char cbuf[3+1], nbuf[3+1];

	strncpy(cbuf, imsi, 3);
	cbuf[3] = '\0';

	if (have_3dig_mnc) {
		strncpy(nbuf, imsi+3, 3);
		nbuf[3] = '\0';
	} else {
		strncpy(nbuf, imsi+3, 2);
		nbuf[2] = '\0';
	}
	return osmo_apn_qualify(atoi(cbuf), atoi(nbuf), ni);
}
