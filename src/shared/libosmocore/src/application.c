/* Utility functions to setup applications */
/*
 * (C) 2011 by Holger Hans Peter Freyther
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

#include <osmocom/core/application.h>
#include <osmocom/core/logging.h>

#include <signal.h>

struct log_target *osmo_stderr_target;

void osmo_init_ignore_signals(void)
{
	/* Signals that by default would terminate */
	signal(SIGPIPE, SIG_IGN);
	signal(SIGALRM, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	signal(SIGIO, SIG_IGN);
}

int osmo_init_logging(const struct log_info *log_info)
{
	log_init(log_info);
	osmo_stderr_target = log_target_create_stderr();
	if (!osmo_stderr_target)
		return -1;

	log_add_target(osmo_stderr_target);
	log_set_all_filter(osmo_stderr_target, 1);
	return 0;
}
