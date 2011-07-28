/* Utility functions to setup applications */
/*
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

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
	log_init(log_info, NULL);
	osmo_stderr_target = log_target_create_stderr();
	if (!osmo_stderr_target)
		return -1;

	log_add_target(osmo_stderr_target);
	log_set_all_filter(osmo_stderr_target, 1);
	return 0;
}

int osmo_daemonize(void)
{
	int rc;
	pid_t pid, sid;

	/* Check if parent PID == init, in which case we are already a daemon */
	if (getppid() == 1)
		return -EEXIST;

	/* Fork from the parent process */
	pid = fork();
	if (pid < 0) {
		/* some error happened */
		return pid;
	}

	if (pid > 0) {
		/* if we have received a positive PID, then we are the parent
		 * and can exit */
		exit(0);
	}

	/* FIXME: do we really want this? */
	umask(0);

	/* Create a new session and set process group ID */
	sid = setsid();
	if (sid < 0)
		return sid;

	/* Change to the /tmp directory, which prevents the CWD from being locked
	 * and unable to remove it */
	rc = chdir("/tmp");
	if (rc < 0)
		return rc;

	/* Redirect stdio to /dev/null */
/* since C89/C99 says stderr is a macro, we can safely do this! */
#ifdef stderr
	freopen("/dev/null", "r", stdin);
	freopen("/dev/null", "w", stdout);
	freopen("/dev/null", "w", stderr);
#endif

	return 0;
}
