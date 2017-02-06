#pragma once

#include <netinet/in.h>
#include <osmocom/core/select.h>

struct mcast_server_sock {
	struct osmo_fd *osmo_fd;
	struct sockaddr_in *sock_conf;
};

struct mcast_client_sock {
	struct osmo_fd *osmo_fd;
	struct ip_mreq *mcast_group;
};

struct mcast_bidir_sock {
	struct mcast_server_sock *tx_sock;
	struct mcast_client_sock *rx_sock;
};

struct mcast_bidir_sock *mcast_bidir_sock_setup(
                void *ctx, char* tx_mcast_group, int tx_mcast_port,
                char* rx_mcast_group, int rx_mcast_port, int loopback,
                int (*fd_rx_cb)(struct osmo_fd *ofd, unsigned int what),
                void *osmo_fd_data);

struct mcast_server_sock *mcast_server_sock_setup(void *ctx,
                                                  char* tx_mcast_group,
                                                  int tx_mcast_port,
                                                  int loopback);
struct mcast_client_sock *mcast_client_sock_setup(
                void *ctx, char* mcast_group, int mcast_port,
                int (*fd_rx_cb)(struct osmo_fd *ofd, unsigned int what),
                void *osmo_fd_data);
int mcast_client_sock_rx(struct mcast_client_sock *client_sock, void* buf,
                         int buf_len);
int mcast_server_sock_tx(struct mcast_server_sock *serv_sock, void* data,
                         int data_len);
int mcast_bidir_sock_tx(struct mcast_bidir_sock *bidir_sock, void* data,
                        int data_len);
int mcast_bidir_sock_rx(struct mcast_bidir_sock *bidir_sock, void* buf,
                        int buf_len);
void mcast_client_sock_close(struct mcast_client_sock* client_sock);
void mcast_server_sock_close(struct mcast_server_sock* server_sock);
void mcast_bidir_sock_close(struct mcast_bidir_sock* bidir_sock);

