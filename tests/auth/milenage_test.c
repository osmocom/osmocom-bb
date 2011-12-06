
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <osmocom/crypt/auth.h>
#include <osmocom/core/utils.h>

static void dump_auth_vec(struct osmo_auth_vector *vec)
{
	printf("RAND:\t%s\n", osmo_hexdump(vec->rand, sizeof(vec->rand)));

	if (vec->auth_types & OSMO_AUTH_TYPE_UMTS) {
		printf("AUTN:\t%s\n", osmo_hexdump(vec->autn, sizeof(vec->autn)));
		printf("IK:\t%s\n", osmo_hexdump(vec->ik, sizeof(vec->ik)));
		printf("CK:\t%s\n", osmo_hexdump(vec->ck, sizeof(vec->ck)));
		printf("RES:\t%s\n", osmo_hexdump(vec->res, vec->res_len));
	}

	if (vec->auth_types & OSMO_AUTH_TYPE_GSM) {
		printf("SRES:\t%s\n", osmo_hexdump(vec->sres, sizeof(vec->sres)));
		printf("Kc:\t%s\n", osmo_hexdump(vec->kc, sizeof(vec->kc)));
	}
}

static struct osmo_sub_auth_data test_aud = {
	.type = OSMO_AUTH_TYPE_UMTS,
	.algo = OSMO_AUTH_ALG_MILENAGE,
	.umts = {
		.opc = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f },
		.k =   { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f },
		.amf = { 0x00, 0x00 },
		.sqn = 0x22,
	},
};

int main(int argc, char **argv)
{
	struct osmo_auth_vector _vec;
	struct osmo_auth_vector *vec = &_vec;
	uint8_t _rand[16];
	int rc;

#if 0
	srand(time(NULL));
	*(uint32_t *)&_rand[0] = rand();
	*(uint32_t *)(&_rand[4]) = rand();
	*(uint32_t *)(&_rand[8]) = rand();
	*(uint32_t *)(&_rand[12]) = rand();
#else
	memset(_rand, 0, sizeof(_rand));
#endif
	memset(vec, 0, sizeof(*vec));

	rc = osmo_auth_gen_vec(vec, &test_aud, _rand);
	if (rc < 0) {
		fprintf(stderr, "error generating auth vector\n");
		exit(1);
	}

	dump_auth_vec(vec);

	const uint8_t auts[14] = { 0x87, 0x11, 0xa0, 0xec, 0x9e, 0x16, 0x37, 0xdf,
			     0x17, 0xf8, 0x0b, 0x38, 0x4e, 0xe4 };

	rc = osmo_auth_gen_vec_auts(vec, &test_aud, auts, _rand, _rand);
	if (rc < 0) {
		printf("AUTS failed\n");
	} else {
		printf("AUTS success: SEQ.MS = %lu\n", test_aud.umts.sqn);
	}

	exit(0);

}
