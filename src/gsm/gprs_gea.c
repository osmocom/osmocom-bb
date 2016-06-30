/*
 * gprs_gea.c
 *
 * GEA 3 & 4 plugin
 *
 * Copyright (C) 2016 by Sysmocom s.f.m.c. GmbH
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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
 */

#include <osmocom/crypt/gprs_cipher.h>
#include <osmocom/gsm/gea.h>

#include <stdint.h>

static struct gprs_cipher_impl gea3_impl = {
	.algo = GPRS_ALGO_GEA3,
	.name = "GEA3 (libosmogsm built-in)",
	.priority = 100,
	.run = &gea3,
};

static struct gprs_cipher_impl gea4_impl = {
	.algo = GPRS_ALGO_GEA4,
	.name = "GEA4 (libosmogsm built-in)",
	.priority = 100,
	.run = &gea4,
};

static __attribute__((constructor)) void on_dso_load_gea(void)
{
	gprs_cipher_register(&gea3_impl);
	gprs_cipher_register(&gea4_impl);
}
