#ifndef _TRANSACT_H
#define _TRANSACT_H

#include <osmocom/core/linuxlist.h>
#include <osmocom/gsm/gsm0411_smc.h>
#include <osmocom/gsm/gsm0411_smr.h>

/* One transaction */
struct gsm_trans {
	/* Entry in list of all transactions */
	struct llist_head entry;

	/* The protocol within which we live */
	uint8_t protocol;

	/* The current transaction ID */
	uint8_t transaction_id;
	
	/* To whom we belong */
	struct osmocom_ms *ms;

	/* reference from MNCC or other application */
	uint32_t callref;

	/* if traffic channel receive was requested */
	int tch_recv;

	union {
		struct {

			/* current call state */
			int state;

			/* most recent progress indicator */
			uint8_t prog_ind;

			/* current timer and message queue */
			int Tcurrent;		/* current CC timer */
			int T308_second;	/* used to send release again */
			struct osmo_timer_list timer;
			struct gsm_mncc msg;	/* stores setup/disconnect/release message */
		} cc;
		struct {
			/* current supp.serv. state */
			int state;

			uint8_t invoke_id;
			struct msgb *msg;
		} ss;
		struct {
			uint8_t sapi;	/* SAPI to be used for this trans */

			struct gsm411_smc_inst smc_inst;
			struct gsm411_smr_inst smr_inst;

			struct gsm_sms *sms;
		} sms;
	};
};



struct gsm_trans *trans_find_by_id(struct osmocom_ms *ms,
				   uint8_t proto, uint8_t trans_id);
struct gsm_trans *trans_find_by_callref(struct osmocom_ms *ms,
					uint32_t callref);

struct gsm_trans *trans_alloc(struct osmocom_ms *ms,
			      uint8_t protocol, uint8_t trans_id,
			      uint32_t callref);
void trans_free(struct gsm_trans *trans);

int trans_assign_trans_id(struct osmocom_ms *ms,
			  uint8_t protocol, uint8_t ti_flag);

#endif
