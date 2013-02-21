/* (C) 2012-2013 by Katerina Barone-Adesi <kat.obsc@gmail.com>
 * All Rights Reserved
 *
 * This program is iree software; you can redistribute it and/or modify
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
 *
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <osmocom/core/strrb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/logging.h>

struct osmo_strrb *rb0, *rb1, *rb2, *rb3, *rb4, *rb5;

#define STR0 "hello"
#define STR1 "a"
#define STR2 "world"
#define STR3 "sky"
#define STR4 "moon"

#define TESTSIZE 2

void init_rbs(void)
{
	rb0 = osmo_strrb_create(NULL, TESTSIZE);

	rb1 = osmo_strrb_create(NULL, TESTSIZE);
	osmo_strrb_add(rb1, STR0);

	rb2 = osmo_strrb_create(NULL, TESTSIZE);
	osmo_strrb_add(rb2, STR0);
	osmo_strrb_add(rb2, STR1);

	rb3 = osmo_strrb_create(NULL, TESTSIZE);
	osmo_strrb_add(rb3, STR0);
	osmo_strrb_add(rb3, STR1);
	osmo_strrb_add(rb3, STR2);

	rb4 = osmo_strrb_create(NULL, TESTSIZE);
	osmo_strrb_add(rb4, STR0);
	osmo_strrb_add(rb4, STR1);
	osmo_strrb_add(rb4, STR2);
	osmo_strrb_add(rb4, STR3);

	rb5 = osmo_strrb_create(NULL, TESTSIZE);
	osmo_strrb_add(rb5, STR0);
	osmo_strrb_add(rb5, STR1);
	osmo_strrb_add(rb5, STR2);
	osmo_strrb_add(rb5, STR3);
	osmo_strrb_add(rb5, STR4);
}

void free_rbs(void)
{
	talloc_free(rb0);
	talloc_free(rb1);
	talloc_free(rb2);
	talloc_free(rb3);
	talloc_free(rb4);
	talloc_free(rb5);
}

void test_offset_valid(void)
{
	assert(_osmo_strrb_is_bufindex_valid(rb1, 0));
	assert(!_osmo_strrb_is_bufindex_valid(rb1, 1));
	assert(!_osmo_strrb_is_bufindex_valid(rb1, 2));

	assert(!_osmo_strrb_is_bufindex_valid(rb3, 0));
	assert(_osmo_strrb_is_bufindex_valid(rb3, 1));
	assert(_osmo_strrb_is_bufindex_valid(rb3, 2));

	assert(_osmo_strrb_is_bufindex_valid(rb4, 0));
	assert(!_osmo_strrb_is_bufindex_valid(rb4, 1));
	assert(_osmo_strrb_is_bufindex_valid(rb4, 2));

	assert(_osmo_strrb_is_bufindex_valid(rb5, 0));
	assert(_osmo_strrb_is_bufindex_valid(rb5, 1));
	assert(!_osmo_strrb_is_bufindex_valid(rb5, 2));
}

void test_elems(void)
{
	assert(osmo_strrb_elements(rb0) == 0);
	assert(osmo_strrb_elements(rb1) == 1);
	assert(osmo_strrb_elements(rb2) == 2);
	assert(osmo_strrb_elements(rb3) == 2);
}

void test_getn(void)
{
	assert(!osmo_strrb_get_nth(rb0, 0));
	assert(!strcmp(STR0, osmo_strrb_get_nth(rb2, 0)));
	assert(!strcmp(STR1, osmo_strrb_get_nth(rb2, 1)));
	assert(!strcmp(STR1, osmo_strrb_get_nth(rb3, 0)));
	assert(!strcmp(STR2, osmo_strrb_get_nth(rb3, 1)));
	assert(!osmo_strrb_get_nth(rb3, 2));
}

void test_getn_wrap(void)
{
	assert(!strcmp(STR2, osmo_strrb_get_nth(rb4, 0)));
	assert(!strcmp(STR3, osmo_strrb_get_nth(rb4, 1)));

	assert(!strcmp(STR3, osmo_strrb_get_nth(rb5, 0)));
	assert(!strcmp(STR4, osmo_strrb_get_nth(rb5, 1)));
}

void test_add(void)
{
	struct osmo_strrb *rb = osmo_strrb_create(NULL, 3);
	assert(rb->start == 0);
	assert(rb->end == 0);

	osmo_strrb_add(rb, "a");
	osmo_strrb_add(rb, "b");
	osmo_strrb_add(rb, "c");
	assert(rb->start == 0);
	assert(rb->end == 3);
	assert(osmo_strrb_elements(rb) == 3);

	osmo_strrb_add(rb, "d");
	assert(rb->start == 1);
	assert(rb->end == 0);
	assert(osmo_strrb_elements(rb) == 3);
	assert(!strcmp("b", osmo_strrb_get_nth(rb, 0)));
	assert(!strcmp("c", osmo_strrb_get_nth(rb, 1)));
	assert(!strcmp("d", osmo_strrb_get_nth(rb, 2)));

	osmo_strrb_add(rb, "e");
	assert(rb->start == 2);
	assert(rb->end == 1);
	assert(!strcmp("c", osmo_strrb_get_nth(rb, 0)));
	assert(!strcmp("d", osmo_strrb_get_nth(rb, 1)));
	assert(!strcmp("e", osmo_strrb_get_nth(rb, 2)));

	osmo_strrb_add(rb, "f");
	assert(rb->start == 3);
	assert(rb->end == 2);
	assert(!strcmp("d", osmo_strrb_get_nth(rb, 0)));
	assert(!strcmp("e", osmo_strrb_get_nth(rb, 1)));
	assert(!strcmp("f", osmo_strrb_get_nth(rb, 2)));

	osmo_strrb_add(rb, "g");
	assert(rb->start == 0);
	assert(rb->end == 3);
	assert(!strcmp("e", osmo_strrb_get_nth(rb, 0)));
	assert(!strcmp("f", osmo_strrb_get_nth(rb, 1)));
	assert(!strcmp("g", osmo_strrb_get_nth(rb, 2)));

	osmo_strrb_add(rb, "h");
	assert(rb->start == 1);
	assert(rb->end == 0);
	assert(!strcmp("f", osmo_strrb_get_nth(rb, 0)));
	assert(!strcmp("g", osmo_strrb_get_nth(rb, 1)));
	assert(!strcmp("h", osmo_strrb_get_nth(rb, 2)));

	talloc_free(rb);
}

void test_long_msg(void)
{
	struct osmo_strrb *rb = osmo_strrb_create(NULL, 2);
	int test_size = RB_MAX_MESSAGE_SIZE + 7;
	char *tests1, *tests2;
	const char *rb_content;
	int i;

	tests1 = malloc(test_size);
	tests2 = malloc(test_size);
	/* Be certain allocating memory worked before continuing */
	assert(tests1);
	assert(tests2);

	for (i = 0; i < RB_MAX_MESSAGE_SIZE; i += 2) {
		tests1[i] = 'a';
		tests1[i + 1] = 'b';
	}
	tests1[i] = '\0';

	osmo_strrb_add(rb, tests1);
	strcpy(tests2, tests1);

	/* Verify that no stale data from test1 is lingering... */
	bzero(tests1, test_size);
	free(tests1);

	rb_content = osmo_strrb_get_nth(rb, 0);
	assert(!strncmp(tests2, rb_content, RB_MAX_MESSAGE_SIZE - 1));
	assert(!rb_content[RB_MAX_MESSAGE_SIZE - 1]);
	assert(strlen(rb_content) == RB_MAX_MESSAGE_SIZE - 1);

	free(tests2);
	talloc_free(rb);
}

int main(int argc, char **argv)
{
	init_rbs();
	test_offset_valid();
	test_elems();
	test_getn();
	test_getn_wrap();
	test_add();
	test_long_msg();
	printf("All tests passed\n");

	free_rbs();
	return 0;
}
