/* GSM/GPRS/3G authentication core infrastructure */

/* (C) 2010-2011 by Harald Welte <laforge@gnumonks.org>
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

#include <osmocom/core/utils.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/plugin.h>

#include <osmocom/crypt/auth.h>

static LLIST_HEAD(osmo_auths);

static struct osmo_auth_impl *selected_auths[_OSMO_AUTH_ALG_NUM];

/* register a cipher with the core */
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

/* load all available GPRS cipher plugins */
int osmo_auth_load(const char *path)
{
	/* load all plugins available from path */
	return osmo_plugin_load_all(path);
}

int osmo_auth_supported(enum osmo_auth_algo algo)
{
	if (algo >= ARRAY_SIZE(selected_auths))
		return -ERANGE;

	if (selected_auths[algo])
		return 1;

	return 0;
}

int osmo_auth_gen_vec(struct osmo_auth_vector *vec,
		      struct osmo_sub_auth_data *aud,
		      const uint8_t *_rand)
{
	struct osmo_auth_impl *impl = selected_auths[aud->type];

	if (!impl)
		return -ENOENT;

	return impl->gen_vec(vec, aud, _rand);
}

int osmo_auth_gen_vec_auts(struct osmo_auth_vector *vec,
			   struct osmo_sub_auth_data *aud,
			   const uint8_t *rand_auts, const uint8_t *auts,
			   const uint8_t *_rand)
{
	struct osmo_auth_impl *impl = selected_auths[aud->type];

	if (!impl || !impl->gen_vec_auts)
		return -ENOENT;

	return impl->gen_vec_auts(vec, aud, rand_auts, auts, _rand);
}
