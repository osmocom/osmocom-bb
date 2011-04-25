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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <fcntl.h>
#include <stdio.h>

#include <osmocom/core/select.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/timer.h>

#include "../config.h"

#ifdef HAVE_SYS_SELECT_H

static int maxfd = 0;
static LLIST_HEAD(bsc_fds);
static int unregistered_count;

int bsc_register_fd(struct bsc_fd *fd)
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

	/* Register FD */
	if (fd->fd > maxfd)
		maxfd = fd->fd;

#ifdef BSC_FD_CHECK
	struct bsc_fd *entry;
	llist_for_each_entry(entry, &bsc_fds, list) {
		if (entry == fd) {
			fprintf(stderr, "Adding a bsc_fd that is already in the list.\n");
			return 0;
		}
	}
#endif

	llist_add_tail(&fd->list, &bsc_fds);

	return 0;
}

void bsc_unregister_fd(struct bsc_fd *fd)
{
	unregistered_count++;
	llist_del(&fd->list);
}

int bsc_select_main(int polling)
{
	struct bsc_fd *ufd, *tmp;
	fd_set readset, writeset, exceptset;
	int work = 0, rc;
	struct timeval no_time = {0, 0};

	FD_ZERO(&readset);
	FD_ZERO(&writeset);
	FD_ZERO(&exceptset);

	/* prepare read and write fdsets */
	llist_for_each_entry(ufd, &bsc_fds, list) {
		if (ufd->when & BSC_FD_READ)
			FD_SET(ufd->fd, &readset);

		if (ufd->when & BSC_FD_WRITE)
			FD_SET(ufd->fd, &writeset);

		if (ufd->when & BSC_FD_EXCEPT)
			FD_SET(ufd->fd, &exceptset);
	}

	bsc_timer_check();

	if (!polling)
		bsc_prepare_timers();
	rc = select(maxfd+1, &readset, &writeset, &exceptset, polling ? &no_time : bsc_nearest_timer());
	if (rc < 0)
		return 0;

	/* fire timers */
	bsc_update_timers();

	/* call registered callback functions */
restart:
	unregistered_count = 0;
	llist_for_each_entry_safe(ufd, tmp, &bsc_fds, list) {
		int flags = 0;

		if (FD_ISSET(ufd->fd, &readset)) {
			flags |= BSC_FD_READ;
			FD_CLR(ufd->fd, &readset);
		}

		if (FD_ISSET(ufd->fd, &writeset)) {
			flags |= BSC_FD_WRITE;
			FD_CLR(ufd->fd, &writeset);
		}

		if (FD_ISSET(ufd->fd, &exceptset)) {
			flags |= BSC_FD_EXCEPT;
			FD_CLR(ufd->fd, &exceptset);
		}

		if (flags) {
			work = 1;
			ufd->cb(ufd, flags);
		}
		/* ugly, ugly hack. If more than one filedescriptors were
		 * unregistered, they might have been consecutive and
		 * llist_for_each_entry_safe() is no longer safe */
		/* this seems to happen with the last element of the list as well */
		if (unregistered_count >= 1)
			goto restart;
	}
	return work;
}

#endif /* _HAVE_SYS_SELECT_H */
