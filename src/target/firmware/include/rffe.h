#ifndef _RFFE_H
#define _RFFE_H

#include <osmocore/gsm_utils.h>

extern const uint8_t system_inherent_gain;

/* initialize RF Frontend */
void rffe_init(void);

/* switch RF Frontend Mode */
void rffe_mode(enum gsm_band band, int tx);

/* get current gain of RF frontend (anything between antenna and baseband in dBm */
uint8_t rffe_get_gain(void);

void rffe_set_gain(int16_t exp_inp, int16_t target_bb);

#endif
