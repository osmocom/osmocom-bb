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

/*! \file application.c
 *  \brief Routines for helping with the osmocom application setup.
 */

/*! \mainpage libosmocore Documentation
 * \section sec_intro Introduction
 * This library is a collection of common code used in various
 * sub-projects inside the Osmocom family of projects.  It includes a
 * logging framework, select() loop abstraction, timers with callbacks,
 * bit vectors, bit packing/unpacking, convolutional decoding, GSMTAP, a
 * generic plugin interface, statistics counters, memory allocator,
 * socket abstraction, message buffers, etc.
 * \n\n
 * Please note that C language projects inside Osmocom are typically
 * single-threaded event-loop state machine designs.  As such,
 * routines in libosmocore are not thread-safe.  If you must use them in
 * a multi-threaded context, you have to add your own locking.
 *
 * \section sec_copyright Copyright and License
 * Copyright © 2008-2011 - Harald Welte, Holger Freyther and contributors\n
 * All rights reserved. \n\n
 * The source code of libosmocore is licensed under the terms of the GNU
 * General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later
 * version.\n
 * See <http://www.gnu.org/licenses/> or COPYING included in the source
 * code package istelf.\n
 * The information detailed here is provided AS IS with NO WARRANTY OF
 * ANY KIND, INCLUDING THE WARRANTY OF DESIGN, MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.
 * \n\n
 *
 * \section sec_contact Contact and Support
 * Community-based support is available at the OpenBSC mailing list
 * <http://lists.osmocom.org/mailman/listinfo/openbsc>\n
 * Commercial support options available upon request from
 * <http://sysmocom.de/>
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

static void sighup_hdlr(int signal)
{
	log_targets_reopen();
}

/*! \brief Ignore \ref SIGPIPE, \ref SIGALRM, \ref SIGHUP and \ref SIGIO */
void osmo_init_ignore_signals(void)
{
	/* Signals that by default would terminate */
#ifdef SIGPIPE
	signal(SIGPIPE, SIG_IGN);
#endif
	signal(SIGALRM, SIG_IGN);
#ifdef SIGHUP
	signal(SIGHUP, &sighup_hdlr);
#endif
#ifdef SIGIO
	signal(SIGIO, SIG_IGN);
#endif
}

/*! \brief Initialize the osmocom logging framework
 *  \param[in] log_info Array of available logging sub-systems
 *  \returns 0 on success, -1 in case of error
 *
 * This function initializes the osmocom logging systems.  It also
 * creates the default (stderr) logging target.
 */
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

/*! \brief Turn the current process into a background daemon
 *
 * This function will fork the process, exit the parent and set umask,
 * create a new session, close stdin/stdout/stderr and chdir to /tmp
 */
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
