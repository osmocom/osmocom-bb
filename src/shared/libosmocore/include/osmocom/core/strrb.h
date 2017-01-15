#pragma once

/* (C) 2012-2013 by Katerina Barone-Adesi <kat.obsc@gmail.com>
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

/*! \defgroup osmo_strrb Osmocom ringbuffers for log strings
 *  @{
 */

/*! \file strrb.h
 *  \brief Osmocom string ringbuffer handling routines
 */

#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#include <osmocom/core/talloc.h>

/*! \brief A structure representing an osmocom string ringbuffer */

#define RB_MAX_MESSAGE_SIZE 240
struct osmo_strrb {
	uint16_t start;		/*!< \brief index of the first slot */
	uint16_t end;		/*!< \brief index of the last slot */
	uint16_t size;		/*!< \brief max number of messages to store */
	char **buffer;		/*!< \brief storage for messages */
};

struct osmo_strrb *osmo_strrb_create(TALLOC_CTX * ctx, size_t rb_size);
bool osmo_strrb_is_empty(const struct osmo_strrb *rb);
const char *osmo_strrb_get_nth(const struct osmo_strrb *rb,
			       unsigned int string_index);
bool _osmo_strrb_is_bufindex_valid(const struct osmo_strrb *rb,
				   unsigned int offset);
size_t osmo_strrb_elements(const struct osmo_strrb *rb);
int osmo_strrb_add(struct osmo_strrb *rb, const char *data);

/*! @} */
