#ifndef _OSMOCORE_SOCKET_H
#define _OSMOCORE_SOCKET_H

#include <stdint.h>

struct sockaddr;

int osmo_sock_init(uint16_t family, uint16_t type, uint8_t proto,
		   const char *host, uint16_t port, int connect0_bind1);

int osmo_sock_init_ofd(struct osmo_fd *ofd, int family, int type, int proto,
			const char *host, uint16_t port, int connect0_bind1);

int osmo_sock_init_sa(struct sockaddr *ss, uint16_t type,
		      uint8_t proto, int connect0_bind1);

/* determine if the given address is a local address */
int osmo_sockaddr_is_local(struct sockaddr *addr, unsigned int addrlen);

#endif /* _OSMOCORE_SOCKET_H */
