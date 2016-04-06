#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <osmocom/core/bits.h>
#include <osmocom/core/conv.h>
#include <osmocom/core/utils.h>

#define MAX_LEN_BITS	512
#define MAX_LEN_BYTES	(512/8)


/* ------------------------------------------------------------------------ */
/* Test codes                                                               */
/* ------------------------------------------------------------------------ */

/* GSM xCCH -> Non-recursive code, flushed, not punctured */
static const uint8_t conv_gsm_xcch_next_output[][2] = {
	{ 0, 3 }, { 1, 2 }, { 0, 3 }, { 1, 2 },
	{ 3, 0 }, { 2, 1 }, { 3, 0 }, { 2, 1 },
	{ 3, 0 }, { 2, 1 }, { 3, 0 }, { 2, 1 },
	{ 0, 3 }, { 1, 2 }, { 0, 3 }, { 1, 2 },
};

static const uint8_t conv_gsm_xcch_next_state[][2] = {
	{  0,  1 }, {  2,  3 }, {  4,  5 }, {  6,  7 },
	{  8,  9 }, { 10, 11 }, { 12, 13 }, { 14, 15 },
	{  0,  1 }, {  2,  3 }, {  4,  5 }, {  6,  7 },
	{  8,  9 }, { 10, 11 }, { 12, 13 }, { 14, 15 },
};

static const struct osmo_conv_code conv_gsm_xcch = {
	.N = 2,
	.K = 5,
	.len = 224,
	.term = CONV_TERM_FLUSH,
	.next_output = conv_gsm_xcch_next_output,
	.next_state  = conv_gsm_xcch_next_state,
};


/* GSM TCH/AFS 7.95 -> Recursive code, flushed, with puncturing */
static const uint8_t conv_gsm_tch_afs_7_95_next_output[][2] = {
	{ 0, 7 }, { 3, 4 }, { 2, 5 }, { 1, 6 },
	{ 2, 5 }, { 1, 6 }, { 0, 7 }, { 3, 4 },
	{ 3, 4 }, { 0, 7 }, { 1, 6 }, { 2, 5 },
	{ 1, 6 }, { 2, 5 }, { 3, 4 }, { 0, 7 },
	{ 3, 4 }, { 0, 7 }, { 1, 6 }, { 2, 5 },
	{ 1, 6 }, { 2, 5 }, { 3, 4 }, { 0, 7 },
	{ 0, 7 }, { 3, 4 }, { 2, 5 }, { 1, 6 },
	{ 2, 5 }, { 1, 6 }, { 0, 7 }, { 3, 4 },
	{ 0, 7 }, { 3, 4 }, { 2, 5 }, { 1, 6 },
	{ 2, 5 }, { 1, 6 }, { 0, 7 }, { 3, 4 },
	{ 3, 4 }, { 0, 7 }, { 1, 6 }, { 2, 5 },
	{ 1, 6 }, { 2, 5 }, { 3, 4 }, { 0, 7 },
	{ 3, 4 }, { 0, 7 }, { 1, 6 }, { 2, 5 },
	{ 1, 6 }, { 2, 5 }, { 3, 4 }, { 0, 7 },
	{ 0, 7 }, { 3, 4 }, { 2, 5 }, { 1, 6 },
	{ 2, 5 }, { 1, 6 }, { 0, 7 }, { 3, 4 },
};

