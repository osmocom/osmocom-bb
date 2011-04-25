
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>

#include <osmocom/core/utils.h>

static char namebuf[255];
const char *get_value_string(const struct value_string *vs, uint32_t val)
{
	int i;

	for (i = 0;; i++) {
		if (vs[i].value == 0 && vs[i].str == NULL)
			break;
		if (vs[i].value == val)
			return vs[i].str;
	}

	snprintf(namebuf, sizeof(namebuf), "unknown 0x%x", val);
	return namebuf;
}

int get_string_value(const struct value_string *vs, const char *str)
{
	int i;

	for (i = 0;; i++) {
		if (vs[i].value == 0 && vs[i].str == NULL)
			break;
		if (!strcasecmp(vs[i].str, str))
			return vs[i].value;
	}
	return -EINVAL;
}

char bcd2char(uint8_t bcd)
{
	if (bcd < 0xa)
		return '0' + bcd;
	else
		return 'A' + (bcd - 0xa);
}

/* only works for numbers in ascii */
uint8_t char2bcd(char c)
{
	return c - 0x30;
}

int hexparse(const char *str, uint8_t *b, int max_len)

{
	int i, l, v;

	l = strlen(str);
	if ((l&1) || ((l>>1) > max_len))
		return -1;

	memset(b, 0x00, max_len);

	for (i=0; i<l; i++) {
		char c = str[i];
		if (c >= '0' && c <= '9')
			v = c - '0';
		else if (c >= 'a' && c <= 'f')
			v = 10 + (c - 'a');
		else if (c >= 'A' && c <= 'F')
			v = 10 + (c - 'A');
		else
			return -1;
		b[i>>1] |= v << (i&1 ? 0 : 4);
	}

	return i>>1;
}

static char hexd_buff[4096];

static char *_hexdump(const unsigned char *buf, int len, char *delim)
{
	int i;
	char *cur = hexd_buff;

	hexd_buff[0] = 0;
	for (i = 0; i < len; i++) {
		int len_remain = sizeof(hexd_buff) - (cur - hexd_buff);
		int rc = snprintf(cur, len_remain, "%02x%s", buf[i], delim);
		if (rc <= 0)
			break;
		cur += rc;
	}
	hexd_buff[sizeof(hexd_buff)-1] = 0;
	return hexd_buff;
}

char *ubit_dump(const uint8_t *bits, unsigned int len)
{
	int i;

	if (len > sizeof(hexd_buff)-1)
		len = sizeof(hexd_buff)-1;
	memset(hexd_buff, 0, sizeof(hexd_buff));

	for (i = 0; i < len; i++) {
		char outch;
		switch (bits[i]) {
		case 0:
			outch = '0';
			break;
		case 0xff:
			outch = '?';
			break;
		case 1:
			outch = '1';
			break;
		default:
			outch = 'E';
			break;
		}
		hexd_buff[i] = outch;
	}
	hexd_buff[sizeof(hexd_buff)-1] = 0;
	return hexd_buff;
}

char *hexdump(const unsigned char *buf, int len)
{
	return _hexdump(buf, len, " ");
}

char *hexdump_nospc(const unsigned char *buf, int len)
{
	return _hexdump(buf, len, "");
}

#include "../config.h"
#ifdef HAVE_CTYPE_H
#include <ctype.h>
void osmo_str2lower(char *out, const char *in)
{
	unsigned int i;

	for (i = 0; i < strlen(in); i++)
		out[i] = tolower(in[i]);
	out[strlen(in)] = '\0';
}

void osmo_str2upper(char *out, const char *in)
{
	unsigned int i;

	for (i = 0; i < strlen(in); i++)
		out[i] = toupper(in[i]);
	out[strlen(in)] = '\0';
}
#endif /* HAVE_CTYPE_H */
