#ifndef _MNCC_SOCK_H
#define _MNCC_SOCK_H

struct mncc_sock_state {
	void *inst;
	struct osmo_fd listen_bfd;	/* fd for listen socket */
	struct osmo_fd conn_bfd;		/* fd for connection to lcr */
	struct llist_head upqueue;
};

int mncc_sock_from_cc(struct mncc_sock_state *state, struct msgb *msg);
void mncc_sock_write_pending(struct mncc_sock_state *state);
struct mncc_sock_state *mncc_sock_init(void *inst, const char *name, void *tall_ctx);
void mncc_sock_exit(struct mncc_sock_state *state);

#endif /* _MNCC_SOCK_H */
