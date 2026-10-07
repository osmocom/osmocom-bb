#pragma once

#include <osmocom/core/fsm.h>

#include <osmocom/bb/trxcon/phyif.h>

struct trx_instance;

struct trx_if_params {
	const char *local_host;
	const char *remote_host;
	uint16_t base_port;
	uint32_t fn_advance;
	uint8_t instance;

	struct osmo_fsm_inst *parent_fi;
	uint32_t parent_term_event;
	void *priv;
};

struct trx_instance *trx_if_open(const struct trx_if_params *params);
void trx_if_close(struct trx_instance *trx);

int trx_if_handle_phyif_burst_req(struct trx_instance *trx, const struct trxcon_phyif_burst_req *br);
int trx_if_handle_phyif_cmd(struct trx_instance *trx, const struct trxcon_phyif_cmd *cmd);
