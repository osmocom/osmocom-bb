/* GSM/GPRS/3G authentication core infrastructure */

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

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/plugin.h>

#include <osmocom/crypt/auth.h>

/*! \addtogroup auth
 *  @{
 */

/* \file auth_core.c
 */

static LLIST_HEAD(osmo_auths);

static struct osmo_auth_impl *selected_auths[_OSMO_AUTH_ALG_NUM];

/*! \brief Register an authentication algorithm implementation with the core
 *  \param[in] impl Structure describing implementation and it's callbacks
 *
 * This function is called by an authentication implementation plugin to
 * register itself with the authentication core.
 */
int osmo_auth_register(struct osmo_auth_impl *impl)
{
	if (impl->algo >= ARRAY_SIZE(selected_auths))
		return -ERANGE;

	llist_add_tail(&impl->list, &osmo_auths);

	/* check if we want to select this implementation over others */
	if (!selected_auths[impl->algo] ||
	    (selected_auths[impl->algo]->priority > impl->priority))
		selected_auths[impl->algo] = impl;

	return 0;
}

/*! \brief Load all available authentication plugins from the given path
 *  \param[in] path Path name of the directory containing the plugins
 *
 * This function will load all plugins contained in the specified path.
 */
int osmo_auth_load(const char *path)
{
	/* load all plugins available from path */
	return osmo_plugin_load_all(path);
}

/*! \brief Determine if a given authentication algorithm is supported
 *  \param[in] algo Algorithm which should be checked
 *
 * This function is used by an application to determine at runtime if a
 * given authentication algorithm is supported or not.
 */
int osmo_auth_supported(enum osmo_auth_algo algo)
{
	if (algo >= ARRAY_SIZE(selected_auths))
		return -ERANGE;

	if (selected_auths[algo])
		return 1;

	return 0;
}

/*! \brief Generate authentication vector
 *  \param[out] vec Generated authentication vector
 *  \param[in] aud Subscriber-specific key material
 *  \param[in] _rand Random challenge to be used
 *
 * This function performs the core cryptographic function of the AUC,
 * computing authentication triples/quintuples based on the permanent
 * subscriber data and a random value.  The result is what is forwarded
 * by the AUC via HLR and VLR to the MSC which will then be able to
 * invoke authentication with the MS
 */
int osmo_auth_gen_vec(struct osmo_auth_vector *vec,
		      struct osmo_sub_auth_data *aud,
		      const uint8_t *_rand)
{
	struct osmo_auth_impl *impl = selected_auths[aud->algo];
	int rc;

	if (!impl)
		return -ENOENT;

	rc = impl->gen_vec(vec, aud, _rand);
	if (rc < 0)
		return rc;

	memcpy(vec->rand, _rand, sizeof(vec->rand));

	return 0;
}

/*! \brief Generate authentication vector and re-sync sequence
 *  \param[out] vec Generated authentication vector
 *  \param[in] aud Subscriber-specific key material
 *  \param[in] rand_auts RAND value sent by the SIM/MS
 *  \param[in] auts AUTS value sent by the SIM/MS
 *  \param[in] _rand Random challenge to be used to generate vector
 *
 * This function performs a special variant of the  core cryptographic
 * function of the AUC: computing authentication triples/quintuples
 * based on the permanent subscriber data, a random value as well as the
 * AUTS and RAND values returned by the SIM/MS.  This special variant is
 * needed if the sequence numbers between MS and AUC have for some
 * reason become diffrent.
 */
int osmo_auth_gen_vec_auts(struct osmo_auth_vector *vec,
			   struct osmo_sub_auth_data *aud,
			   const uint8_t *rand_auts, const uint8_t *auts,
			   const uint8_t *_rand)
{
	struct osmo_auth_impl *impl = selected_auths[aud->algo];

	if (!impl || !impl->gen_vec_auts)
		return -ENOENT;

	return impl->gen_vec_auts(vec, aud, rand_auts, auts, _rand);
}

static const struct value_string auth_alg_vals[] = {
	{ OSMO_AUTH_ALG_NONE, "None" },
	{ OSMO_AUTH_ALG_COMP128v1, "COMP128v1" },
	{ OSMO_AUTH_ALG_COMP128v2, "COMP128v2" },
	{ OSMO_AUTH_ALG_COMP128v3, "COMP128v3" },
	{ OSMO_AUTH_ALG_XOR, "XOR" },
	{ OSMO_AUTH_ALG_MILENAGE, "MILENAGE" },
	{ 0, NULL }
};

/*! \brief Get human-readable name of authentication algorithm */
const char *osmo_auth_alg_name(enum osmo_auth_algo alg)
{
	return get_value_string(auth_alg_vals, alg);
}

/*! \brief Parse human-readable name of authentication algorithm */
enum osmo_auth_algo osmo_auth_alg_parse(const char *name)
{
	return get_string_value(auth_alg_vals, name);
}

/*! @} */
