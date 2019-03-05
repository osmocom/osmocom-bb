/* TIFFS (TI Flash File System) reader implementation */

/*
 * This code was written by Mychaela Falconia <falcon@freecalypso.org>
 * who refuses to claim copyright on it and has released it as public domain
 * instead. NO rights reserved, all rights relinquished.
 *
 * Tweaked (coding style changes) by Vadim Yanitskiy <axilirator@gmail.com>
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <tiffs.h>
#include "globals.h"

extern char *index();

#define	MAX_FN_COMPONENT	20

static unsigned find_named_child(unsigned start, const char *seekname)
{
	struct tiffs_inode *irec;
	unsigned ino;

	for (ino = start; ino != 0xFFFF; ino = irec->sibling) {
		irec = tiffs_active_index + ino;
		if (!irec->type)
			continue;
		if (!strcmp((const char *) INODE_TO_DATAPTR(irec), seekname))
			return ino;
	}

	return 0;
}

static int pathname_to_inode(const char *pathname)
{
	char seekname[MAX_FN_COMPONENT + 1];
	struct tiffs_inode *irec;
	const char *cur, *next;
	unsigned ino, namelen;

	cur = pathname;
	if (*cur == '/')
		cur++;

	for (ino = tiffs_root_ino; cur; cur = next) {
		if (!*cur)
			break;

		next = index(cur, '/');
		if (next == cur) {
			puts("Malformed TIFFS pathname: multiple adjacent slashes\n");
			return -1;
		} else if (next) {
			namelen = next - cur;
			next++;
		} else {
			namelen = strlen(cur);
		}

		if (namelen > MAX_FN_COMPONENT) {
			puts("Malformed TIFFS pathname: component name too long\n");
			return -1;
		}

		memcpy(seekname, cur, namelen);
		seekname[namelen] = '\0';

		irec = tiffs_active_index + ino;
		if (irec->type != TIFFS_OBJTYPE_DIR) {
			/* printf("Error: non-terminal non-directory\n"); */
			return 0;
		}
		ino = find_named_child(irec->descend, seekname);
		if (!ino) {
			/* printf("Error: pathname component not found\n"); */
			return 0;
		}
	}

	return ino;
}

static uint8_t *find_endofchunk(int ino)
{
	struct tiffs_inode *irec;
	uint8_t *ptr;
	int i;

	irec = tiffs_active_index + ino;
	ptr = INODE_TO_DATAPTR(irec) + irec->len;

	for (i = 0; i < 16; i++) {
		ptr--;
		if (!*ptr)
			return ptr;
		if (*ptr != 0xFF)
			break;
	}

	printf("TIFFS error: inode #%x has no valid termination\n", ino);
	return ptr; /* XXX */
}

static int get_file_head(const char *pathname, uint8_t **startret,
			 size_t *sizeret, int *continue_ret)
{
	struct tiffs_inode *irec;
	uint8_t *start, *end;
	int ino, cont, size;

	ino = pathname_to_inode(pathname);
	if (ino <= 0)
		return ino;

	irec = tiffs_active_index + ino;
	if (irec->type != TIFFS_OBJTYPE_FILE) {
		printf("TIFFS error: '%s' is not a regular file\n", pathname);
		return -1;
	}

	start = INODE_TO_DATAPTR(irec);
	start += strlen((const char *) start) + 1;
	end = find_endofchunk(ino);

	size = end - start;
	if (size < 0)
		size = 0;

	cont = irec->descend;
	if (cont == 0xFFFF)
		cont = 0;
	if (startret)
		*startret = start;
	if (sizeret)
		*sizeret = size;
	if (continue_ret)
		*continue_ret = cont;

	return 1;
}

static int get_segment(int ino, uint8_t **startret,
		       size_t *sizeret, int *continue_ret)
{
	struct tiffs_inode *irec;
	uint8_t *start, *end;
	int cont, size;

	for (;;) {
		irec = tiffs_active_index + ino;
		if (irec->type)
			break;
		if (irec->sibling == 0xFFFF) {
			printf("TIFFS error: segment inode #%d: "
				"deleted and no sibling\n", ino);
			return -1;
		}
		ino = irec->sibling;
	}

	if (irec->type != TIFFS_OBJTYPE_SEGMENT) {
		printf("TIFFS error: inode #%x is not a segment\n", ino);
		return -1;
	}

	start = INODE_TO_DATAPTR(irec);
	end = find_endofchunk(ino);

	size = end - start;
	if (size <= 0) {
		printf("TIFFS error: segment inode #%x has bad length\n", ino);
		return -1;
	}

	cont = irec->descend;
	if (cont == 0xFFFF)
		cont = 0;
	if (startret)
		*startret = start;
	if (sizeret)
		*sizeret = size;
	if (continue_ret)
		*continue_ret = cont;

	return 0;
}

int tiffs_read_file_maxlen(const char *pathname, uint8_t *buf,
			   size_t maxlen, size_t *lenrtn)
{
	size_t chunk_size, real_len, roomleft;
	uint8_t *chunk_start;
	int stat, cont;

	if (!tiffs_init_done) {
		puts("TIFFS reader is not initialized\n");
		return -1;
	}

	stat = get_file_head(pathname, &chunk_start, &chunk_size, &cont);
	if (stat <= 0)
		return stat;
	if (chunk_size > maxlen) {
toobig:		printf("TIFFS error: '%s' is bigger than the read buffer\n", pathname);
		return -1;
	}

	real_len = chunk_size;
	memcpy(buf, chunk_start, chunk_size);
	buf += chunk_size;
	roomleft = maxlen - chunk_size;
	while (cont) {
		stat = get_segment(cont, &chunk_start, &chunk_size, &cont);
		if (stat < 0)
			return stat;
		if (chunk_size > roomleft)
			goto toobig;

		real_len += chunk_size;
		memcpy(buf, chunk_start, chunk_size);
		buf += chunk_size;
		roomleft -= chunk_size;
	}

	if (lenrtn)
		*lenrtn = real_len;

	return 1;
}

int tiffs_read_file_fixedlen(const char *pathname, uint8_t *buf,
			     size_t expect_len)
{
	size_t real_len;
	int rc;

	rc = tiffs_read_file_maxlen(pathname, buf, expect_len, &real_len);
	if (rc <= 0)
		return rc;
	if (real_len != expect_len) {
		printf("TIFFS error: '%s' has the wrong length\n", pathname);
		return -1;
	}

	return 1;
}
