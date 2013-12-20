#ifndef OSMOCORE_UTIL_H
#define OSMOCORE_UTIL_H

/*! \defgroup utils General-purpose utility functions
 *  @{
 */

/*! \file utils.h */

/*! \brief Determine number of elements in an array of static size */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
/*! \brief Return the maximum of two specified values */
#define OSMO_MAX(a, b) ((a) >= (b) ? (a) : (b))
/*! \brief Return the minimum of two specified values */
#define OSMO_MIN(a, b) ((a) >= (b) ? (b) : (a))

#include <stdint.h>

/*! \brief A mapping between human-readable string and numeric value */
struct value_string {
	unsigned int value;	/*!< \brief numeric value */
	const char *str;	/*!< \brief human-readable string */
};

const char *get_value_string(const struct value_string *vs, uint32_t val);

int get_string_value(const struct value_string *vs, const char *str);

char osmo_bcd2char(uint8_t bcd);
/* only works for numbers in ascci */
uint8_t osmo_char2bcd(char c);

int osmo_hexparse(const char *str, uint8_t *b, int max_len);

char *osmo_ubit_dump(const uint8_t *bits, unsigned int len);
char *osmo_hexdump(const unsigned char *buf, int len);
char *osmo_hexdump_nospc(const unsigned char *buf, int len);
char *osmo_osmo_hexdump_nospc(const unsigned char *buf, int len) __attribute__((__deprecated__));

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

/*! @} */

#endif
