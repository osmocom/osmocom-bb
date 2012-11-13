#ifndef _GSM0411_SMC_H
#define _GSM0411_SMC_H

#include <osmocom/core/timer.h>
#include <osmocom/gsm/protocol/gsm_04_11.h>

#define GSM411_MMSMS_EST_REQ		0x310
#define GSM411_MMSMS_EST_IND		0x312
#define GSM411_MMSMS_EST_CNF		0x311
#define GSM411_MMSMS_REL_REQ		0x320
#define GSM411_MMSMS_REL_IND		0x322
#define GSM411_MMSMS_DATA_REQ		0x330
#define GSM411_MMSMS_DATA_IND		0x332
#define GSM411_MMSMS_UNIT_DATA_REQ	0x340
#define GSM411_MMSMS_UNIT_DATA_IND	0x342
#define GSM411_MMSMS_ERR_IND		0x372

#define GSM411_MNSMS_ABORT_REQ		0x101
#define GSM411_MNSMS_DATA_REQ		0x102
#define GSM411_MNSMS_DATA_IND		0x103
#define GSM411_MNSMS_EST_REQ		0x104
#define GSM411_MNSMS_EST_IND		0x105
#define GSM411_MNSMS_ERROR_IND		0x106
#define GSM411_MNSMS_REL_REQ		0x107

struct gsm411_smc_inst {
	int network;		/* is this a MO (0) or MT (1) transfer */
	int (*mn_recv) (struct gsm411_smc_inst *inst, int msg_type,
			struct msgb *msg);
	int (*mm_send) (struct gsm411_smc_inst *inst, int msg_type,
			struct msgb *msg, int cp_msg_type);

	enum gsm411_cp_state cp_state;
	struct osmo_timer_list cp_timer;
	struct msgb *cp_msg;	/* store pending message */
	int cp_rel;		/* store pending release */
	int cp_retx;		/* retry counter */
	int cp_max_retr;	/* maximum retry */
	int cp_tc1;		/* timer value TC1* */

};

extern const struct value_string gsm411_cp_cause_strs[];

/* init a new instance */
void gsm411_smc_init(struct gsm411_smc_inst *inst, int network,
	int (*mn_recv) (struct gsm411_smc_inst *inst, int msg_type,
			struct msgb *msg),
	int (*mm_send) (struct gsm411_smc_inst *inst, int msg_type,
			struct msgb *msg, int cp_msg_type));

/* clear instance */
void gsm411_smc_clear(struct gsm411_smc_inst *inst);

/* message from upper layer */
int gsm411_smc_send(struct gsm411_smc_inst *inst, int msg_type,
	struct msgb *msg);

/* message from lower layer */
int gsm411_smc_recv(struct gsm411_smc_inst *inst, int msg_type,
	struct msgb *msg, int cp_msg_type);

#endif /* _GSM0411_SMC_H */
