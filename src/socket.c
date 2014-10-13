/*
 * (C) 2011 by Harald Welte <laforge@gnumonks.org>
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

#include "../config.h"

/*! \addtogroup socket
 *  @{
 */

/*! \file socket.c
 *  \brief Osmocom socket convenience functions
 */

#ifdef HAVE_SYS_SOCKET_H

#include <osmocom/core/logging.h>
#include <osmocom/core/select.h>
#include <osmocom/core/socket.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <netinet/in.h>

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <ifaddrs.h>

/*! \brief Initialize a socket (including bind/connect)
 *  \param[in] family Address Family like AF_INET, AF_INET6, AF_UNSPEC
 *  \param[in] type Socket type like SOCK_DGRAM, SOCK_STREAM
 *  \param[in] proto Protocol like IPPROTO_TCP, IPPROTO_UDP
 *  \param[in] host remote host name or IP address in string form
 *  \param[in] port remote port number in host byte order
 *  \param[in] flags flags like \ref OSMO_SOCK_F_CONNECT
 *
 * This function creates a new socket of the designated \a family, \a
 * type and \a proto and optionally binds or connects it, depending on
 * the value of \a flags parameter.
 */
int osmo_sock_init(uint16_t family, uint16_t type, uint8_t proto,
		   const char *host, uint16_t port, unsigned int flags)
{
	struct addrinfo hints, *result, *rp;
	int sfd, rc, on = 1;
	char portbuf[16];

	if ((flags & (OSMO_SOCK_F_BIND | OSMO_SOCK_F_CONNECT)) ==
		     (OSMO_SOCK_F_BIND | OSMO_SOCK_F_CONNECT))
		return -EINVAL;

	sprintf(portbuf, "%u", port);
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = family;
	if (type == SOCK_RAW) {
		/* Workaround for glibc, that returns EAI_SERVICE (-8) if
		 * SOCK_RAW and IPPROTO_GRE is used.
		 */
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
	} else {
		hints.ai_socktype = type;
		hints.ai_protocol = proto;
	}

	if (flags & OSMO_SOCK_F_BIND)
		hints.ai_flags |= AI_PASSIVE;

	rc = getaddrinfo(host, portbuf, &hints, &result);
	if (rc != 0) {
		perror("getaddrinfo returned NULL");
		return -EINVAL;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		/* Workaround for glibc again */
		if (type == SOCK_RAW) {
			rp->ai_socktype = SOCK_RAW;
			rp->ai_protocol = proto;
		}

		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1)
			continue;
		if (flags & OSMO_SOCK_F_NONBLOCK) {
			if (ioctl(sfd, FIONBIO, (unsigned char *)&on) < 0) {
				perror("cannot set this socket unblocking");
				close(sfd);
				return -EINVAL;
			}
		}
		if (flags & OSMO_SOCK_F_CONNECT) {
			rc = connect(sfd, rp->ai_addr, rp->ai_addrlen);
			if (rc != -1 || (rc == -1 && errno == EINPROGRESS))
				break;
		} else {
			rc = setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR,
							&on, sizeof(on));
			if (rc < 0) {
				perror("cannot setsockopt socket");
				break;
			}
			if (bind(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
				break;
		}
		close(sfd);
	}
	freeaddrinfo(result);

	if (rp == NULL) {
		perror("unable to connect/bind socket");
		return -ENODEV;
	}

	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	/* Make sure to call 'listen' on a bound, connection-oriented sock */
	if (flags & OSMO_SOCK_F_BIND) {
		switch (type) {
		case SOCK_STREAM:
		case SOCK_SEQPACKET:
			listen(sfd, 10);
			break;
		}
	}
	return sfd;
}

/*! \brief fill \ref osmo_fd for a give sfd
 *  \param[out] ofd file descriptor (will be filled in)
 *  \param[in] sfd socket file descriptor
 *
 * This function fills the \a ofd structure.
 */
