
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>

#include <osmocore/utils.h>

const char *get_value_string(const struct value_string *vs, uint32_t val)
{
	int i;

	for (i = 0;; i++) {
		if (vs[i].value == 0 && vs[i].str == NULL)
			break;
		if (vs[i].value == val)
			return vs[i].str;
	}
	return "unknown";
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
