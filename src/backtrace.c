/*
 * (C) 2009 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2012 by Harald Welte <laforge@gnumonks.org>
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

/*! \file backtrace.c
 *  \brief Routines realted to generating call back traces
 */

#include <stdio.h>
#include <stdlib.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/logging.h>
#include "config.h"

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>

static void _osmo_backtrace(int use_printf, int subsys, int level)
{
	int i, nptrs;
	void *buffer[100];
	char **strings;

	nptrs = backtrace(buffer, ARRAY_SIZE(buffer));
	if (use_printf)
		printf("backtrace() returned %d addresses\n", nptrs);
	else
		LOGP(subsys, level, "backtrace() returned %d addresses\n",
		     nptrs);

	strings = backtrace_symbols(buffer, nptrs);
	if (!strings)
		return;

	for (i = 1; i < nptrs; i++) {
		if (use_printf)
			printf("%s\n", strings[i]);
		else
			LOGP(subsys, level, "\t%s\n", strings[i]);
	}

	free(strings);
}

/*! \brief Generate and print a call back-trace
 *
 * This function will generate a function call back-trace of the
 * current process and print it to stdout. */
void osmo_generate_backtrace(void)
{
	_osmo_backtrace(1, 0, 0);
}

/*! \brief Generate and log a call back-trace
 *
 * This function will generate a function call back-trace of the
 * current process and log it to the specified subsystem and
 * level using the libosmocore logging subsystem */
void osmo_log_backtrace(int subsys, int level)
{
	_osmo_backtrace(0, subsys, level);
}
#else
void osmo_generate_backtrace(void)
{
	printf("This platform has no backtrace function\n");
}
void osmo_log_backtrace(int subsys, int level)
{
	LOGP(subsys, level, "This platform has no backtrace function\n");
}
#endif
