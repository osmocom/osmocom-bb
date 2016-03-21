/* select filedescriptor handling, taken from:
 * userspace logging daemon for the iptables ULOG target
 * of the linux 2.4 netfilter subsystem.
 *
 * (C) 2000-2009 by Harald Welte <laforge@gnumonks.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301, USA.
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>

#include <osmocom/core/select.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/timer.h>

#include "../config.h"

#ifdef HAVE_SYS_SELECT_H

/*! \addtogroup select
 *  @{
 */

/*! \file select.c
 *  \brief select loop abstraction
 */

static int maxfd = 0;
static LLIST_HEAD(osmo_fds);
static int unregistered_count;

/*! \brief Register a new file descriptor with select loop abstraction
 *  \param[in] fd osmocom file descriptor to be registered
 */
int osmo_fd_register(struct osmo_fd *fd)
{
	int flags;

	/* make FD nonblocking */
	flags = fcntl(fd->fd, F_GETFL);
	if (flags < 0)
		return flags;
	flags |= O_NONBLOCK;
	flags = fcntl(fd->fd, F_SETFL, flags);
	if (flags < 0)
		return flags;

	/* set close-on-exec flag */
	flags = fcntl(fd->fd, F_GETFD);
	if (flags < 0)
		return flags;
	flags |= FD_CLOEXEC;
	flags = fcntl(fd->fd, F_SETFD, flags);
	if (flags < 0)
		return flags;

	/* Register FD */
	if (fd->fd > maxfd)
		maxfd = fd->fd;

#ifdef BSC_FD_CHECK
	struct osmo_fd *entry;
	llist_for_each_entry(entry, &osmo_fds, list) {
		if (entry == fd) {
			fprintf(stderr, "Adding a osmo_fd that is already in the list.\n");
			return 0;
		}
	}
#endif

	llist_add_tail(&fd->list, &osmo_fds);

	return 0;
}

/*! \brief Unregister a file descriptor from select loop abstraction
 *  \param[in] fd osmocom file descriptor to be unregistered
 */
void osmo_fd_unregister(struct osmo_fd *fd)
{
	unregistered_count++;
	llist_del(&fd->list);
}

inline int osmo_fd_fill_fds(void *_rset, void *_wset, void *_eset)
{
	fd_set *readset = _rset, *writeset = _wset, *exceptset = _eset;
	struct osmo_fd *ufd;

	llist_for_each_entry(ufd, &osmo_fds, list) {
		if (ufd->when & BSC_FD_READ)
			FD_SET(ufd->fd, readset);

		if (ufd->when & BSC_FD_WRITE)
			FD_SET(ufd->fd, writeset);

		if (ufd->when & BSC_FD_EXCEPT)
			FD_SET(ufd->fd, exceptset);
	}

	return maxfd;
}

inline int osmo_fd_disp_fds(void *_rset, void *_wset, void *_eset)
{
	struct osmo_fd *ufd, *tmp;
	int work = 0;
	fd_set *readset = _rset, *writeset = _wset, *exceptset = _eset;

restart:
	unregistered_count = 0;
	llist_for_each_entry_safe(ufd, tmp, &osmo_fds, list) {
		int flags = 0;

		if (FD_ISSET(ufd->fd, readset)) {
			flags |= BSC_FD_READ;
			FD_CLR(ufd->fd, readset);
		}

		if (FD_ISSET(ufd->fd, writeset)) {
			flags |= BSC_FD_WRITE;
			FD_CLR(ufd->fd, writeset);
		}

		if (FD_ISSET(ufd->fd, exceptset)) {
			flags |= BSC_FD_EXCEPT;
			FD_CLR(ufd->fd, exceptset);
		}

		if (flags) {
			work = 1;
			ufd->cb(ufd, flags);
		}
		/* ugly, ugly hack. If more than one filedescriptor was
		 * unregistered, they might have been consecutive and
		 * llist_for_each_entry_safe() is no longer safe */
		/* this seems to happen with the last element of the list as well */
		if (unregistered_count >= 1)
			goto restart;
	}

	return work;
}

/*! \brief select main loop integration
 *  \param[in] polling should we pollonly (1) or block on select (0)
 */
int osmo_select_main(int polling)
{
	fd_set readset, writeset, exceptset;
	int rc;
	struct timeval no_time = {0, 0};

	FD_ZERO(&readset);
	FD_ZERO(&writeset);
	FD_ZERO(&exceptset);

	/* prepare read and write fdsets */
	osmo_fd_fill_fds(&readset, &writeset, &exceptset);

	osmo_timers_check();

	if (!polling)
		osmo_timers_prepare();
	rc = select(maxfd+1, &readset, &writeset, &exceptset, polling ? &no_time : osmo_timers_nearest());
	if (rc < 0)
		return 0;

	/* fire timers */
	osmo_timers_update();

	/* call registered callback functions */
	return osmo_fd_disp_fds(&readset, &writeset, &exceptset);
}

/*! \brief find an osmo_fd based on the integer fd */
struct osmo_fd *osmo_fd_get_by_fd(int fd)
{
	struct osmo_fd *ofd;

	llist_for_each_entry(ofd, &osmo_fds, list) {
		if (ofd->fd == fd)
			return ofd;
	}
	return NULL;
}

/*! @} */

#endif /* _HAVE_SYS_SELECT_H */
