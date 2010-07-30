
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>

#include <osmocore/utils.h>

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

/* only works for numbers in ascci */
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
