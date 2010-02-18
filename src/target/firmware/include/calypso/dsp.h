#ifndef _CALYPSO_DSP_H
#define _CALYPSO_DSP_H

#include <calypso/dsp_api.h>

struct dsp_api {
	T_NDB_MCU_DSP *ndb;
	T_DB_DSP_TO_MCU *db_r;
	T_DB_MCU_TO_DSP *db_w;
	T_PARAM_MCU_DSP *param;
	int r_page;
	int w_page;
	int r_page_used;
	int frame_ctr;
};

extern struct dsp_api dsp_api;

void dsp_power_on(void);
void dsp_dump_version(void);
void dsp_dump(void);
void dsp_checksum_task(void);
void dsp_api_memset(uint16_t *ptr, int octets);
void dsp_load_afc_dac(uint16_t afc);
void dsp_load_apc_dac(uint16_t apc);
void dsp_end_scenario(void);

void dsp_load_rx_task(uint16_t task, uint8_t burst_id, uint8_t tsc);
void dsp_load_tx_task(uint16_t task, uint8_t burst_id, uint8_t tsc);

#endif
