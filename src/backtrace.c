/*
 * (C) 2008 by Daniel Willmann <daniel@totalueberwachung.de>
 * (C) 2009 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 by Nico Golde <nico@ngolde.de>
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

#include <stdio.h>
#include <stdlib.h>
#include <osmocore/utils.h>
#include "config.h"

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
void generate_backtrace()
{
	int i, nptrs;
	void *buffer[100];
	char **strings;

	nptrs = backtrace(buffer, ARRAY_SIZE(buffer));
	printf("backtrace() returned %d addresses\n", nptrs);

	strings = backtrace_symbols(buffer, nptrs);
	if (!strings)
		return;

	for (i = 1; i < nptrs; i++)
		printf("%s\n", strings[i]);

	free(strings);
}
#endif