static const uint8_t conv_gsm_tch_afs_7_95_next_state[][2] = {
	{  0,  1 }, {  2,  3 }, {  5,  4 }, {  7,  6 },
	{  9,  8 }, { 11, 10 }, { 12, 13 }, { 14, 15 },
	{ 16, 17 }, { 18, 19 }, { 21, 20 }, { 23, 22 },
	{ 25, 24 }, { 27, 26 }, { 28, 29 }, { 30, 31 },
	{ 33, 32 }, { 35, 34 }, { 36, 37 }, { 38, 39 },
	{ 40, 41 }, { 42, 43 }, { 45, 44 }, { 47, 46 },
	{ 49, 48 }, { 51, 50 }, { 52, 53 }, { 54, 55 },
	{ 56, 57 }, { 58, 59 }, { 61, 60 }, { 63, 62 },
	{  1,  0 }, {  3,  2 }, {  4,  5 }, {  6,  7 },
	{  8,  9 }, { 10, 11 }, { 13, 12 }, { 15, 14 },
	{ 17, 16 }, { 19, 18 }, { 20, 21 }, { 22, 23 },
	{ 24, 25 }, { 26, 27 }, { 29, 28 }, { 31, 30 },
	{ 32, 33 }, { 34, 35 }, { 37, 36 }, { 39, 38 },
	{ 41, 40 }, { 43, 42 }, { 44, 45 }, { 46, 47 },
	{ 48, 49 }, { 50, 51 }, { 53, 52 }, { 55, 54 },
	{ 57, 56 }, { 59, 58 }, { 60, 61 }, { 62, 63 },
};

static const uint8_t conv_gsm_tch_afs_7_95_next_term_output[] = {
	 0,  3,  5,  6,  5,  6,  0,  3,  3,  0,  6,  5,  6,  5,  3,  0,
	 4,  7,  1,  2,  1,  2,  4,  7,  7,  4,  2,  1,  2,  1,  7,  4,
	 7,  4,  2,  1,  2,  1,  7,  4,  4,  7,  1,  2,  1,  2,  4,  7,
	 3,  0,  6,  5,  6,  5,  3,  0,  0,  3,  5,  6,  5,  6,  0,  3,
};

static const uint8_t conv_gsm_tch_afs_7_95_next_term_state[] = {
	 0,  2,  4,  6,  8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
	32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62,
	 0,  2,  4,  6,  8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
	32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62,
};

static int conv_gsm_tch_afs_7_95_puncture[] = {
	  1,   2,   4,   5,   8,  22,  70, 118, 166, 214, 262, 310,
	317, 319, 325, 332, 334, 341, 343, 349, 356, 358, 365, 367,
	373, 380, 382, 385, 389, 391, 397, 404, 406, 409, 413, 415,
	421, 428, 430, 433, 437, 439, 445, 452, 454, 457, 461, 463,
	469, 476, 478, 481, 485, 487, 490, 493, 500, 502, 503, 505,
	506, 508, 509, 511, 512,
	-1, /* end */
};

static const struct osmo_conv_code conv_gsm_tch_afs_7_95 = {
	.N = 3,
	.K = 7,
	.len = 165,
	.term = CONV_TERM_FLUSH,
	.next_output      = conv_gsm_tch_afs_7_95_next_output,
	.next_state       = conv_gsm_tch_afs_7_95_next_state,
	.next_term_output = conv_gsm_tch_afs_7_95_next_term_output,
	.next_term_state  = conv_gsm_tch_afs_7_95_next_term_state,
	.puncture         = conv_gsm_tch_afs_7_95_puncture,
};


/* GMR-1 TCH3 Speech -> Non recursive code, tail-biting, punctured */
static const uint8_t conv_gmr1_tch3_speech_next_output[][2] = {
	{  0,  3 }, {  1,  2 }, {  3,  0 }, {  2,  1 },
	{  3,  0 }, {  2,  1 }, {  0,  3 }, {  1,  2 },
	{  0,  3 }, {  1,  2 }, {  3,  0 }, {  2,  1 },
	{  3,  0 }, {  2,  1 }, {  0,  3 }, {  1,  2 },
	{  2,  1 }, {  3,  0 }, {  1,  2 }, {  0,  3 },
	{  1,  2 }, {  0,  3 }, {  2,  1 }, {  3,  0 },
	{  2,  1 }, {  3,  0 }, {  1,  2 }, {  0,  3 },
	{  1,  2 }, {  0,  3 }, {  2,  1 }, {  3,  0 },
	{  3,  0 }, {  2,  1 }, {  0,  3 }, {  1,  2 },
	{  0,  3 }, {  1,  2 }, {  3,  0 }, {  2,  1 },
	{  3,  0 }, {  2,  1 }, {  0,  3 }, {  1,  2 },
	{  0,  3 }, {  1,  2 }, {  3,  0 }, {  2,  1 },
	{  1,  2 }, {  0,  3 }, {  2,  1 }, {  3,  0 },
	{  2,  1 }, {  3,  0 }, {  1,  2 }, {  0,  3 },
	{  1,  2 }, {  0,  3 }, {  2,  1 }, {  3,  0 },
	{  2,  1 }, {  3,  0 }, {  1,  2 }, {  0,  3 },
};

