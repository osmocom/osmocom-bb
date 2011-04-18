/* Process handling support code */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

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
