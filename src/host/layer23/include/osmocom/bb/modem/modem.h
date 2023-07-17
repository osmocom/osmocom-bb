#pragma once

#include <stdbool.h>

int modem_start(void);
int modem_gprs_attach_if_needed(struct osmocom_ms *ms);
int modem_sync_to_cell(struct osmocom_ms *ms);

enum modem_state {
	MODEM_ST_IDLE,
	MODEM_ST_ATTACHING,
	MODEM_ST_ATTACHED
};

struct modem_app {
	struct osmocom_ms *ms;
	enum modem_state modem_state;
};
extern struct modem_app app_data;
