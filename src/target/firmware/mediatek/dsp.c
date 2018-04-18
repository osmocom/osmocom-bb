#include <stdint.h>
#include <calypso/dsp.h>
#include <calypso/dsp_api.h>

// borrowed from calypso/dsp.c in various places
struct dsp_api dsp_api = {
	.ndb	= (T_NDB_MCU_DSP *) BASE_API_NDB,
	.db_r	= (T_DB_DSP_TO_MCU *) BASE_API_R_PAGE_0,
	.db_w	= (T_DB_MCU_TO_DSP *) BASE_API_W_PAGE_0,
	.param	= (T_PARAM_MCU_DSP *) BASE_API_PARAM,
	.r_page	= 0,
	.w_page = 0,
};

void dsp_power_on(void) {}
void dsp_load_ciph_param(int mode, uint8_t *key) {}
void dsp_memcpy_to_api(volatile uint16_t *dsp_buf, const uint8_t *mcu_buf, int n, int be) {}
void dsp_load_tch_param(struct gsm_time *next_time,
                        uint8_t chan_mode, uint8_t chan_type, uint8_t chan_sub,
                        uint8_t tch_loop, uint8_t sync_tch, uint8_t tn) {}
void dsp_load_tx_task(uint16_t task, uint8_t burst_id, uint8_t tsc) {}
void dsp_memcpy_from_api(uint8_t *mcu_buf, const volatile uint16_t *dsp_buf, int n, int be) {}
void dsp_load_rx_task(uint16_t task, uint8_t burst_id, uint8_t tsc) {}
void dsp_api_memset(uint16_t *ptr, int octets) {}
void dsp_end_scenario(void) {}
