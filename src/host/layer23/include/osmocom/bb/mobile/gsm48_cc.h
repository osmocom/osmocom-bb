#ifndef _GSM48_CC_H
#define _GSM48_CC_H

struct gsm48_cclayer {
        struct osmocom_ms       *ms;

	struct llist_head	mncc_upqueue;
};

int gsm48_cc_init(struct osmocom_ms *ms);
int gsm48_cc_exit(struct osmocom_ms *ms);
int gsm48_rcv_cc(struct osmocom_ms *ms, struct msgb *msg);
int mncc_dequeue(struct osmocom_ms *ms);
int mncc_tx_to_cc(void *inst, int msg_type, void *arg);
int mncc_clear_trans(void *inst, uint8_t protocol);

#endif /* _GSM48_CC_H */

