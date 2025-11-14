#pragma once

#include <stdint.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/timer.h>

struct osmocom_ms;

enum gsm_call_type {
	GSM_CALL_T_UNKNOWN = 0,
	GSM_CALL_T_VOICE,
	GSM_CALL_T_DATA, /* UDI or 3.1 kHz audio */
	GSM_CALL_T_DATA_FAX,
};

#define DTMF_ST_IDLE		0	/* no DTMF active */
#define DTMF_ST_START		1	/* DTMF started, waiting for resp. */
#define DTMF_ST_MARK		2	/* wait tone duration */
#define DTMF_ST_STOP		3	/* DTMF stopped, waiting for resp. */
#define DTMF_ST_SPACE		4	/* wait space between tones */

#define RINGER_MARK		0, 500000
#define RINGER_SPACE		0, 250000

#define CALL_ST_IDLE		0	/* no state */
#define CALL_ST_MO_INIT		1	/* call initiated, no response yet */
#define CALL_ST_MO_PROC		2	/* call proceeding */
#define CALL_ST_MO_ALERT	3	/* call alerting */
#define CALL_ST_MT_RING		4	/* call ringing */
#define CALL_ST_MT_KNOCK	5	/* call knocking */
#define CALL_ST_ACTIVE		6	/* call connected and active */
#define CALL_ST_HOLD		7	/* call connected, but on hold */
#define CALL_ST_DISC_RX		8	/* call disconnected (disc received) */
#define CALL_ST_DISC_TX		9	/* call disconnected (disc sent)  */

struct gsm_call {
	struct llist_head	entry;

	struct osmocom_ms	*ms;

	uint32_t		callref;
	enum gsm_call_type	type;

        uint8_t                 call_state;

        char                    number[33]; /* remote number */

	struct osmo_timer_list	dtmf_timer;
	uint8_t			dtmf_state;
	uint8_t			dtmf_index;
	char			dtmf[32]; /* dtmf sequence */

        struct osmo_timer_list  ringer_timer;
        uint8_t                 ringer_state;
};

int mncc_recv_dummy(struct osmocom_ms *ms, int msg_type, void *arg);
int mncc_recv_socket(struct osmocom_ms *ms, int msg_type, void *arg);
int mncc_recv_internal(struct osmocom_ms *ms, int msg_type, void *arg);
int mncc_recv_external(struct osmocom_ms *ms, int msg_type, void *arg);
int mnccms_init(struct osmocom_ms *ms);
void mnccms_exit(struct osmocom_ms *ms);
int mncc_call(struct osmocom_ms *ms, const char *number,
	      enum gsm_call_type call_type);
int mncc_hangup(struct osmocom_ms *ms, int index);
int mncc_answer(struct osmocom_ms *ms, int index);
int mncc_hold(struct osmocom_ms *ms, int index);
int mncc_retrieve(struct osmocom_ms *ms, int index);
int mncc_dtmf(struct osmocom_ms *ms, int index, char *dtmf);
int mncc_list(struct osmocom_ms *ms);
