#pragma once

#include <stdint.h>

/* 23.003 Section 9.1.1, excluding any terminating zero byte */
#define APN_NI_MAXLEN	63

/* 23.003 Section 9.1, excluding any terminating zero byte */
#define APN_MAXLEN	100

char *osmo_apn_qualify(unsigned int mcc, unsigned int mnc, const char *ni);

/* Compose a string of the form '<ni>.mnc001.mcc002.gprs\0', returned in a
 * static buffer. */
char *osmo_apn_qualify_from_imsi(const char *imsi,
				 const char *ni, int have_3dig_mnc);