static const uint8_t conv_gmr1_tch3_speech_next_state[][2] = {
	{  0,  1 }, {  2,  3 }, {  4,  5 }, {  6,  7 },
	{  8,  9 }, { 10, 11 }, { 12, 13 }, { 14, 15 },
	{ 16, 17 }, { 18, 19 }, { 20, 21 }, { 22, 23 },
	{ 24, 25 }, { 26, 27 }, { 28, 29 }, { 30, 31 },
	{ 32, 33 }, { 34, 35 }, { 36, 37 }, { 38, 39 },
	{ 40, 41 }, { 42, 43 }, { 44, 45 }, { 46, 47 },
	{ 48, 49 }, { 50, 51 }, { 52, 53 }, { 54, 55 },
	{ 56, 57 }, { 58, 59 }, { 60, 61 }, { 62, 63 },
	{  0,  1 }, {  2,  3 }, {  4,  5 }, {  6,  7 },
	{  8,  9 }, { 10, 11 }, { 12, 13 }, { 14, 15 },
	{ 16, 17 }, { 18, 19 }, { 20, 21 }, { 22, 23 },
	{ 24, 25 }, { 26, 27 }, { 28, 29 }, { 30, 31 },
	{ 32, 33 }, { 34, 35 }, { 36, 37 }, { 38, 39 },
	{ 40, 41 }, { 42, 43 }, { 44, 45 }, { 46, 47 },
	{ 48, 49 }, { 50, 51 }, { 52, 53 }, { 54, 55 },
	{ 56, 57 }, { 58, 59 }, { 60, 61 }, { 62, 63 },
};

static const int conv_gmr1_tch3_speech_puncture[] = {
	 3,  7, 11, 15, 19, 23, 27, 31, 35, 39, 43, 47,
	51, 55, 59, 63, 67, 71, 75, 79, 83, 87, 91, 95,
	-1, /* end */
};

static const struct osmo_conv_code conv_gmr1_tch3_speech = {
	.N = 2,
	.K = 7,
	.len = 48,
	.term = CONV_TERM_TAIL_BITING,
	.next_output = conv_gmr1_tch3_speech_next_output,
	.next_state  = conv_gmr1_tch3_speech_next_state,
	.puncture = conv_gmr1_tch3_speech_puncture,
};


/* WiMax FCH -> Non recursive code, tail-biting, non-punctured */
static const uint8_t conv_wimax_fch_next_output[][2] = {
	{  0,  3 }, {  2,  1 }, {  3,  0 }, {  1,  2 },
	{  3,  0 }, {  1,  2 }, {  0,  3 }, {  2,  1 },
	{  0,  3 }, {  2,  1 }, {  3,  0 }, {  1,  2 },
	{  3,  0 }, {  1,  2 }, {  0,  3 }, {  2,  1 },
	{  1,  2 }, {  3,  0 }, {  2,  1 }, {  0,  3 },
	{  2,  1 }, {  0,  3 }, {  1,  2 }, {  3,  0 },
	{  1,  2 }, {  3,  0 }, {  2,  1 }, {  0,  3 },
	{  2,  1 }, {  0,  3 }, {  1,  2 }, {  3,  0 },
	{  3,  0 }, {  1,  2 }, {  0,  3 }, {  2,  1 },
	{  0,  3 }, {  2,  1 }, {  3,  0 }, {  1,  2 },
	{  3,  0 }, {  1,  2 }, {  0,  3 }, {  2,  1 },
	{  0,  3 }, {  2,  1 }, {  3,  0 }, {  1,  2 },
	{  2,  1 }, {  0,  3 }, {  1,  2 }, {  3,  0 },
	{  1,  2 }, {  3,  0 }, {  2,  1 }, {  0,  3 },
	{  2,  1 }, {  0,  3 }, {  1,  2 }, {  3,  0 },
	{  1,  2 }, {  3,  0 }, {  2,  1 }, {  0,  3 },
};

