#ifndef _CALYPSO_DSP_H
#define _CALYPSO_DSP_H

#include <calypso/dsp_api.h>
#include <rffe.h>

#define CAL_DSP_TGT_BB_LVL	80

struct gsm_time;

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
void dsp_memcpy_to_api(volatile uint16_t *dsp_buf, const uint8_t *mcu_buf, int n, int be);
void dsp_memcpy_from_api(uint8_t *mcu_buf, const volatile uint16_t *dsp_buf, int n, int be);
void dsp_load_afc_dac(uint16_t afc);
void dsp_load_apc_dac(uint16_t apc);
void dsp_load_tch_param(struct gsm_time *next_time,
                        uint8_t chan_mode, uint8_t chan_type, uint8_t chan_sub,
                        uint8_t tch_loop, uint8_t sync_tch, uint8_t tn);
void dsp_load_ciph_param(int mode, uint8_t *key);
void dsp_end_scenario(void);

void dsp_load_rx_task(uint16_t task, uint8_t burst_id, uint8_t tsc);
void dsp_load_tx_task(uint16_t task, uint8_t burst_id, uint8_t tsc);

static inline uint16_t
dsp_task_iq_swap(uint16_t dsp_task, uint16_t band_arfcn, int tx)
{
	if (rffe_iq_swapped(band_arfcn, tx))
		dsp_task |= 0x8000;
	return dsp_task;
}

#endif
