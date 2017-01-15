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

int osmo_apn_from_str(uint8_t *apn_enc, size_t max_apn_enc_len, const char *str);
char * osmo_apn_to_str(char *out_str, const uint8_t *apn_enc, size_t apn_enc_len);