static const uint8_t conv_wimax_fch_next_state[][2] = {
	{  0,  1 }, {  2,  3 }, {  4,  5 }, {  6,  7 },
	{  8,  9 }, { 10, 11 }, { 12, 13 }, { 14, 15 },
	{ 16, 17 }, { 18, 19 }, { 20, 21 }, { 22, 23 },
	{ 24, 25 }, { 26, 27 }, { 28, 29 }, { 30, 31 },
	{ 32, 33 }, { 34, 35 }, { 36, 37 }, { 38, 39 },
	{ 40, 41 }, { 42, 43 }, { 44, 45 }, { 46, 47 },
	{ 48, 49 }, { 50, 51 }, { 52, 53 }, { 54, 55 },
	{ 56, 57 }, { 58, 59 }, { 60, 61 }, { 62, 63 },
	{  0,  1 }, {  2,  3 }, {  4,  5 }, {  6,  7 },
	{  8,  9 }, { 10, 11 }, { 12, 13 }, { 14, 15 },
	{ 16, 17 }, { 18, 19 }, { 20, 21 }, { 22, 23 },
	{ 24, 25 }, { 26, 27 }, { 28, 29 }, { 30, 31 },
	{ 32, 33 }, { 34, 35 }, { 36, 37 }, { 38, 39 },
	{ 40, 41 }, { 42, 43 }, { 44, 45 }, { 46, 47 },
	{ 48, 49 }, { 50, 51 }, { 52, 53 }, { 54, 55 },
	{ 56, 57 }, { 58, 59 }, { 60, 61 }, { 62, 63 },
};

static const struct osmo_conv_code conv_wimax_fch = {
	.N = 2,
	.K = 7,
	.len = 48,
	.term = CONV_TERM_TAIL_BITING,
	.next_output = conv_wimax_fch_next_output,
	.next_state  = conv_wimax_fch_next_state,
};


/* Random code -> Non recursive code, direct truncation, non-punctured */
static const struct osmo_conv_code conv_trunc = {
	.N = 2,
	.K = 5,
	.len = 224,
	.term = CONV_TERM_TRUNCATION,
	.next_output = conv_gsm_xcch_next_output,
	.next_state  = conv_gsm_xcch_next_state,
};


/* ------------------------------------------------------------------------ */
/* Test vectors                                                             */
/* ------------------------------------------------------------------------ */

struct conv_test_vector {
	const char *name;
	const struct osmo_conv_code *code;
	int in_len;
	int out_len;
	int has_vec;
	pbit_t vec_in[MAX_LEN_BYTES];
	pbit_t vec_out[MAX_LEN_BYTES];
};

