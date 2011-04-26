#ifndef _OSMOCOM_RSLMS_H
#define _OSMOCOM_RSLMS_H

#include <osmocom/core/msgb.h>
#include <osmocom/bb/common/osmocom_data.h>

/* From L3 into RSLMS (direction -> L2) */

/* Send a 'simple' RLL request to L2 */
int rslms_tx_rll_req(struct osmocom_ms *ms, uint8_t msg_type,
		     uint8_t chan_nr, uint8_t link_id);

/* Send a RLL request (including L3 info) to L2 */
int rslms_tx_rll_req_l3(struct osmocom_ms *ms, uint8_t msg_type,
			uint8_t chan_nr, uint8_t link_id, struct msgb *msg);


/* From L2 into RSLMS (direction -> L3) */

/* input function that L2 calls when sending messages up to L3 */
//int rslms_sendmsg(struct msgb *msg, struct osmocom_ms *ms);

#endif /* _OSMOCOM_RSLMS_H */
