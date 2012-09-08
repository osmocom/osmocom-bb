
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>

#include <osmocom/core/utils.h>

/*! \addtogroup utils
 * @{
 */

/*! \file utils.c */

static char namebuf[255];

/*! \brief get human-readable string for given value
 *  \param[in] vs Array of value_string tuples
 *  \param[in] val Value to be converted
 *  \returns pointer to human-readable string
 */
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

/*! \brief get numeric value for given human-readable string
 *  \param[in] vs Array of value_string tuples
 *  \param[in] str human-readable string
 *  \returns numeric value (>0) or negative numer in case of error
 */
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

/*! \brief Convert BCD-encoded digit into printable character
 *  \param[in] bcd A single BCD-encoded digit
 *  \returns single printable character
 */
char osmo_bcd2char(uint8_t bcd)
{
	if (bcd < 0xa)
		return '0' + bcd;
	else
		return 'A' + (bcd - 0xa);
}

/* only works for numbers in ascii */
uint8_t osmo_char2bcd(char c)
{
	return c - 0x30;
}

int osmo_hexparse(const char *str, uint8_t *b, int max_len)

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

static char *_osmo_hexdump(const unsigned char *buf, int len, char *delim)
{
	int i;
	char *cur = hexd_buff;

	hexd_buff[0] = 0;
	for (i = 0; i < len; i++) {
		int len_remain = sizeof(hexd_buff) - (cur - hexd_buff);
		if (len_remain <= 0)
			break;
		int rc = snprintf(cur, len_remain, "%02x%s", buf[i], delim);
		if (rc <= 0)
			break;
		cur += rc;
	}
	hexd_buff[sizeof(hexd_buff)-1] = 0;
	return hexd_buff;
}

/*! \brief Convert a sequence of unpacked bits to ASCII string
 * \param[in] bits A sequence of unpacked bits
 * \param[in] len Length of bits
 */
char *osmo_ubit_dump(const uint8_t *bits, unsigned int len)
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

/*! \brief Convert binary sequence to hexadecimal ASCII string
 *  \param[in] buf pointer to sequence of bytes
 *  \param[in] len length of buf in number of bytes
 *  \returns pointer to zero-terminated string
 *
 * This function will print a sequence of bytes as hexadecimal numbers,
 * adding one space character between each byte (e.g. "1a ef d9")
 */
char *osmo_hexdump(const unsigned char *buf, int len)
{
	return _osmo_hexdump(buf, len, " ");
}

/*! \brief Convert binary sequence to hexadecimal ASCII string
 *  \param[in] buf pointer to sequence of bytes
 *  \param[in] len length of buf in number of bytes
 *  \returns pointer to zero-terminated string
 *
 * This function will print a sequence of bytes as hexadecimal numbers,
 * without any space character between each byte (e.g. "1aefd9")
 */
char *osmo_hexdump_nospc(const unsigned char *buf, int len)
{
	return _osmo_hexdump(buf, len, "");
}

	/* Compat with previous typo to preserve abi */
char *osmo_osmo_hexdump_nospc(const unsigned char *buf, int len)
	__attribute__((weak, alias("osmo_hexdump_nospc")));

#include "../config.h"
#ifdef HAVE_CTYPE_H
#include <ctype.h>
/*! \brief Convert an entire string to lower case
 *  \param[out] out output string, caller-allocated
 *  \param[in] in input string
 */
void osmo_str2lower(char *out, const char *in)
{
	unsigned int i;

	for (i = 0; i < strlen(in); i++)
		out[i] = tolower(in[i]);
	out[strlen(in)] = '\0';
}

/*! \brief Convert an entire string to upper case
 *  \param[out] out output string, caller-allocated
 *  \param[in] in input string
 */
void osmo_str2upper(char *out, const char *in)
{
	unsigned int i;

	for (i = 0; i < strlen(in); i++)
		out[i] = toupper(in[i]);
	out[strlen(in)] = '\0';
}
#endif /* HAVE_CTYPE_H */

/*! @} */
