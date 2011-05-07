#ifndef _BSC_SELECT_H
#define _BSC_SELECT_H

#include <osmocom/core/linuxlist.h>

#define BSC_FD_READ	0x0001
#define BSC_FD_WRITE	0x0002
#define BSC_FD_EXCEPT	0x0004

struct osmo_fd {
	struct llist_head list;
	int fd;
	unsigned int when;
	int (*cb)(struct osmo_fd *fd, unsigned int what);
	void *data;
	unsigned int priv_nr;
};

int osmo_fd_register(struct osmo_fd *fd);
void osmo_fd_unregister(struct osmo_fd *fd);
int osmo_select_main(int polling);
#endif /* _BSC_SELECT_H */
