#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/bits.h>

static const uint8_t input[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
static const uint8_t exp_out[] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
static char s[18];

enum END {LE, BE};

static inline const char *
end2str(enum END e)
{
	if (e == LE) return "LE";
	return "BE";
}


/* convenience wrappers */

static inline uint64_t
load64(enum END e, const uint8_t *buf, unsigned nbytes)
{
	return (e == BE) ? osmo_load64be_ext(buf, nbytes) : osmo_load64le_ext(buf, nbytes);
}

static inline uint32_t
load32(enum END e, const uint8_t *buf, unsigned nbytes)
{
	return (e == BE) ? osmo_load32be_ext(buf, nbytes) : osmo_load32le_ext(buf, nbytes);
}

static inline uint16_t
load16(enum END e, const uint8_t *buf)
{
	return (e == BE) ? osmo_load16be(buf) : osmo_load16le(buf);
}

static inline void
store64(enum END e, uint64_t t, uint8_t *buf, unsigned nbytes)
{
	(e == BE) ? osmo_store64be_ext(t, buf, nbytes) : osmo_store64le_ext(t, buf, nbytes);
}

static inline void
store32(enum END e, uint64_t t, uint8_t *buf, unsigned nbytes)
{
	(e == BE) ? osmo_store32be_ext(t, buf, nbytes) : osmo_store32le_ext(t, buf, nbytes);
}

static inline void
store16(enum END e, uint64_t t, uint8_t *buf)
{
	(e == BE) ? osmo_store16be(t, buf) : osmo_store16le(t, buf);
}


/* helper functions */

static inline bool
printcheck(bool chk, unsigned nbytes, enum END e, bool b)
{
	if (!chk) {
		printf("%u %s FAILED", nbytes * 8, end2str(e));
		return true;
	}
	printf("%u %s OK", nbytes * 8, end2str(e));
	return b;
}

static inline bool
dumpcheck(const char *dump, const char *s, unsigned nbytes, bool chk, enum END e, bool b)
{
	bool x = printcheck(chk, nbytes, e, b);
	if (!dump) return x;

	int m = memcmp(s, dump, nbytes);
	if (0 == m) {
		printf(", storage OK");
		return x;
	}
	printf(", [%d]", m);

	return true;
}


/* printcheckXX(): load/store 'test' and check against 'expected' value, compare to 'dump' buffer if given and print if necessary */

static inline void
printcheck64(enum END e, unsigned nbytes, uint64_t test, uint64_t expected, const char *dump, bool print)
{
	uint8_t buf[nbytes];

	store64(e, test, buf, nbytes);

	char *s = osmo_hexdump_nospc(buf, nbytes);
	uint64_t result = load64(e, buf, nbytes);

	print = dumpcheck(dump, s, nbytes, result == expected, e, print);

	if (print)
		printf(": buffer %s known buffer %s loaded %.16" PRIx64 " expected %.16" PRIx64, s, dump, result, expected);
	printf("\n");
}

static inline void
printcheck32(enum END e, unsigned nbytes, uint32_t test, uint32_t expected, const char *dump, bool print)
{
	uint8_t buf[nbytes];

	store32(e, test, buf, nbytes);

	char *s = osmo_hexdump_nospc(buf, nbytes);
	uint32_t result = load32(e, buf, nbytes);

	print = dumpcheck(dump, s, nbytes, result == expected, e, print);

	if (print)
		printf(": buffer %s known buffer %s loaded %.8" PRIx32 " expected %.8" PRIx32, s, dump, result, expected);
	printf("\n");
}

static inline void
printcheck16(enum END e, uint32_t test, uint32_t expected, const char *dump, bool print)
{
	uint8_t buf[2];

	store16(e, test, buf);

	char *s = osmo_hexdump_nospc(buf, 2);
	uint16_t result = load16(e, buf);

	print = dumpcheck(dump, s, 2, result == expected, e, print);

	if (print)
		printf(": buffer %s known buffer %s loaded %.4" PRIx16 " expected %.4" PRIx16, s, dump, result, expected);
	printf("\n");
}


/* compute expected value - zero excessive bytes */

static inline uint64_t
exp64(enum END e, unsigned nbytes, uint64_t value)
{
	uint8_t adj = 64 - nbytes * 8;
	uint64_t v = value << adj;
	return (e == LE) ? v >> adj : v;
}

static inline uint32_t
exp32(enum END e, unsigned nbytes, uint32_t value)
{
	uint8_t adj = 32 - nbytes * 8;
	uint32_t v = value << adj;
	return (e == LE) ? v >> adj : v;
}


/* run actual tests - if 'test' is 0 than generate random test value internally */

static inline void
check64(uint64_t test, uint64_t expected, unsigned nbytes, enum END e)
{
	bool print = true;
	if (0 == test && 0 == expected) {
		test = ((uint64_t)rand() << 32) + rand();
		expected = exp64(e, nbytes, test);
		print = false;
	}
	snprintf(s, 17, "%.16" PRIx64, expected);
	printcheck64(e, nbytes, test, expected, (BE == e) ? s : NULL, print);
}

static inline void
check32(uint32_t test, uint32_t expected, unsigned nbytes, enum END e)
{
	bool print = true;
	if (0 == test && 0 == expected) {
		test = rand();
		expected = exp32(e, nbytes, test);
		print = false;
	}
	snprintf(s, 17, "%.8" PRIx32, expected);
	printcheck32(e, nbytes, test, expected, (BE == e) ? s : NULL, print);
}

static inline void
check16(uint16_t test, enum END e)
{
	bool print = true;
	if (0 == test) {
		test = (uint16_t)rand();
		print = false;
	}
	snprintf(s, 17, "%.4" PRIx16, test);
	printcheck16(e, test, test, (BE == e) ? s : NULL, print);
}


int main(int argc, char **argv)
{
	uint8_t out[ARRAY_SIZE(input)];
	unsigned int offs;

	srand(time(NULL));

	for (offs = 0; offs < sizeof(out); offs++) {
		uint8_t *start = out + offs;
		uint8_t len = sizeof(out) - offs;

		memcpy(out, input, sizeof(out));

		printf("INORDER:  %s\n", osmo_hexdump(start, len));
		osmo_revbytebits_buf(start, len);
		printf("REVERSED: %s\n", osmo_hexdump(start, len));
		if (memcmp(start, exp_out + offs, len)) {
			printf("EXPECTED: %s\n", osmo_hexdump(exp_out+offs, len));
			fprintf(stderr, "REVERSED != EXPECTED!\n");
			exit(1);
		}
		printf("\n");
	}

	printf("checking byte packing...\n");

	printf("running static tests...\n");

	check64(0xDEADBEEFF00DCAFE, 0xDEADBEEFF00DCAFE, 8, BE);
	check64(0xDEADBEEFF00DCAFE, 0xADBEEFF00DCAFE00, 7, BE);
	check64(0xDEADBEEFF00DCAFE, 0xBEEFF00DCAFE0000, 6, BE);
	check64(0xDEADBEEFF00DCAFE, 0xEFF00DCAFE000000, 5, BE);

	check64(0xDEADBEEFF00DCAFE, 0xDEADBEEFF00DCAFE, 8, LE);
	check64(0xDEADBEEFF00DCAFE, 0x00ADBEEFF00DCAFE, 7, LE);
	check64(0xDEADBEEFF00DCAFE, 0x0000BEEFF00DCAFE, 6, LE);
	check64(0xDEADBEEFF00DCAFE, 0x000000EFF00DCAFE, 5, LE);

	check32(0xBABEFACE, 0xBABEFACE, 4, BE);
	check32(0xBABEFACE, 0xBEFACE00, 3, BE);

	check32(0xBABEFACE, 0xBABEFACE, 4, LE);
	check32(0xBABEFACE, 0x00BEFACE, 3, LE);

	check16(0xB00B, BE);
	check16(0xB00B, LE);

	printf("running random tests...\n");

	check64(0, 0, 8, BE);
	check64(0, 0, 7, BE);
	check64(0, 0, 6, BE);
	check64(0, 0, 5, BE);

	check64(0, 0, 8, LE);
	check64(0, 0, 7, LE);
	check64(0, 0, 6, LE);
	check64(0, 0, 5, LE);

	check32(0, 0, 4, BE);
	check32(0, 0, 3, BE);

	check32(0, 0, 4, LE);
	check32(0, 0, 3, LE);

	check16(0, BE);
	check16(0, LE);

	return 0;
}
