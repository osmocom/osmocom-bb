#pragma once

#include <osmocom/core/backtrace.h>
#include <osmocom/core/talloc.h>

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
/*! \brief Stringify the contents of a macro, e.g. a port number */
#define OSMO_STRINGIFY(x) #x
/*! \brief Make a value_string entry from an enum value name */
#define OSMO_VALUE_STRING(x) { x, OSMO_STRINGIFY(x) }

#include <stdint.h>

/*! \brief A mapping between human-readable string and numeric value */
struct value_string {
	unsigned int value;	/*!< \brief numeric value */
	const char *str;	/*!< \brief human-readable string */
};

const char *get_value_string(const struct value_string *vs, uint32_t val);
const char *get_value_string_or_null(const struct value_string *vs,
				     uint32_t val);

int get_string_value(const struct value_string *vs, const char *str);

char osmo_bcd2char(uint8_t bcd);
/* only works for numbers in ascci */
uint8_t osmo_char2bcd(char c);

int osmo_hexparse(const char *str, uint8_t *b, int max_len);

char *osmo_ubit_dump(const uint8_t *bits, unsigned int len);
char *osmo_hexdump(const unsigned char *buf, int len);
char *osmo_hexdump_nospc(const unsigned char *buf, int len);
char *osmo_osmo_hexdump_nospc(const unsigned char *buf, int len) __attribute__((__deprecated__));

#define osmo_static_assert(exp, name) typedef int dummy##name [(exp) ? 1 : -1] __attribute__((__unused__));

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

/*! Helper macro to terminate when an assertion failes
 *  \param[in] exp Predicate to verify
 *  This function will generate a backtrace and terminate the program if
 *  the predicate evaluates to false (0).
 */
#define OSMO_ASSERT(exp)    \
	if (!(exp)) { \
		fprintf(stderr, "Assert failed %s %s:%d\n", #exp, __BASE_FILE__, __LINE__); \
		osmo_generate_backtrace(); \
		abort(); \
	}

/*! duplicate a string using talloc and release its prior content (if any)
 * \param[in] ctx Talloc context to use for allocation
 * \param[out] dst pointer to string, will be updated with ptr to new string
 * \param[in] newstr String that will be copieed to newly allocated string */
static inline void osmo_talloc_replace_string(void *ctx, char **dst, const char *newstr)
{
	if (*dst)
		talloc_free(*dst);
	*dst = talloc_strdup(ctx, newstr);
}

int osmo_constant_time_cmp(const uint8_t *exp, const uint8_t *rel, const int count);
uint64_t osmo_decode_big_endian(const uint8_t *data, size_t data_len);
uint8_t *osmo_encode_big_endian(uint64_t value, size_t data_len);

size_t osmo_strlcpy(char *dst, const char *src, size_t siz);

/*! @} */
