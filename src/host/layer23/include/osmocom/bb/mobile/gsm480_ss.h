#ifndef _GSM480_SS_H
#define _GSM480_SS_H

int gsm480_ss_init(struct osmocom_ms *ms);
int gsm480_ss_exit(struct osmocom_ms *ms);
int gsm480_rcv_ss(struct osmocom_ms *ms, struct msgb *msg);
int ss_send(struct osmocom_ms *ms, const char *code, int new_trans);

#endif /* _GSM480_SS_H */
