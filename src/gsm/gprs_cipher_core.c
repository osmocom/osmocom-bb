/* GPRS LLC cipher core infrastructure */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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

#include <osmocom/crypt/gprs_cipher.h>

static LLIST_HEAD(gprs_ciphers);

static struct gprs_cipher_impl *selected_ciphers[_GPRS_ALGO_NUM];

/* register a cipher with the core */
int gprs_cipher_register(struct gprs_cipher_impl *ciph)
{
	if (ciph->algo >= ARRAY_SIZE(selected_ciphers))
		return -ERANGE;

	llist_add_tail(&ciph->list, &gprs_ciphers);

	/* check if we want to select this implementation over others */
	if (!selected_ciphers[ciph->algo] ||
	    (selected_ciphers[ciph->algo]->priority > ciph->priority))
		selected_ciphers[ciph->algo] = ciph;

	return 0;
}

/* load all available GPRS cipher plugins */
int gprs_cipher_load(const char *path)
{
	/* load all plugins available from path */
	return osmo_plugin_load_all(path);
}

/* function to be called by core code */
int gprs_cipher_run(uint8_t *out, uint16_t len, enum gprs_ciph_algo algo,
		    uint64_t kc, uint32_t iv, enum gprs_cipher_direction dir)
{
	if (algo >= ARRAY_SIZE(selected_ciphers))
		return -ERANGE;

	if (!selected_ciphers[algo])
		return -EINVAL;

	if (len > GSM0464_CIPH_MAX_BLOCK)
		return -ERANGE;

	/* run the actual cipher from the plugin */
	return selected_ciphers[algo]->run(out, len, kc, iv, dir);
}

int gprs_cipher_supported(enum gprs_ciph_algo algo)
{
	if (algo >= ARRAY_SIZE(selected_ciphers))
		return -ERANGE;

	if (selected_ciphers[algo])
		return 1;

	return 0;
}

/* GSM TS 04.64 / Section A.2.1 : Generation of 'input' */
uint32_t gprs_cipher_gen_input_ui(uint32_t iov_ui, uint8_t sapi, uint32_t lfn, uint32_t oc)
{
	uint32_t sx = ((1<<27) * sapi) + (1<<31);

	return (iov_ui ^ sx) + lfn + oc;
}

/* GSM TS 04.64 / Section A.2.1 : Generation of 'input' */
uint32_t gprs_cipher_gen_input_i(uint32_t iov_i, uint32_t lfn, uint32_t oc)
{
	return iov_i + lfn + oc;
}
