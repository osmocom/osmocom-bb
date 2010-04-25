#ifndef _GSM48_CC_H
#define _GSM48_CC_H

struct gsm48_cclayer {
        struct osmocom_ms       *ms;

	struct llist_head	mncc_upqueue;
	int			(*mncc_recv)(struct osmocom_ms *, int, void *);
};

int gsm48_cc_init(struct osmocom_ms *ms);
int gsm48_cc_exit(struct osmocom_ms *ms);
int gsm48_rcv_cc(struct osmocom_ms *ms, struct msgb *msg);
int mncc_dequeue(struct osmocom_ms *ms);
int mncc_send(struct osmocom_ms *ms, int msg_type, void *arg);

#endif /* _GSM48_CC_H */

