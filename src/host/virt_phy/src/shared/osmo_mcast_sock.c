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

struct mcast_server_sock *mcast_server_sock_setup(void *ctx,
                                                  char* tx_mcast_group,
                                                  int tx_mcast_port,
                                                  int loopback)
{
	struct mcast_server_sock *serv_sock = talloc_zero(ctx,
	                struct mcast_server_sock);

	serv_sock->osmo_fd = talloc_zero(ctx, struct osmo_fd);
	serv_sock->sock_conf = talloc_zero(ctx, struct sockaddr_in);

	// setup mcast server socket
	serv_sock->osmo_fd->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (serv_sock->osmo_fd->fd == -1) {
		perror("Failed to create Multicast Server Socket");
		return NULL;
	}

	serv_sock->sock_conf->sin_family = AF_INET;
	serv_sock->sock_conf->sin_addr.s_addr = inet_addr(tx_mcast_group);
	serv_sock->sock_conf->sin_port = htons(tx_mcast_port);

	// determines whether sent mcast packets should be looped back to the local sockets.
	// loopback must be enabled if the mcast client is on the same machine
	if (setsockopt(serv_sock->osmo_fd->fd, IPPROTO_IP,
	IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) < 0) {
		perror("Failed to disable loopback.\n");
		return NULL;
	}

	return serv_sock;
}

