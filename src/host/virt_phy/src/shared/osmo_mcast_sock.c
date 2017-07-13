#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <osmocom/core/socket.h>
#include <osmocom/core/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <talloc.h>
#include <unistd.h>
#include <virtphy/osmo_mcast_sock.h>

/* convenience wrapper */
static void fd_close(struct osmo_fd *ofd)
{
	/* multicast memberships of socket are implicitly dropped when
	 * socket is closed */
	osmo_fd_unregister(ofd);
	close(ofd->fd);
	ofd->fd = -1;
	ofd->when = 0;
}

/* server socket is what we use for transmission. It is not subscribed
 * to a multicast group or locally bound, but it is just a normal UDP
 * socket that's connected to the remote mcast group + port */
int mcast_server_sock_setup(struct osmo_fd *ofd, const char* tx_mcast_group,
			    uint16_t tx_mcast_port, bool loopback)
{
	int rc;

	/* setup mcast server socket */
	rc = osmo_sock_init_ofd(ofd, AF_INET, SOCK_DGRAM, IPPROTO_UDP,
				tx_mcast_group, tx_mcast_port, OSMO_SOCK_F_CONNECT);
	if (rc < 0) {
		perror("Failed to create Multicast Server Socket");
		return rc;
	}

	/* determines whether sent mcast packets should be looped back to the local sockets.
	 * loopback must be enabled if the mcast client is on the same machine */
	rc = setsockopt(ofd->fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback));
	if (rc < 0) {
		perror("Failed to configure multicast loopback.\n");
		fd_close(ofd);
		return rc;
	}

	return 0;
}

/* the client socket is what we use for reception.  It is a UDP socket
 * that's bound to the GSMTAP UDP port and subscribed to the respective
 * multicast group */
int mcast_client_sock_setup(struct osmo_fd *ofd, const char *mcast_group, uint16_t mcast_port,
			    int (*fd_rx_cb)(struct osmo_fd *ofd, unsigned int what),
			    void *osmo_fd_data)
{
	struct ip_mreq mreq;
	int rc, loopback = 1, all = 0;

	ofd->cb = fd_rx_cb;
	ofd->when = BSC_FD_READ;
	ofd->data = osmo_fd_data;

	/* Create mcast client socket */
	rc = osmo_sock_init_ofd(ofd, AF_INET, SOCK_DGRAM, IPPROTO_UDP,
				NULL, mcast_port, OSMO_SOCK_F_BIND);
	if (rc < 0) {
		perror("Could not create mcast client socket");
		return rc;
	}

	/* Enable loopback of msgs to the host. */
	/* Loopback must be enabled for the client, so multiple
	 * processes are able to receive a mcast package. */
	rc = setsockopt(ofd->fd, IPPROTO_IP, IP_MULTICAST_LOOP,
			&loopback, sizeof(loopback));
	if (rc < 0) {
		perror("Failed to enable IP_MULTICAST_LOOP");
		fd_close(ofd);
		return rc;
	}

	/* Configure and join the multicast group */
	memset(&mreq, 0, sizeof(mreq));
	mreq.imr_multiaddr.s_addr = inet_addr(mcast_group);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	rc = setsockopt(ofd->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
	if (rc < 0) {
		perror("Failed to join to mcast goup");
		fd_close(ofd);
		return rc;
	}

	/* this option will set the delivery option so that only packets
	 * from sockets we are subscribed to via IP_ADD_MEMBERSHIP are received */
	if (setsockopt(ofd->fd, IPPROTO_IP, IP_MULTICAST_ALL, &all, sizeof(all)) < 0) {
		perror("Failed to modify delivery policy to explicitly joined.\n");
		fd_close(ofd);
		return rc;
	}

	return 0;
}

struct mcast_bidir_sock *
mcast_bidir_sock_setup(void *ctx, const char *tx_mcast_group, uint16_t tx_mcast_port,
			const char *rx_mcast_group, uint16_t rx_mcast_port, bool loopback,
			int (*fd_rx_cb)(struct osmo_fd *ofd, unsigned int what),
			void *osmo_fd_data)
{
	struct mcast_bidir_sock *bidir_sock = talloc(ctx, struct mcast_bidir_sock);
	int rc;

	if (!bidir_sock)
		return NULL;

	rc = mcast_client_sock_setup(&bidir_sock->rx_ofd, rx_mcast_group, rx_mcast_port,
				fd_rx_cb, osmo_fd_data);
	if (rc < 0) {
		talloc_free(bidir_sock);
		return NULL;
	}
	rc = mcast_server_sock_setup(&bidir_sock->tx_ofd, tx_mcast_group, tx_mcast_port, loopback);
	if (rc < 0) {
		fd_close(&bidir_sock->rx_ofd);
		talloc_free(bidir_sock);
		return NULL;
	}
	return bidir_sock;

}

int mcast_bidir_sock_tx(struct mcast_bidir_sock *bidir_sock, const uint8_t *data,
                        unsigned int data_len)
{
	return send(bidir_sock->tx_ofd.fd, data, data_len, 0);
}

int mcast_bidir_sock_rx(struct mcast_bidir_sock *bidir_sock, uint8_t *buf, unsigned int buf_len)
{
	return recv(bidir_sock->rx_ofd.fd, buf, buf_len, 0);
}

void mcast_bidir_sock_close(struct mcast_bidir_sock *bidir_sock)
{
	fd_close(&bidir_sock->tx_ofd);
	fd_close(&bidir_sock->rx_ofd);
	talloc_free(bidir_sock);
}
