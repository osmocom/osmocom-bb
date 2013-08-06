/* (C) 2013 by Jacob Erlbeck <jerlbeck@sysmocom.de>
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
#include <string.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/utils.h>
#include <osmocom/vty/misc.h>

static void test_cmd_string_from_valstr(void)
{
	char *cmd;
	const struct value_string printf_seq_vs[] = {
		{ .value = 42, .str = "[foo%s%s%s%s%s]"},
		{ .value = 43, .str = "[bar%s%s%s%s%s]"},
		{ .value = 0,  .str = NULL}
	};

	printf("Going to test vty_cmd_string_from_valstr()\n");

	/* check against character strings that could break printf */

	cmd = vty_cmd_string_from_valstr (NULL, printf_seq_vs, "[prefix%s%s%s%s%s]", "[sep%s%s%s%s%s]", "[end%s%s%s%s%s]", 1);
	printf ("Tested with %%s-strings, resulting cmd = '%s'\n", cmd);
	talloc_free (cmd);
}

int main(int argc, char **argv)
{
	test_cmd_string_from_valstr();
	printf("All tests passed\n");

	return 0;
}
