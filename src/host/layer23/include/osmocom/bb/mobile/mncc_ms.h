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
int mncc_hangup(struct osmocom_ms *ms);
int mncc_answer(struct osmocom_ms *ms);
int mncc_hold(struct osmocom_ms *ms);
int mncc_retrieve(struct osmocom_ms *ms, int number);
int mncc_dtmf(struct osmocom_ms *ms, char *dtmf);
int mncc_list(struct osmocom_ms *ms);
