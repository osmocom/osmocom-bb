#pragma once

/*! \defgroup socket Socket convenience functions
 *  @{
 */

/*! \file socket.h
 *  \brief Osmocom socket convenience functions
 */

#include <stdint.h>

struct sockaddr;
struct osmo_fd;

/* flags for osmo_sock_init. */
#define OSMO_SOCK_F_CONNECT	(1 << 0)
#define OSMO_SOCK_F_BIND	(1 << 1)
#define OSMO_SOCK_F_NONBLOCK	(1 << 2)

int osmo_sock_init(uint16_t family, uint16_t type, uint8_t proto,
		   const char *host, uint16_t port, unsigned int flags);

int osmo_sock_init_ofd(struct osmo_fd *ofd, int family, int type, int proto,
			const char *host, uint16_t port, unsigned int flags);

int osmo_sock_init_sa(struct sockaddr *ss, uint16_t type,
		      uint8_t proto, unsigned int flags);

int osmo_sockaddr_is_local(struct sockaddr *addr, unsigned int addrlen);

int osmo_sock_unix_init(uint16_t type, uint8_t proto,
			const char *socket_path, unsigned int flags);

int osmo_sock_unix_init_ofd(struct osmo_fd *ofd, uint16_t type, uint8_t proto,
			    const char *socket_path, unsigned int flags);

/*! @} */
