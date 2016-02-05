#pragma once

/* bit compression routines */

/* (C) 2016 sysmocom s.f.m.c. GmbH by Max Suraev <msuraev@sysmocom.de>
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

/*! \defgroup bitcomp Bit compression
 *  @{
 */

/*! \file bitcomp.h
 *  \brief Osmocom bit compression routines
 */

#include <stdint.h>
#include <stdbool.h>

#include <osmocom/core/bitvec.h>


int osmo_t4_encode(struct bitvec *bv);
int osmo_t4_decode(const struct bitvec *in, bool cc, struct bitvec *out);

/*! @} */
