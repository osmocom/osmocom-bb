/* GSM/GPRS/3G authentication core infrastructure */

/* (C) 2011 by Harald Welte <laforge@gnumonks.org>
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

#include <osmocom/crypt/auth.h>
#include <osmocom/core/bits.h>
#include "milenage/common.h"
#include "milenage/milenage.h"

static int milenage_gen_vec(struct osmo_auth_vector *vec,
			    struct osmo_sub_auth_data *aud,
			    const uint8_t *_rand)
{
	size_t res_len = sizeof(vec->res);
	uint8_t sqn[6];
	int rc;

	osmo_store64be_ext(aud->u.umts.sqn, sqn, 6);
	milenage_generate(aud->u.umts.opc, aud->u.umts.amf, aud->u.umts.k,
			  sqn, _rand,
			  vec->autn, vec->ik, vec->ck, vec->res, &res_len);
	vec->res_len = res_len;
	rc = gsm_milenage(aud->u.umts.opc, aud->u.umts.k, _rand, vec->sres, vec->kc);
	if (rc < 0)
		return rc;

	vec->auth_types = OSMO_AUTH_TYPE_UMTS | OSMO_AUTH_TYPE_GSM;
	aud->u.umts.sqn++;

	return 0;
}

static int milenage_gen_vec_auts(struct osmo_auth_vector *vec,
				 struct osmo_sub_auth_data *aud,
				 const uint8_t *auts, const uint8_t *rand_auts,
				 const uint8_t *_rand)
{
	uint8_t sqn_out[6];
	uint8_t gen_opc[16];
	uint8_t *opc;
	int rc;

	/* Check if we only know OP and compute OPC if required */
	if (aud->type == OSMO_AUTH_TYPE_UMTS && aud->u.umts.opc_is_op) {
		rc = milenage_opc_gen(gen_opc, aud->u.umts.k,
				      aud->u.umts.opc);
		if (rc < 0)
			return rc;
		opc = gen_opc;
	} else
		opc = aud->u.umts.opc;

	rc = milenage_auts(opc, aud->u.umts.k, rand_auts, auts, sqn_out);
	if (rc < 0)
		return rc;

	aud->u.umts.sqn = 1 + (osmo_load64be_ext(sqn_out, 6) >> 16);

	return milenage_gen_vec(vec, aud, _rand);
}

static struct osmo_auth_impl milenage_alg = {
	.algo = OSMO_AUTH_ALG_MILENAGE,
	.name = "MILENAGE (libosmogsm built-in)",
	.priority = 1000,
	.gen_vec = &milenage_gen_vec,
	.gen_vec_auts = &milenage_gen_vec_auts,
};

static __attribute__((constructor)) void on_dso_load_milenage(void)
{
	osmo_auth_register(&milenage_alg);
}
