#ifndef _BSC_SELECT_H
#define _BSC_SELECT_H

#include <osmocom/core/linuxlist.h>

/*! \defgroup select Select loop abstraction
 *  @{
 */

/*! \file select.h
 *  \brief select loop abstraction
 */

/*! \brief Indicate interest in reading from the file descriptor */
#define BSC_FD_READ	0x0001
/*! \brief Indicate interest in writing to the file descriptor */
#define BSC_FD_WRITE	0x0002
/*! \brief Indicate interest in exceptions from the file descriptor */
#define BSC_FD_EXCEPT	0x0004

/*! \brief Structure representing a file dsecriptor */
struct osmo_fd {
	/*! linked list for internal management */
	struct llist_head list;	
	/*! actual operating-system level file decriptor */
	int fd;
	/*! bit-mask or of \ref BSC_FD_READ, \ref BSC_FD_WRITE and/or
	 * \ref BSC_FD_EXCEPT */
	unsigned int when;
	/*! call-back function to be called once file descriptor becomes
	 * available */
	int (*cb)(struct osmo_fd *fd, unsigned int what);
	/*! data pointer passed through to call-back function */
	void *data;
	/*! private number, extending \a data */
	unsigned int priv_nr;
};

int osmo_fd_register(struct osmo_fd *fd);
void osmo_fd_unregister(struct osmo_fd *fd);
int osmo_select_main(int polling);

/*! @} */

#endif /* _BSC_SELECT_H */
