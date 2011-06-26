#ifndef OSMOCORE_UTIL_H
#define OSMOCORE_UTIL_H

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define OSMO_MAX(a, b) (a) >= (b) ? (a) : (b)

#include <stdint.h>

struct value_string {
	unsigned int value;
	const char *str;
};

const char *get_value_string(const struct value_string *vs, uint32_t val);
int get_string_value(const struct value_string *vs, const char *str);

char osmo_bcd2char(uint8_t bcd);
/* only works for numbers in ascci */
uint8_t osmo_char2bcd(char c);

int osmo_hexparse(const char *str, uint8_t *b, int max_len);
char *osmo_hexdump(const unsigned char *buf, int len);
char *osmo_osmo_hexdump_nospc(const unsigned char *buf, int len);
char *osmo_ubit_dump(const uint8_t *bits, unsigned int len);

#define osmo_static_assert(exp, name) typedef int dummy##name [(exp) ? 1 : -1];

void osmo_str2lower(char *out, const char *in);
void osmo_str2upper(char *out, const char *in);

#define OSMO_SNPRINTF_RET(ret, rem, offset, len)		\
do {								\
	len += ret;						\
	if (ret > rem)						\
		ret = rem;					\
	offset += ret;						\
	rem -= ret;						\
} while (0)

#endif
