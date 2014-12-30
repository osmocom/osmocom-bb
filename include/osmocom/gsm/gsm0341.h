#pragma once

#include <osmocom/gsm/protocol/gsm_03_41.h>

struct gsm341_ms_message *
gsm0341_build_msg(void *ctx, uint8_t geo_scope, uint8_t msg_code,
		  uint8_t update, uint16_t msg_id, uint8_t dcs,
		  uint8_t page_total, uint8_t page_cur,
		  uint8_t *data, uint8_t len);