static inline int osmo_fd_init_ofd(struct osmo_fd *ofd, int sfd)
{
	int rc;

	if (sfd < 0)
		return sfd;

	ofd->fd = sfd;
	ofd->when = BSC_FD_READ;

	rc = osmo_fd_register(ofd);
	if (rc < 0) {
		close(sfd);
		return rc;
	}

	return sfd;
}

/*! \brief Initialize a socket and fill \ref osmo_fd
 *  \param[out] ofd file descriptor (will be filled in)
 *  \param[in] family Address Family like AF_INET, AF_INET6, AF_UNSPEC
 *  \param[in] type Socket type like SOCK_DGRAM, SOCK_STREAM
 *  \param[in] proto Protocol like IPPROTO_TCP, IPPROTO_UDP
 *  \param[in] host remote host name or IP address in string form
 *  \param[in] port remote port number in host byte order
 *  \param[in] flags flags like \ref OSMO_SOCK_F_CONNECT
 *
 * This function creates (and optionall binds/connects) a socket using
 * \ref osmo_sock_init, but also fills the \a ofd structure.
 */
int osmo_sock_init_ofd(struct osmo_fd *ofd, int family, int type, int proto,
			const char *host, uint16_t port, unsigned int flags)
{
	return osmo_fd_init_ofd(ofd, osmo_sock_init(family, type, proto, host, port, flags));
}

/*! \brief Initialize a socket and fill \ref sockaddr
 *  \param[out] ss socket address (will be filled in)
 *  \param[in] type Socket type like SOCK_DGRAM, SOCK_STREAM
 *  \param[in] proto Protocol like IPPROTO_TCP, IPPROTO_UDP
 *  \param[in] flags flags like \ref OSMO_SOCK_F_CONNECT
 *
 * This function creates (and optionall binds/connects) a socket using
 * \ref osmo_sock_init, but also fills the \a ss structure.
 */
int osmo_sock_init_sa(struct sockaddr *ss, uint16_t type,
		      uint8_t proto, unsigned int flags)
{
	char host[NI_MAXHOST];
	uint16_t port;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	int s, sa_len;

	/* determine port and host from ss */
	switch (ss->sa_family) {
	case AF_INET:
		sin = (struct sockaddr_in *) ss;
		sa_len = sizeof(struct sockaddr_in);
		port = ntohs(sin->sin_port);
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *) ss;
		sa_len = sizeof(struct sockaddr_in6);
		port = ntohs(sin6->sin6_port);
		break;
	default:
		return -EINVAL;
	}

	s = getnameinfo(ss, sa_len, host, NI_MAXHOST,
			NULL, 0, NI_NUMERICHOST);
	if (s != 0) {
		perror("getnameinfo failed");
		return s;
	}

	return osmo_sock_init(ss->sa_family, type, proto, host, port, flags);
}

static int sockaddr_equal(const struct sockaddr *a,
			  const struct sockaddr *b, unsigned int len)
{
	struct sockaddr_in *sin_a, *sin_b;
	struct sockaddr_in6 *sin6_a, *sin6_b;

	if (a->sa_family != b->sa_family)
		return 0;

	switch (a->sa_family) {
	case AF_INET:
		sin_a = (struct sockaddr_in *)a;
		sin_b = (struct sockaddr_in *)b;
		if (!memcmp(&sin_a->sin_addr, &sin_b->sin_addr,
			    sizeof(struct in_addr)))
			return 1;
		break;
	case AF_INET6:
		sin6_a = (struct sockaddr_in6 *)a;
		sin6_b = (struct sockaddr_in6 *)b;
		if (!memcmp(&sin6_a->sin6_addr, &sin6_b->sin6_addr,
			    sizeof(struct in6_addr)))
			return 1;
		break;
	}
	return 0;
}

/*! \brief Determine if the given address is a local address
 *  \param[in] addr Socket Address
 *  \param[in] addrlen Length of socket address in bytes
 *  \returns 1 if address is local, 0 otherwise.
 */
