/* Ringbuffer implementation, tailored for logging.
 * This is a lossy ringbuffer. It keeps up to N of the newest messages,
 * overwriting the oldest as newer ones come in.
 *
 * (C) 2012-2013, Katerina Barone-Adesi <kat.obsc@gmail.com>
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

/*! \file strrb.c
 *  \brief Lossy string ringbuffer for logging; keeps newest messages.
 */

#include <stdio.h>
#include <string.h>
#include <string.h>

#include <osmocom/core/strrb.h>

/* Ringbuffer assumptions, invarients, and notes:
 * - start is the index of the first used index slot in the ring buffer.
 * - end is the index of the next index slot in the ring buffer.
 * - start == end => buffer is empty
 * - Consequence: the buffer can hold at most size - 1 messages
 * (if this were not the case, full and empty buffers would be indistinguishable
 * given the conventions in this implementation).
 * - Whenever the ringbuffer is full, start is advanced. The second oldest
 * message becomes unreachable by valid indexes (end is not a valid index)
 * and the oldest message is overwritten (if there was a message there, which
 * is the case unless this is the first time the ringbuffer becomes full).
*/

/*! \brief Create an empty, initialized osmo_strrb.
 *  \param[in] ctx The talloc memory context which should own this.
 *  \param[in] rb_size The number of message slots the osmo_strrb can hold.
 *  \returns A struct osmo_strrb* on success, NULL in case of error.
 *
 * This function creates and initializes a ringbuffer.
 * Note that the ringbuffer stores at most rb_size - 1 messages.
 */

struct osmo_strrb *osmo_strrb_create(TALLOC_CTX * ctx, size_t rb_size)
{
	struct osmo_strrb *rb = NULL;
	unsigned int i;

	rb = talloc_zero(ctx, struct osmo_strrb);
	if (!rb)
		goto alloc_error;

	/* start and end are zero already, which is correct */
	rb->size = rb_size;

	rb->buffer = talloc_array(rb, char *, rb->size);
	if (!rb->buffer)
		goto alloc_error;
	for (i = 0; i < rb->size; i++) {
		rb->buffer[i] =
		    talloc_zero_size(rb->buffer, RB_MAX_MESSAGE_SIZE);
		if (!rb->buffer[i])
			goto alloc_error;
	}

	return rb;

alloc_error:			/* talloc_free(NULL) is safe */
	talloc_free(rb);
	return NULL;
}

/*! \brief Check if an osmo_strrb is empty.
 *  \param[in] rb The osmo_strrb to check.
 *  \returns True if the osmo_strrb is empty, false otherwise.
 */
bool osmo_strrb_is_empty(const struct osmo_strrb *rb)
{
	return rb->end == rb->start;
}

/*! \brief Return a pointer to the Nth string in the osmo_strrb.
 * \param[in] rb The osmo_strrb to search.
 * \param[in] string_index The index sought (N), zero-indexed.
 *
 * Return a pointer to the Nth string in the osmo_strrb.
 * Return NULL if there is no Nth string.
 * Note that N is zero-indexed.
 * \returns A pointer to the target string on success, NULL in case of error.
 */
const char *osmo_strrb_get_nth(const struct osmo_strrb *rb,
			       unsigned int string_index)
{
	unsigned int offset = string_index + rb->start;

	if ((offset >= rb->size) && (rb->start > rb->end))
		offset -= rb->size;
	if (_osmo_strrb_is_bufindex_valid(rb, offset))
		return rb->buffer[offset];

	return NULL;
}

bool _osmo_strrb_is_bufindex_valid(const struct osmo_strrb *rb,
				   unsigned int bufi)
{
	if (osmo_strrb_is_empty(rb))
		return 0;
	if (bufi >= rb->size)
		return 0;
	if (rb->start < rb->end)
		return (bufi >= rb->start) && (bufi < rb->end);
	return (bufi < rb->end) || (bufi >= rb->start);
}

/*! \brief Count the number of log messages in an osmo_strrb.
 *  \param[in] rb The osmo_strrb to count the elements of.
 *
 *  \returns The number of log messages in the osmo_strrb.
 */
size_t osmo_strrb_elements(const struct osmo_strrb *rb)
{
	if (rb->end < rb->start)
		return rb->end + (rb->size - rb->start);

	return rb->end - rb->start;
}

/*! \brief Add a string to the osmo_strrb.
 * \param[in] rb The osmo_strrb to add to.
 * \param[in] data The string to add.
 *
 * Add a message to the osmo_strrb.
 * Older messages will be overwritten as necessary.
 * \returns 0 normally, 1 as a warning (ie, if data was truncated).
 */
int osmo_strrb_add(struct osmo_strrb *rb, const char *data)
{
	size_t len = strlen(data);
	int ret = 0;

	if (len >= RB_MAX_MESSAGE_SIZE) {
		len = RB_MAX_MESSAGE_SIZE - 1;
		ret = 1;
	}

	memcpy(rb->buffer[rb->end], data, len);
	rb->buffer[rb->end][len] = '\0';

	rb->end += 1;
	rb->end %= rb->size;

	/* The buffer is full; oldest message is forgotten - see notes above */
	if (rb->end == rb->start) {
		rb->start += 1;
		rb->start %= rb->size;
	}
	return ret;
}