static const struct conv_test_vector tests[] = {
	{
		.name = "GSM xCCH (non-recursive, flushed, not punctured)",
		.code = &conv_gsm_xcch,
		.in_len  = 224,
		.out_len = 456,
		.has_vec = 1,
		.vec_in  = { 0xf3, 0x1d, 0xb4, 0x0c, 0x4d, 0x1d, 0x9d, 0xae,
		             0xc0, 0x0a, 0x42, 0x57, 0x13, 0x60, 0x80, 0x96,
		             0xef, 0x23, 0x7e, 0x4c, 0x1d, 0x96, 0x24, 0x19,
		             0x17, 0xf2, 0x44, 0x99 },
		.vec_out = { 0xe9, 0x4d, 0x70, 0xab, 0xa2, 0x87, 0xf0, 0xe7,
		             0x04, 0x14, 0x7c, 0xab, 0xaf, 0x6b, 0xa1, 0x16,
		             0xeb, 0x30, 0x00, 0xde, 0xc8, 0xfd, 0x0b, 0x85,
		             0x80, 0x41, 0x4a, 0xcc, 0xd3, 0xc0, 0xd0, 0xb6,
		             0x26, 0xe5, 0x4e, 0x32, 0x49, 0x69, 0x38, 0x17,
		             0x33, 0xab, 0xaf, 0xb6, 0xc1, 0x08, 0xf3, 0x9f,
		             0x8c, 0x75, 0x6a, 0x4e, 0x08, 0xc4, 0x20, 0x5f,
		             0x8f },
	},
	{
		.name = "GSM TCH/AFS 7.95 (recursive, flushed, punctured)",
		.code = &conv_gsm_tch_afs_7_95,
		.in_len  = 165,
		.out_len = 448,
		.has_vec = 1,
		.vec_in  = { 0x87, 0x66, 0xc3, 0x58, 0x09, 0xd4, 0x06, 0x59,
		             0x10, 0xbf, 0x6b, 0x7f, 0xc8, 0xed, 0x72, 0xaa,
		             0xc1, 0x3d, 0xf3, 0x1e, 0xb0 },
		.vec_out = { 0x92, 0xbc, 0xde, 0xa0, 0xde, 0xbe, 0x01, 0x2f,
		             0xbe, 0xe4, 0x61, 0x32, 0x4d, 0x4f, 0xdc, 0x41,
		             0x43, 0x0d, 0x15, 0xe0, 0x23, 0xdd, 0x18, 0x91,
		             0xe5, 0x36, 0x2d, 0xb7, 0xd9, 0x78, 0xb8, 0xb1,
		             0xb7, 0xcb, 0x2f, 0xc0, 0x52, 0x8f, 0xe2, 0x8c,
		             0x6f, 0xa6, 0x79, 0x88, 0xed, 0x0c, 0x2e, 0x9e,
		             0xa1, 0x5f, 0x45, 0x4a, 0xfb, 0xe6, 0x5a, 0x9c },
	},
	{
		.name = "GMR-1 TCH3 Speech (non-recursive, tail-biting, punctured)",
		.code = &conv_gmr1_tch3_speech,
		.in_len  = 48,
		.out_len = 72,
		.has_vec = 1,
		.vec_in  = { 0x4d, 0xcb, 0xfc, 0x72, 0xf4, 0x8c },
		.vec_out = { 0xc0, 0x86, 0x63, 0x4b, 0x8b, 0xd4, 0x6a, 0x76, 0xb2 },
	},
	{
		.name = "WiMax FCH (non-recursive, tail-biting, not punctured)",
		.code = &conv_wimax_fch,
		.in_len  = 48,
		.out_len = 96,
		.has_vec = 1,
		.vec_in  = { 0xfc, 0xa0, 0xa0, 0xfc, 0xa0, 0xa0 },
		.vec_out = { 0x19, 0x42, 0x8a, 0xed, 0x21, 0xed, 0x19, 0x42,
		             0x8a, 0xed, 0x21, 0xed },
	},
	{
		.name = "??? (non-recursive, direct truncation, not punctured)",
		.code = &conv_trunc,
		.in_len  = 224,
		.out_len = 448,
		.has_vec = 1,
		.vec_in  = { 0xe5, 0xe0, 0x85, 0x7e, 0xf7, 0x08, 0x19, 0x5a,
		             0xb9, 0xad, 0x82, 0x37, 0x98, 0x8b, 0x26, 0xb9,
		             0x81, 0x26, 0x9c, 0x75, 0xaf, 0xf3, 0xcb, 0x07,
		             0xac, 0x63, 0xe2, 0x9c,
		},
		.vec_out = { 0xea, 0x3b, 0x55, 0x0c, 0xd3, 0xf7, 0x85, 0x69,
		             0xe5, 0x79, 0x83, 0xd3, 0xc3, 0x9f, 0xb8, 0x61,
		             0x21, 0x63, 0x51, 0x18, 0xac, 0xcd, 0x32, 0x49,
		             0x53, 0x5c, 0x13, 0x1d, 0xbe, 0x05, 0x11, 0x63,
		             0x5c, 0xc3, 0x42, 0x05, 0x1c, 0x68, 0x0a, 0xb4,
		             0x61, 0x15, 0xaa, 0x4d, 0x94, 0xed, 0xb3, 0x3a,
		             0x5d, 0x1b, 0x09, 0xc2, 0x99, 0x01, 0xec, 0x68 },
	},
	{ /* end */ },
};




