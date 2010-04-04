#ifndef _TRANSACT_H
#define _TRANSACT_H

#include <osmocore/linuxlist.h>

/* One transaction */
struct gsm_trans {
	/* Entry in list of all transactions */
	struct llist_head entry;

	/* The protocol within which we live */
	u_int8_t protocol;

	/* The current transaction ID */
	u_int8_t transaction_id;
	
	/* To whom we belong */
	struct osmocom_ms *ms;

	/* reference from MNCC or other application */
	u_int32_t callref;

	/* if traffic channel receive was requested */
	int tch_recv;

	union {
		struct {

			/* current call state */
			int state;

			/* current timer and message queue */
			int Tcurrent;		/* current CC timer */
			int T308_second;	/* used to send release again */
			struct timer_list timer;
			struct gsm_mncc msg;	/* stores setup/disconnect/release message */
		} cc;
		struct {
			u_int8_t link_id;	/* RSL Link ID to be used for this trans */
			int is_mt;	/* is this a MO (0) or MT (1) transfer */
			enum gsm411_cp_state cp_state;
			struct timer_list cp_timer;

			enum gsm411_rp_state rp_state;

			struct gsm_sms *sms;
		} sms;
	};
};



struct gsm_trans *trans_find_by_id(struct osmocom_ms *ms,
				   u_int8_t proto, u_int8_t trans_id);
struct gsm_trans *trans_find_by_callref(struct gsm_network *net,
					u_int32_t callref);

struct gsm_trans *trans_alloc(struct osmocom_ms *ms,
			      u_int8_t protocol, u_int8_t trans_id,
			      u_int32_t callref);
void trans_free(struct gsm_trans *trans);

int trans_assign_trans_id(struct osmocom_ms *ms,
			  u_int8_t protocol, u_int8_t ti_flag);

#endif
