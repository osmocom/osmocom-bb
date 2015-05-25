/* GSM/GPRS/3G authentication testing tool */

/* (C) 2010-2012 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>

#include <osmocom/crypt/auth.h>
#include <osmocom/core/utils.h>

static void dump_triplets_dat(struct osmo_auth_vector *vec)
{
	if (vec->auth_types & OSMO_AUTH_TYPE_UMTS) {
		fprintf(stderr, "triplets.dat doesn't support UMTS!\n");
		return;
	}
	printf("imsi,");
	printf("%s,", osmo_hexdump_nospc(vec->rand, sizeof(vec->rand)));
	printf("%s,", osmo_hexdump_nospc(vec->sres, sizeof(vec->sres)));
	printf("%s\n", osmo_hexdump_nospc(vec->kc, sizeof(vec->kc)));
}

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
	.type = OSMO_AUTH_TYPE_NONE,
	.algo = OSMO_AUTH_ALG_NONE,
};

static void help()
{
	printf( "-2  --2g\tUse 2G (GSM) authentication\n"
		"-3  --3g\tUse 3G (UMTS) authentication\n"
		"-a  --algorithm\tSpecify name of the algorithm\n"
		"-k  --key\tSpecify Ki / K\n"
		"-o  --opc\tSpecify OPC (only for 3G)\n"
		"-O  --op\tSpecify OP (only for 3G)\n"
		"-f  --amf\tSpecify AMF (only for 3G)\n"
		"-s  --sqn\tSpecify SQN (only for 3G)\n"
		"-A  --auts\tSpecify AUTS (only for 3G)\n"
		"-r  --rand\tSpecify random value\n"
		"-I  --ipsec\tOutput in triplets.dat format for strongswan\n");
}

int main(int argc, char **argv)
{
	struct osmo_auth_vector _vec;
	struct osmo_auth_vector *vec = &_vec;
	uint8_t _rand[16], _auts[16];
	int rc, option_index;
	int rand_is_set = 0;
	int auts_is_set = 0;
	int fmt_triplets_dat = 0;

	printf("osmo-auc-gen (C) 2011-2012 by Harald Welte\n");
	printf("This is FREE SOFTWARE with ABSOLUTELY NO WARRANTY\n\n");

	memset(_auts, 0, sizeof(_auts));

	while (1) {
		int c;
		unsigned long ul;
		static struct option long_options[] = {
			{ "2g", 0, 0, '2' },
			{ "3g", 0, 0, '3' },
			{ "algorithm", 1, 0, 'a' },
			{ "key", 1, 0, 'k' },
			{ "opc", 1, 0, 'o' },
			{ "op", 1, 0, 'O' },
			{ "amf", 1, 0, 'f' },
			{ "sqn", 1, 0, 's' },
			{ "rand", 1, 0, 'r' },
			{ "auts", 1, 0, 'A' },
			{ "help", 0, 0, 'h' },
			{ 0, 0, 0, 0 }
		};

		rc = 0;

		c = getopt_long(argc, argv, "23a:k:o:f:s:r:hO:A:I", long_options,
				&option_index);

		if (c == -1)
			break;

		switch (c) {
		case '2':
			test_aud.type = OSMO_AUTH_TYPE_GSM;
			break;
		case '3':
			test_aud.type = OSMO_AUTH_TYPE_UMTS;
			break;
		case 'a':
			rc = osmo_auth_alg_parse(optarg);
			if (rc < 0)
				break;
			test_aud.algo = rc;
			break;
		case 'k':
			switch (test_aud.type) {
			case OSMO_AUTH_TYPE_GSM:
				rc = osmo_hexparse(optarg, test_aud.u.gsm.ki,
						   sizeof(test_aud.u.gsm.ki));
				break;
			case OSMO_AUTH_TYPE_UMTS:
				rc = osmo_hexparse(optarg, test_aud.u.umts.k,
						   sizeof(test_aud.u.umts.k));
				break;
			default:
				fprintf(stderr, "please specify 2g/3g first!\n");
			}
			break;
		case 'o':
			if (test_aud.type != OSMO_AUTH_TYPE_UMTS) {
				fprintf(stderr, "Only UMTS has OPC\n");
				exit(2);
			}
			rc = osmo_hexparse(optarg, test_aud.u.umts.opc,
					   sizeof(test_aud.u.umts.opc));
			test_aud.u.umts.opc_is_op = 0;
			break;
		case 'O':
			if (test_aud.type != OSMO_AUTH_TYPE_UMTS) {
				fprintf(stderr, "Only UMTS has OP\n");
				exit(2);
			}
			rc = osmo_hexparse(optarg, test_aud.u.umts.opc,
					   sizeof(test_aud.u.umts.opc));
			test_aud.u.umts.opc_is_op = 1;
			break;
		case 'A':
			if (test_aud.type != OSMO_AUTH_TYPE_UMTS) {
				fprintf(stderr, "Only UMTS has AUTS\n");
				exit(2);
			}
			rc = osmo_hexparse(optarg, _auts, sizeof(_auts));
			auts_is_set = 1;
			break;
		case 'f':
			if (test_aud.type != OSMO_AUTH_TYPE_UMTS) {
				fprintf(stderr, "Only UMTS has AMF\n");
				exit(2);
			}
			rc = osmo_hexparse(optarg, test_aud.u.umts.amf,
					   sizeof(test_aud.u.umts.amf));
			break;
		case 's':
			if (test_aud.type != OSMO_AUTH_TYPE_UMTS) {
				fprintf(stderr, "Only UMTS has SQN\n");
				exit(2);
			}
			ul = strtoul(optarg, 0, 10);
			test_aud.u.umts.sqn = ul;
			break;
		case 'r':
			rc = osmo_hexparse(optarg, _rand, sizeof(_rand));
			rand_is_set = 1;
			break;
		case 'I':
			fmt_triplets_dat = 1;
			break;
		case 'h':
			help();
			exit(0);
		default:
			help();
			exit(1);
		}

		if (rc < 0) {
			fprintf(stderr, "Error parsing argument of option `%c'\n", c);
			exit(2);
		}
	}

	if (!rand_is_set) {
		int i;
		printf("WARNING: We're using really weak random numbers!\n\n");
		srand(time(NULL));

		for (i = 0; i < 4; ++i) {
			uint32_t r;
			r = rand();
			memcpy(&_rand[i*4], &r, 4);
		}
	}

	if (test_aud.type == OSMO_AUTH_TYPE_NONE ||
	    test_aud.algo == OSMO_AUTH_ALG_NONE) {
		help();
		exit(2);
	}

	memset(vec, 0, sizeof(*vec));

	if (!auts_is_set)
		rc = osmo_auth_gen_vec(vec, &test_aud, _rand);
	else
		rc = osmo_auth_gen_vec_auts(vec, &test_aud, _auts, _rand, _rand);
	if (rc < 0) {
		if (!auts_is_set)
			fprintf(stderr, "error generating auth vector\n");
		else
			fprintf(stderr, "AUTS from MS seems incorrect\n");
		exit(1);
	}

	if (fmt_triplets_dat)
		dump_triplets_dat(vec);
	else
		dump_auth_vec(vec);

	if (auts_is_set)
		printf("AUTS success: SEQ.MS = %" PRIu64 "\n", test_aud.u.umts.sqn);

	exit(0);
}
