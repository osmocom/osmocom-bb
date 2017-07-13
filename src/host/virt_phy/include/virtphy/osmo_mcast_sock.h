#pragma once

#include <netinet/in.h>
#include <osmocom/core/select.h>

struct mcast_bidir_sock {
	struct osmo_fd tx_ofd;
	struct osmo_fd rx_ofd;
};

struct mcast_bidir_sock *mcast_bidir_sock_setup(void *ctx,
                const char *tx_mcast_group, int tx_mcast_port,
                const char *rx_mcast_group, int rx_mcast_port, int loopback,
                int (*fd_rx_cb)(struct osmo_fd *ofd, unsigned int what),
                void *osmo_fd_data);

int mcast_server_sock_setup(struct osmo_fd *ofd, const char *tx_mcast_group,
			    int tx_mcast_port, int loopback);

int mcast_client_sock_setup(struct osmo_fd *ofd, const char *mcast_group, int mcast_port,
			    int (*fd_rx_cb)(struct osmo_fd *ofd, unsigned int what),
			    void *osmo_fd_data);

int mcast_bidir_sock_tx(struct mcast_bidir_sock *bidir_sock, void *data, int data_len);
int mcast_bidir_sock_rx(struct mcast_bidir_sock *bidir_sock, void *buf, int buf_len);
void mcast_bidir_sock_close(struct mcast_bidir_sock* bidir_sock);

