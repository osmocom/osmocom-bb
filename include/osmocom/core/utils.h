#ifndef OSMOCORE_UTIL_H
#define OSMOCORE_UTIL_H

/*! \file utils.h
 *  \brief General-purpose utilities in the Osmocom core library
 */

/*! \brief Determine number of elements in an array of static size */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
/*! \brief Return the maximum of two specified values */
#define OSMO_MAX(a, b) (a) >= (b) ? (a) : (b)
/*! \brief Return the minimum of two specified values */
#define OSMO_MIN(a, b) (a) >= (b) ? (b) : (a)

#include <stdint.h>

/*! \brief A mapping between human-readable string and numeric value */
struct value_string {
	unsigned int value;	/*!< \brief numeric value */
	const char *str;	/*!< \brief human-readable string */
};

/*! \fn get_value_string(const struct value_string *vs, uint32_t val)
 * \brief get human-readable string for given value
 * \param[in] vs Array of value_string tuples
 * \param[in] val Value to be converted
 * \returns pointer to human-readable string
 */
const char *get_value_string(const struct value_string *vs, uint32_t val);

/*! \fn get_string_value(const struct value_string *vs, const char *str)
 * \brief get numeric value for given human-readable string
 * \param[in] vs Array of value_string tuples
 * \param[in] str human-readable string
 * \returns numeric value (>0) or negative numer in case of error
 */
int get_string_value(const struct value_string *vs, const char *str);

/*! \fn osmo_bcd2char(uint8_t bcd)
 * \brief Convert BCD-encoded digit into printable character
 * \param[in] bcd A single BCD-encoded digit
 * \returns single printable character
 */
char osmo_bcd2char(uint8_t bcd);
/* only works for numbers in ascci */
uint8_t osmo_char2bcd(char c);

int osmo_hexparse(const char *str, uint8_t *b, int max_len);

/*! \fn osmo_hexdump(const unsigned char *buf, int len)
 * \brief Convert binary sequence to hexadecimal ASCII string
 * This function will print a sequence of bytes as hexadecimal numbers,
 * adding one space character between each byte (e.g. "1a ef d9")
 * \param[in] buf pointer to sequence of bytes
 * \param[in] len length of buf in number of bytes
 * \returns pointer to zero-terminated string
 */
char *osmo_hexdump(const unsigned char *buf, int len);

/*! \fn osmo_hexdump_nospc(const unsigned char *buf, int len)
 * \brief Convert binary sequence to hexadecimal ASCII string
 * This function will print a sequence of bytes as hexadecimal numbers,
 * without any space character between each byte (e.g. "1aefd9")
 * \param[in] buf pointer to sequence of bytes
 * \param[in] len length of buf in number of bytes
 * \returns pointer to zero-terminated string
 */
char *osmo_osmo_hexdump_nospc(const unsigned char *buf, int len);

/*! \fn osmo_ubit_dump(const uint8_t *bits, unsigned int len)
 * \brief Convert a sequence of unpacked bits to ASCII string
 * \param[in] bits A sequence of unpacked bits
 * \param[in] len Length of bits
 */
char *osmo_ubit_dump(const uint8_t *bits, unsigned int len);

#define osmo_static_assert(exp, name) typedef int dummy##name [(exp) ? 1 : -1];

/*! \fn osmo_str2lower(char *out, const char *in)
 * \brief Convert an entire string to lower case
 * \param[out] out output string, caller-allocated
 * \param[in] in input string
 */
void osmo_str2lower(char *out, const char *in);

/*! \fn osmo_str2upper(char *out, const char *in)
 * \brief Convert an entire string to upper case
 * \param[out] out output string, caller-allocated
 * \param[in] in input string
 */
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
