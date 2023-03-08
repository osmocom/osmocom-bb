#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <netinet/in.h>
#include <osmocom/core/select.h>

struct mcast_bidir_sock {
	struct osmo_fd tx_ofd;
	struct osmo_fd rx_ofd;
};

struct mcast_bidir_sock *mcast_bidir_sock_setup(void *ctx,
                const char *tx_mcast_group, uint16_t tx_mcast_port,
                const char *rx_mcast_group, uint16_t rx_mcast_port, bool loopback,
                int (*fd_rx_cb)(struct osmo_fd *ofd, unsigned int what),
                void *osmo_fd_data);

int mcast_server_sock_setup(struct osmo_fd *ofd, const char *tx_mcast_group,
			    uint16_t tx_mcast_port, bool loopback);

int mcast_client_sock_setup(struct osmo_fd *ofd, const char *mcast_group, uint16_t mcast_port,
			    int (*fd_rx_cb)(struct osmo_fd *ofd, unsigned int what),
			    void *osmo_fd_data);

int mcast_bidir_sock_tx(struct mcast_bidir_sock *bidir_sock, const uint8_t *data, unsigned int data_len);
int mcast_bidir_sock_rx(struct mcast_bidir_sock *bidir_sock, uint8_t *buf, unsigned int buf_len);
void mcast_bidir_sock_close(struct mcast_bidir_sock* bidir_sock);