struct mcast_client_sock *mcast_client_sock_setup(
                void *ctx, char* mcast_group, int mcast_port,
                int (*fd_rx_cb)(struct osmo_fd *ofd, unsigned int what),
                void *osmo_fd_data)
{
	struct mcast_client_sock *client_sock = talloc_zero(ctx,
	                struct mcast_client_sock);
	struct sockaddr_in *rx_sock_conf = talloc_zero(NULL,
	                struct sockaddr_in);
	int rc, reuseaddr = 1, loopback = 1, all = 0;

	client_sock->osmo_fd = talloc_zero(ctx, struct osmo_fd);
	client_sock->mcast_group = talloc_zero(ctx, struct ip_mreq);

	// Create mcast client socket
	client_sock->osmo_fd->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (client_sock->osmo_fd->fd == -1) {
		perror("Could not create mcast client socket");
		return NULL;
	}

	// Enable SO_REUSEADDR to allow multiple instances of this application to receive copies of the multicast datagrams.
	rc = setsockopt(client_sock->osmo_fd->fd,
	SOL_SOCKET,
	SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
	if (rc < 0) {
		perror("Failed to configure REUSEADDR option");
		return NULL;
	}

	// Bind to the proper port number with the IP address specified as INADDR_ANY.
	rx_sock_conf->sin_family = AF_INET;
	rx_sock_conf->sin_addr.s_addr = htonl(INADDR_ANY);
	rx_sock_conf->sin_port = htons(mcast_port);
	rc = bind(client_sock->osmo_fd->fd, (struct sockaddr *)rx_sock_conf,
	                sizeof(*rx_sock_conf));
	talloc_free(rx_sock_conf);
	if (rc < 0) {
		perror("Could not bind mcast client socket");
		return NULL;
	}

	// Enable loopback of msgs to the host.
	// Loopback must be enabled for the client, so multiple processes are able to recevie a mcast package.
	rc = setsockopt(client_sock->osmo_fd->fd,
	IPPROTO_IP,
	IP_MULTICAST_LOOP, &loopback, sizeof(loopback));
	if (rc < 0) {
		perror("Failed to enable IP_MULTICAST_LOOP");
		return NULL;
	}

	// Configure and join the multicast group
	client_sock->mcast_group->imr_multiaddr.s_addr = inet_addr(mcast_group);
	client_sock->mcast_group->imr_interface.s_addr = htonl(INADDR_ANY);
	rc = setsockopt(client_sock->osmo_fd->fd,
	IPPROTO_IP,
	IP_ADD_MEMBERSHIP, client_sock->mcast_group,
	                sizeof(*client_sock->mcast_group));
	if (rc < 0) {
		perror("Failed to join to mcast goup");
		return NULL;
	}

	// this option will set the delivery option so that only packages are received
	// from sockets we are bound to via IP_ADD_MEMBERSHIP
	if (setsockopt(client_sock->osmo_fd->fd, IPPROTO_IP,
	IP_MULTICAST_ALL, &all, sizeof(all)) < 0) {
		perror("Failed to modify delivery policy to explicitly joined.\n");
		return NULL;
	}

	// configure and register the osmocom filedescriptor
	client_sock->osmo_fd->cb = fd_rx_cb;
	client_sock->osmo_fd->when = BSC_FD_READ;
	client_sock->osmo_fd->data = osmo_fd_data;

	osmo_fd_register(client_sock->osmo_fd);

	return client_sock;
}

struct mcast_bidir_sock *mcast_bidir_sock_setup(
                void *ctx, char* tx_mcast_group, int tx_mcast_port,
                char* rx_mcast_group, int rx_mcast_port, int loopback,
                int (*fd_rx_cb)(struct osmo_fd *ofd, unsigned int what),
                void *osmo_fd_data)
{
	struct mcast_bidir_sock *bidir_sock = talloc(ctx,
	                struct mcast_bidir_sock);
	bidir_sock->rx_sock = mcast_client_sock_setup(ctx, rx_mcast_group,
	                rx_mcast_port, fd_rx_cb, osmo_fd_data);
	bidir_sock->tx_sock = mcast_server_sock_setup(ctx, tx_mcast_group,
	                tx_mcast_port, loopback);
	if (!bidir_sock->rx_sock || !bidir_sock->tx_sock) {
		return NULL;
	}
	return bidir_sock;

}

int mcast_client_sock_rx(struct mcast_client_sock *client_sock, void* buf,
                         int buf_len)
{
	return recv(client_sock->osmo_fd->fd, buf, buf_len, 0);
}

int mcast_server_sock_tx(struct mcast_server_sock *serv_sock, void* data,
                         int data_len)
{
	return sendto(serv_sock->osmo_fd->fd, data, data_len, 0,
	                (struct sockaddr *)serv_sock->sock_conf,
	                sizeof(*serv_sock->sock_conf));
}

int mcast_bidir_sock_tx(struct mcast_bidir_sock *bidir_sock, void* data,
                        int data_len)
{
	return mcast_server_sock_tx(bidir_sock->tx_sock, data, data_len);
}
int mcast_bidir_sock_rx(struct mcast_bidir_sock *bidir_sock, void* buf,
                        int buf_len)
{
	return mcast_client_sock_rx(bidir_sock->rx_sock, buf, buf_len);
}

void mcast_client_sock_close(struct mcast_client_sock *client_sock)
{
	setsockopt(client_sock->osmo_fd->fd,
	IPPROTO_IP,
	IP_DROP_MEMBERSHIP, client_sock->mcast_group,
	                sizeof(*client_sock->mcast_group));
	osmo_fd_unregister(client_sock->osmo_fd);
	client_sock->osmo_fd->fd = -1;
	client_sock->osmo_fd->when = 0;
	close(client_sock->osmo_fd->fd);
	talloc_free(client_sock->mcast_group);
	talloc_free(client_sock->osmo_fd);
	talloc_free(client_sock);

}
void mcast_server_sock_close(struct mcast_server_sock *serv_sock)
{
	close(serv_sock->osmo_fd->fd);
	talloc_free(serv_sock->sock_conf);
	talloc_free(serv_sock);
}

void mcast_bidir_sock_close(struct mcast_bidir_sock *bidir_sock)
{
	mcast_client_sock_close(bidir_sock->rx_sock);
	mcast_server_sock_close(bidir_sock->tx_sock);
	talloc_free(bidir_sock);
}
