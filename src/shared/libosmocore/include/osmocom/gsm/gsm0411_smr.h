#ifndef _GSM0411_SMR_H
#define _GSM0411_SMR_H

#include <osmocom/gsm/protocol/gsm_04_11.h>

#define GSM411_SM_RL_DATA_REQ		0x401
#define GSM411_SM_RL_DATA_IND		0x402
#define GSM411_SM_RL_MEM_AVAIL_REQ	0x403
#define GSM411_SM_RL_MEM_AVAIL_IND	0x404
#define GSM411_SM_RL_REPORT_REQ		0x405
#define GSM411_SM_RL_REPORT_IND		0x406

struct gsm411_smr_inst {
	int network;		/* is this a MO (0) or MT (1) transfer */
	int (*rl_recv) (struct gsm411_smr_inst *inst, int msg_type,
			struct msgb *msg);
	int (*mn_send) (struct gsm411_smr_inst *inst, int msg_type,
			struct msgb *msg);

	enum gsm411_rp_state rp_state;
	struct osmo_timer_list rp_timer;
};

extern const struct value_string gsm411_rp_cause_strs[];

/* init a new instance */
void gsm411_smr_init(struct gsm411_smr_inst *inst, int network,
	int (*rl_recv) (struct gsm411_smr_inst *inst, int msg_type,
			struct msgb *msg),
	int (*mn_send) (struct gsm411_smr_inst *inst, int msg_type,
			struct msgb *msg));

/* clear instance */
void gsm411_smr_clear(struct gsm411_smr_inst *inst);

/* message from upper layer */
int gsm411_smr_send(struct gsm411_smr_inst *inst, int msg_type,
	struct msgb *msg);

/* message from lower layer */
int gsm411_smr_recv(struct gsm411_smr_inst *inst, int msg_type,
	struct msgb *msg);

#endif /* _GSM0411_SMR_H */