int osmo_sockaddr_is_local(struct sockaddr *addr, unsigned int addrlen)
{
	struct ifaddrs *ifaddr, *ifa;

	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		return -EIO;
	}

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr)
			continue;
		if (sockaddr_equal(ifa->ifa_addr, addr, addrlen))
			return 1;
	}

	return 0;
}

/*! \brief Initialize a unix domain socket (including bind/connect)
 *  \param[in] type Socket type like SOCK_DGRAM, SOCK_STREAM
 *  \param[in] proto Protocol like IPPROTO_TCP, IPPROTO_UDP
 *  \param[in] socket_path path to identify the socket
 *  \param[in] flags flags like \ref OSMO_SOCK_F_CONNECT
 *
 * This function creates a new unix domain socket, \a
 * type and \a proto and optionally binds or connects it, depending on
 * the value of \a flags parameter.
 */
int osmo_sock_unix_init(uint16_t type, uint8_t proto,
			const char *socket_path, unsigned int flags)
{
	struct sockaddr_un local;
	int sfd, rc, on = 1;
	unsigned int namelen;

	if ((flags & (OSMO_SOCK_F_BIND | OSMO_SOCK_F_CONNECT)) ==
		     (OSMO_SOCK_F_BIND | OSMO_SOCK_F_CONNECT))
		return -EINVAL;

	local.sun_family = AF_UNIX;
	strncpy(local.sun_path, socket_path, sizeof(local.sun_path));
	local.sun_path[sizeof(local.sun_path) - 1] = '\0';

#if defined(BSD44SOCKETS) || defined(__UNIXWARE__)
	local.sun_len = strlen(local.sun_path);
#endif
#if defined(BSD44SOCKETS) || defined(SUN_LEN)
	namelen = SUN_LEN(&local);
#else
	namelen = strlen(local.sun_path) +
		  offsetof(struct sockaddr_un, sun_path);
#endif

	sfd = socket(AF_UNIX, type, proto);
	if (sfd < 0)
		return -1;

	if (flags & OSMO_SOCK_F_CONNECT) {
		rc = connect(sfd, (struct sockaddr *)&local, namelen);
		if (rc < 0)
			goto err;
	} else {
		unlink(local.sun_path);
		rc = bind(sfd, (struct sockaddr *)&local, namelen);
		if  (rc < 0)
			goto err;
	}

	if (flags & OSMO_SOCK_F_NONBLOCK) {
		if (ioctl(sfd, FIONBIO, (unsigned char *)&on) < 0) {
			perror("cannot set this socket unblocking");
			close(sfd);
			return -EINVAL;
		}
	}

	if (flags & OSMO_SOCK_F_BIND) {
		rc = listen(sfd, 10);
		if (rc < 0)
			goto err;
	}

	return sfd;
err:
	close(sfd);
	return -1;
}

/*! \brief Initialize a unix domain socket and fill \ref osmo_fd
 *  \param[out] ofd file descriptor (will be filled in)
 *  \param[in] type Socket type like SOCK_DGRAM, SOCK_STREAM
 *  \param[in] proto Protocol like IPPROTO_TCP, IPPROTO_UDP
 *  \param[in] socket_path path to identify the socket
 *  \param[in] flags flags like \ref OSMO_SOCK_F_CONNECT
 *
 * This function creates (and optionall binds/connects) a socket using
 * \ref osmo_sock_unix_init, but also fills the \a ofd structure.
 */
int osmo_sock_unix_init_ofd(struct osmo_fd *ofd, uint16_t type, uint8_t proto,
			    const char *socket_path, unsigned int flags)
{
	return osmo_fd_init_ofd(ofd, osmo_sock_unix_init(type, proto, socket_path, flags));
}

#endif /* HAVE_SYS_SOCKET_H */

/*! @} */