/* ------------------------------------------------------------------------ */
/* Main                                                                     */
/* ------------------------------------------------------------------------ */

static void
fill_random(ubit_t *b, int n)
{
	int i;
	for (i=0; i<n; i++)
		b[i] = random() & 1;
}

int main(int argc, char *argv[])
{
	const struct conv_test_vector *tst;
	ubit_t *bu0, *bu1;
	sbit_t *bs;

	srandom(time(NULL));

	bu0 = malloc(sizeof(ubit_t) * MAX_LEN_BITS);
	bu1 = malloc(sizeof(ubit_t) * MAX_LEN_BITS);
	bs  = malloc(sizeof(sbit_t) * MAX_LEN_BITS);

	for (tst=tests; tst->name; tst++)
	{
		int i,l;

		/* Test name */
		printf("[+] Testing: %s\n", tst->name);

		/* Check length */
		l = osmo_conv_get_input_length(tst->code, 0);
		printf("[.] Input length  : ret = %3d  exp = %3d -> %s\n",
			l, tst->in_len, l == tst->in_len ? "OK" : "Bad !");

		if (l != tst->in_len) {
			fprintf(stderr, "[!] Failure for input length computation\n");
			return -1;
		}

		l = osmo_conv_get_output_length(tst->code, 0);
		printf("[.] Output length : ret = %3d  exp = %3d -> %s\n",
			l, tst->out_len, l == tst->out_len ? "OK" : "Bad !");

		if (l != tst->out_len) {
			fprintf(stderr, "[!] Failure for output length computation\n");
			return -1;
		}

		/* Check pre-computed vector */
		if (tst->has_vec) {
			printf("[.] Pre computed vector checks:\n");

			printf("[..] Encoding: ");

			osmo_pbit2ubit(bu0, tst->vec_in, tst->in_len);

			l = osmo_conv_encode(tst->code, bu0, bu1);
			if (l != tst->out_len) {
				printf("ERROR !\n");
				fprintf(stderr, "[!] Failed encoding length check\n");
				return -1;
			}

			osmo_pbit2ubit(bu0, tst->vec_out, tst->out_len);

			if (memcmp(bu0, bu1, tst->out_len)) {
				printf("ERROR !\n");
				fprintf(stderr, "[!] Failed encoding: Results don't match\n");
				return -1;
			};

			printf("OK\n");


			printf("[..] Decoding: ");

			osmo_ubit2sbit(bs, bu0, l);

			l = osmo_conv_decode(tst->code, bs, bu1);
			if (l != 0) {
				printf("ERROR !\n");
				fprintf(stderr, "[!] Failed decoding: non-zero path (%d)\n", l);
				return -1;
			}

			osmo_pbit2ubit(bu0, tst->vec_in, tst->in_len);

			if (memcmp(bu0, bu1, tst->in_len)) {
				printf("ERROR !\n");
				fprintf(stderr, "[!] Failed decoding: Results don't match\n");
				return -1;
			}

			printf("OK\n");
		}

		/* Check random vector */
		printf("[.] Random vector checks:\n");

		for (i=0; i<3; i++) {
			printf("[..] Encoding / Decoding cycle : ");

			fill_random(bu0, tst->in_len);

			l = osmo_conv_encode(tst->code, bu0, bu1);
			if (l != tst->out_len) {
				printf("ERROR !\n");
				fprintf(stderr, "[!] Failed encoding length check\n");
				return -1;
			}

			osmo_ubit2sbit(bs, bu1, l);

			l = osmo_conv_decode(tst->code, bs, bu1);
			if (l != 0) {
				printf("ERROR !\n");
				fprintf(stderr, "[!] Failed decoding: non-zero path (%d)\n", l);
				return -1;
			}

			if (memcmp(bu0, bu1, tst->in_len)) {
				printf("ERROR !\n");
				fprintf(stderr, "[!] Failed decoding: Results don't match\n");
				return -1;
			}

			printf("OK\n");
		}

		/* Spacing */
		printf("\n");
	}

	free(bs);
	free(bu1);
	free(bu0);

	return 0;
}
