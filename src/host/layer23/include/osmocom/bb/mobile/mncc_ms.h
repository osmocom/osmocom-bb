#pragma once

#include <stdint.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/timer.h>

struct osmocom_ms;

struct gsm_call {
	struct llist_head	entry;

	struct osmocom_ms	*ms;

	uint32_t		callref;

	bool			init; /* call initiated, no response yet */
	bool			hold; /* call on hold */
	bool			ring; /* call ringing/knocking */

	struct osmo_timer_list	dtmf_timer;
	uint8_t			dtmf_state;
	uint8_t			dtmf_index;
	char			dtmf[32]; /* dtmf sequence */
};

int mncc_call(struct osmocom_ms *ms, const char *number);
int mncc_hangup(struct osmocom_ms *ms);
int mncc_answer(struct osmocom_ms *ms);
int mncc_hold(struct osmocom_ms *ms);
int mncc_retrieve(struct osmocom_ms *ms, int number);
int mncc_dtmf(struct osmocom_ms *ms, char *dtmf);

