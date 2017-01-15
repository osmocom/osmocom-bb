/* registers COMP128 version 2 and 3 A3/A8 algorithms for the
 * GSM/GPRS/3G authentication core infrastructure
 *
 */

/* (C) 2010-2011 by Harald Welte <laforge@gnumonks.org>
 * (C) 2013 by Kévin Redon <kevredon@mail.tsaitgaist.info>
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
#include <osmocom/gsm/comp128v23.h>

static int c128v2_gen_vec(struct osmo_auth_vector *vec,
			  struct osmo_sub_auth_data *aud,
			  const uint8_t *_rand)
{
	comp128v2(aud->u.gsm.ki, _rand, vec->sres, vec->kc);
	vec->auth_types = OSMO_AUTH_TYPE_GSM;

	return 0;
}

static struct osmo_auth_impl c128v2_alg = {
	.algo = OSMO_AUTH_ALG_COMP128v2,
	.name = "COMP128v2 (libosmogsm built-in)",
	.priority = 1000,
	.gen_vec = &c128v2_gen_vec,
};

static int c128v3_gen_vec(struct osmo_auth_vector *vec,
			  struct osmo_sub_auth_data *aud,
			  const uint8_t *_rand)
{
	comp128v3(aud->u.gsm.ki, _rand, vec->sres, vec->kc);
	vec->auth_types = OSMO_AUTH_TYPE_GSM;

	return 0;
}

static struct osmo_auth_impl c128v3_alg = {
	.algo = OSMO_AUTH_ALG_COMP128v3,
	.name = "COMP128v3 (libosmogsm built-in)",
	.priority = 1000,
	.gen_vec = &c128v3_gen_vec,
};

static __attribute__((constructor)) void on_dso_load_c128(void)
{
	osmo_auth_register(&c128v2_alg);
	osmo_auth_register(&c128v3_alg);
}
